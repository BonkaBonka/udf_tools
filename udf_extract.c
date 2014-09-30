#include <assert.h>
#include <stdio.h>

#include <dvdread/dvd_reader.h>

int main(int argc, char *argv[]) {
	dvd_reader_t *device;
	dvd_file_t *file;
	ssize_t count;

	assert(argc == 3);
	device = DVDOpen(argv[1]);
	assert(device != NULL);

	file = DVDOpenFilename(device, argv[2]);
	assert(file != NULL);

	unsigned char buffer[DVDFileSize64(file)];

	count = DVDReadBytes(file, buffer, sizeof(buffer));
	assert(count == sizeof(buffer));

	fwrite(buffer, sizeof(buffer), 1, stdout);

	DVDCloseFile(file);

	DVDClose(device);

	return 0;
}
