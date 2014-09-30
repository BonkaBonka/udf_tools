/*
 * This code is based on dvdudf by:
 *   Christian Wolff <scarabaeus@convergence.de>.
 *
 * Modifications by:
 *   Billy Biggs <vektor@dumbterm.net>.
 *   Björn Englund <d4bjorn@dtek.chalmers.se>.
 *
 * dvdudf: parse and read the UDF volume information of a DVD Video
 * Copyright (C) 1999 Christian Wolff for convergence integrated media
 * GmbH The author can be reached at scarabaeus@convergence.de, the
 * project's page is at http://linuxtv.org/dvd/
 *
 * This file is part of libdvdread.
 *
 * libdvdread is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * libdvdread is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libdvdread; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LIBDVDREAD_DVD_UDF_H
#define LIBDVDREAD_DVD_UDF_H

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif


/*
 *
 * UDF defines struct AD, which dvdread unfortunately overloads to refer to
 * a file, creating problems with chained ADs. We need to define a generic
 * UDF_FILE structure to hold the information required.
 * It could be worth a separate struct for Directories, since they are (probably)
 * only ever in 1 chain.
 *
 * Is there a real max number of chains? What can fit in 2048?
 * My 4.4GB file example uses 5 chains. A BD ISO can store 50GB of data, so
 * potentially we could have a 50GB file, so we would need to be able to
 * support 62 chains.
 *
 */

#ifndef UDF_MAX_AD_CHAINS     // Allow MAX to be increased by API.
#define UDF_MAX_AD_CHAINS 50
#endif


/**
 * The length of one Logical Block of a DVD.
 */
#define DVD_VIDEO_LB_LEN 2048


/**
 * Maximum length of filenames allowed in UDF.
 */
#define MAX_UDF_FILE_NAME_LEN 2048


#ifndef NULL
#define NULL ((void *)0)
#endif


/**
 * Opaque type that is used as a handle for one instance of an opened DVD.
 */
typedef struct dvd_reader_s dvd_reader_t;

/**
 * Opaque type for a file read handle, much like a normal fd or FILE *.
 */
typedef struct dvd_file_s dvd_file_t;

/**
 * Public type that is used to provide statistics on a handle.
 */

typedef struct dvd_input_s *dvd_input_t;




struct AD {
    uint32_t Location;
    uint32_t Length;
    uint8_t  Flags;
    uint16_t Partition;
};

struct extent_ad {
    uint32_t location;
    uint32_t length;
};

struct avdp_t {
    struct extent_ad mvds;
    struct extent_ad rvds;
};

struct UDF_FILE {
    uint64_t Length;
    uint32_t num_AD;
    //uint32_t Partition_Start;
    uint16_t flags; // From ICBTAG
    uint32_t content_offset; // When flags&7==3
    struct AD AD_chain[UDF_MAX_AD_CHAINS]; // Location(s) of the file data/content
    uint32_t info_location;                // Location of the 260/266 FileInfo
};

typedef struct UDF_FILE udf_file_t;

struct Partition {
    int valid;
    char VolumeDesc[128];
    uint16_t Flags;
    uint16_t Number;
    char Contents[32];
    uint32_t AccessType;
    uint32_t Start;
    uint32_t Length;

    uint32_t fsd_location;

    udf_file_t Metadata_Mainfile;   // To hold the AD_chain
    udf_file_t Metadata_Mirrorfile; // To hold the AD_chain

};


struct space_bitmap {
    uint32_t NumberOfBits;
    uint32_t NumberOfBytes;
};


struct pvd_t {
    uint8_t VolumeIdentifier[32];
    uint8_t VolumeSetIdentifier[128];
};

struct lbudf {
    uint32_t lb;
    uint8_t *data;
    /* needed for proper freeing */
    uint8_t *data_base;
};

struct icbmap {
    uint32_t lbn;
    udf_file_t file;
    uint8_t filetype;
};

struct udf_cache {
    int avdp_valid;
    struct avdp_t avdp;
    int pvd_valid;
    struct pvd_t pvd;
    int partition_valid;
    struct Partition partition;
    int rooticb_valid;
    struct AD rooticb;
    int lb_num;
    struct lbudf *lbs;
    int map_num;
    struct icbmap *maps;
};

struct MetadataPartition {
    uint8_t PartMapType;
    uint8_t PartMapLen;
    // Identifier Flags (1) + String (23) + Suffix (8)
    uint8_t IdentifierStr[23];

    // Sparable Table
    uint16_t VolSeq;
    uint16_t PartNum;
    uint16_t PacketLen;
    uint8_t  N_ST;
    uint32_t EachSize;

    // Metadata
    uint32_t MainFileLocation;
    uint32_t MirrorFileLocation;
    uint32_t BitmapFileLocation;
    uint32_t AllocationUnitSize;
    uint16_t AlignmentUnitSize;
    uint8_t  Flags;

};


typedef struct {
  off_t size;          /**< Total size of file in bytes */
  int nr_parts;        /**< Number of file parts */
  off_t parts_size[9]; /**< Size of each part in bytes */
} dvd_stat_t;

/*
 * DVDReaddir entry types.
 */
typedef enum {
  DVD_DT_UNKNOWN = 0,
  DVD_DT_FIFO,
  DVD_DT_CHR,
  DVD_DT_DIR,
  DVD_DT_BLK,
  DVD_DT_REG,
  DVD_DT_LNK,
  DVD_DT_SOCK,
  DVD_DT_WHT
} dvd_dir_type_t;

/*
 * DVDReaddir structure.
 * Extended a little from POSIX to also return filesize.
 */
typedef struct {
  unsigned char  d_name[MAX_UDF_FILE_NAME_LEN];
    /* "Shall not exceed 1023; Ecma-167 page 123" */
  dvd_dir_type_t d_type;       // DT_REG, DT_DIR
  unsigned int   d_namlen;
  uint64_t       d_filesize;
  struct UDF_FILE dir_file;         /* The AD_chain of the found entry */
} dvd_dirent_t;



/*
 * DVDOpendir DIR* structure
 */
typedef struct {
  uint32_t dir_location;
  uint32_t dir_length;
  uint32_t dir_current;   /* Separate to _location should we one day want to
                           * implement dir_rewind() */
  unsigned int current_p; /* Internal implementation specific. UDFScanDirX */
  dvd_dirent_t entry;
  struct UDF_FILE *dir_file;         /* The AD_chain of the listing directory */
} dvd_dir_t;

struct udf_cache_s {
    uint32_t lbnumber;
    unsigned char data[DVD_VIDEO_LB_LEN];
};
typedef struct udf_cache_s udf_cache_t;

#define NUM_UDF_CACHE 256 // x2 KB in memory use.

struct dvd_reader_s {
  /* Basic information. */
  int isImageFile;

  /* Hack for keeping track of the css status.
   * 0: no css, 1: perhaps (need init of keys), 2: have done init */
  int css_state;
  int css_title; /* Last title that we have called dvdinpute_title for. */

  /* Information required for an image file. */
  dvd_input_t dev;

  /* Information required for a directory path drive. */
  char *path_root;

  /* Filesystem cache */
  int udfcache_level; /* 0 - turned off, 1 - on */
  int cache_index;
  udf_cache_t udf_cache[NUM_UDF_CACHE];

  struct Partition partition;
  struct MetadataPartition meta_partition;
  struct UDF_FILE RootDirectory;

};



typedef enum {
    PartitionCache, RootICBCache, LBUDFCache, MapCache, AVDPCache, PVDCache
} UDFCacheType;




/**
 * Looks for a file on the UDF disc/imagefile and returns the block number
 * where it begins, or 0 if it is not found.  The filename should be an
 * absolute pathname on the UDF filesystem, starting with '/'.  For example,
 * '/VIDEO_TS/VTS_01_1.IFO'.  On success, filesize will be set to the size of
 * the file in bytes.
 */
udf_file_t *UDFFindFile(
                        dvd_reader_t *device,
                        char *filename,
                        uint64_t *size );
void        UDFFreeFile( dvd_reader_t *device, udf_file_t *udf_file );
uint32_t    UDFFileBlockDir( dvd_reader_t *device, udf_file_t *udf_file, uint32_t file_block);
uint32_t    UDFFileBlockFile( dvd_reader_t *device, udf_file_t *udf_file, uint32_t file_block);
int         UDFScanDirX( dvd_reader_t *device, dvd_dir_t *dirp );
void FreeUDFCache(void *cache);
int UDFGetVolumeIdentifier(dvd_reader_t *device,
                           char *volid, unsigned int volid_size);
int UDFGetVolumeSetIdentifier(dvd_reader_t *device,
                              uint8_t *volsetid, unsigned int volsetid_size);
void *GetUDFCacheHandle(dvd_reader_t *device);
void SetUDFCacheHandle(dvd_reader_t *device, void *cache);
int UDFOpen( dvd_reader_t *device );

#ifdef __cplusplus
};
#endif
#endif /* LIBDVDREAD_DVD_UDF_H */
