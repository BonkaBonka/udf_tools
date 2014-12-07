#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <openssl/evp.h>

#include <dvdread/dvd_reader.h>
#include <dvdread/dvd_udf.h>

#include <jansson.h>

#define HASH_NAME "SHA512"

// http://stackoverflow.com/a/744881
int filename_endswith(const char *filename, const char *extension) {
	char *dot = strrchr(filename, '.');
	return (NULL == dot) ? 0 : strcasecmp(dot, extension) == 0;
}

// http://stackoverflow.com/a/17147874
char *tohex(unsigned char *bin, size_t binsz) {
	static const char hex_str[]= "0123456789abcdef";
	char *result;
	unsigned int i;

	if (!binsz)
		return NULL;

	result = malloc(binsz * 2 + 1);
	result[binsz * 2] = 0;

	for (i = 0; i < binsz; i++)	{
		result[i * 2 + 0] = hex_str[bin[i] >> 4];
		result[i * 2 + 1] = hex_str[bin[i] & 0x0F];
	}

	return result;
}

void process_file(dvd_reader_t *device, char *filename, EVP_MD_CTX *messagedigest_context, char *ext) {
	if (filename_endswith(filename, ext)) {
		dvd_file_t *file = DVDOpenFilename(device, filename);
		ssize_t count, offset;
		unsigned char buffer[DVDFileSize64(file)];

		offset = 0;
		while (offset < sizeof(buffer)) {
			count = DVDReadBytes(file, buffer + offset, sizeof(buffer) - offset);
			assert(count > 0);
			offset += count;
		}
		EVP_DigestUpdate(messagedigest_context, buffer, sizeof(buffer));

		DVDCloseFile(file);
	}
}

void process_directory(dvd_reader_t *device, char *dirname, EVP_MD_CTX *messagedigest_context, char *ext) {
	char path[MAX_UDF_FILE_NAME_LEN + 1];
	dvd_dir_t *dir;
	dvd_dirent_t *dirent;

	path[sizeof(path) - 1] = 0;

	dir = DVDOpenDir(device, dirname);
	assert(dir != NULL);

	while ((dirent = DVDReadDir(device, dir)) != NULL) {
		snprintf(path, sizeof(path) - 1, "%s/%s", dirname, dirent->d_name);

		switch (dirent->d_type) {
			case DVD_DT_DIR:
				process_directory(device, path, messagedigest_context, ext);
				break;
			case DVD_DT_REG:
				process_file(device, path, messagedigest_context, ext);
				break;
			default:
				fprintf(stderr, "Unhandled type %d\n", dirent->d_type);
				return;
		}
	}

	DVDCloseDir(device, dir);
}

int main(int argc, char *argv[]) {
	char volid[32];
	unsigned char volsetid[128];
	unsigned char messagedigest_value[EVP_MAX_MD_SIZE];
	unsigned int messagedigest_len;
	char *ext;
	char *str;
	dvd_reader_t *device;
	dvd_file_t *file;
	json_t *obj = json_object();
	const EVP_MD *messagedigest;
	EVP_MD_CTX messagedigest_context;

	OpenSSL_add_all_digests();
	messagedigest = EVP_get_digestbyname(HASH_NAME);
	assert(messagedigest != NULL);

	EVP_MD_CTX_init(&messagedigest_context);
	EVP_DigestInit_ex(&messagedigest_context, messagedigest, NULL);

	assert(argc == 2);
	device = DVDOpen(argv[1]);
	assert(device != NULL);

	memset(volid, 0, sizeof(volid));
	DVDUDFVolumeInfo(device, volid, sizeof(volid), volsetid, sizeof(volsetid));
	EVP_DigestUpdate(&messagedigest_context, volid, sizeof(volid));
	EVP_DigestUpdate(&messagedigest_context, volsetid, sizeof(volsetid));

	json_object_set_new(obj, "udf_vol_id", json_string(volid));

	str = tohex(volsetid, sizeof(volsetid));
	json_object_set_new(obj, "udf_vol_set_id", json_string(str));
	free(str);

	memset(volid, 0, sizeof(volid));
	DVDISOVolumeInfo(device, volid, sizeof(volid), volsetid, sizeof(volsetid));
	EVP_DigestUpdate(&messagedigest_context, volid, sizeof(volid));
	EVP_DigestUpdate(&messagedigest_context, volsetid, sizeof(volsetid));

	json_object_set_new(obj, "iso_vol_id", json_string(volid));

	str = tohex(volsetid, sizeof(volsetid));
	json_object_set_new(obj, "iso_vol_set_id", json_string(str));
	free(str);

	file = DVDOpenFilename(device, "/VIDEO_TS/VIDEO_TS.IFO");
	if (file != NULL) {
		DVDCloseFile(file);
		json_object_set_new(obj, "disc_type", json_string("DVD"));
		ext = ".IFO";
	} else {
		json_object_set_new(obj, "disc_type", json_string("Blu-Ray"));
		ext = ".XML";
	}

	process_directory(device, "", &messagedigest_context, ext);

	DVDClose(device);

	EVP_DigestFinal_ex(&messagedigest_context, messagedigest_value, &messagedigest_len);
	EVP_MD_CTX_cleanup(&messagedigest_context);

	str = tohex(messagedigest_value, messagedigest_len);
	json_object_set_new(obj, "hash_value", json_string(str));
	free(str);

	str = json_dumps(obj, 0);
	fputs(str, stdout);
	free(str);

	json_decref(obj);

	return 0;
}
