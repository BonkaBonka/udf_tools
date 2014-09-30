/*
 * This code is based on dvdudf by:
 *   Christian Wolff <scarabaeus@convergence.de>.
 *
 * Modifications by:
 *   Billy Biggs <vektor@dumbterm.net>.
 *   Björn Englund <d4bjorn@dtek.chalmers.se>.
 *
 *   Jörgen Lundman <lundman@lundman.net> Handle metadata partition,
 *       ICBTag.Flags type 3, POSIX Opendir and UDF 2.60 work.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <inttypes.h>

#include "dvdread/dvd_udf.h"
#include "dvdread_internal.h"
#include "dvdread/dvd_reader.h"


//#define DEBUG


#define ICB_DATA_IN_AD_SPACE(X) (((X)&7)==3)

/* For direct data access, LSB first */
#define GETN1(p) ((uint8_t)data[p])
#define GETN2(p) ((uint16_t)data[p] | ((uint16_t)data[(p) + 1] << 8))
#define GETN3(p) ((uint32_t)data[p] | ((uint32_t)data[(p) + 1] << 8)    \
                  | ((uint32_t)data[(p) + 2] << 16))
#define GETN4(p) ((uint32_t)data[p]                     \
                  | ((uint32_t)data[(p) + 1] << 8)      \
                  | ((uint32_t)data[(p) + 2] << 16)     \
                  | ((uint32_t)data[(p) + 3] << 24))
#define GETN8(p) ((uint64_t)data[p]                     \
                  | ((uint64_t)data[(p) + 1] << 8)      \
                  | ((uint64_t)data[(p) + 2] << 16)     \
                  | ((uint64_t)data[(p) + 3] << 24)     \
                  | ((uint64_t)data[(p) + 4] << 32)     \
                  | ((uint64_t)data[(p) + 5] << 40)     \
                  | ((uint64_t)data[(p) + 6] << 48)     \
                  | ((uint64_t)data[(p) + 7] << 56))
/* This is wrong with regard to endianess */
#define GETN(p, n, target) memcpy(target, &data[p], n)


#ifdef DEBUG
char *TagIDStr(int id)
{
    switch (id) {
    case 1:
        return "   1 TAGID_PRI_VOL";
    case 2:
        return "   2 TAGID_ANCHOR";
    case 3:
        return "   3 TAGID_VOL";
    case 4:
        return "   4 TAGID_IMP_VOL";
    case 5:
        return "   5 TAGID_PARTITION";
    case 6:
        return "   6 TAGID_LOGVOL";
    case 7:
        return "   7 TAGID_UNALLOC_SPACE";
    case 8:
        return "   8 TAGID_TERM";
    case 9:
        return "   9 TAGID_LOGVOL_INTEGRITY";
    case 256:
        return " 256 TAGID_FSD";
    case 257:
        return " 257 TAGID_FID";
    case 258:
        return " 258 TAGID_ALLOCEXTENT";
    case 259:
        return " 259 TAGID_INDIRECTENTRY";
    case 260:
        return " 260 TAGID_ICB_TERM";
    case 261:
        return " 261 TAGID_FENTRY";
    case 262:
        return " 262 TAGID_EXTATTR_HDR";
    case 263:
        return " 263 TAGID_UNALL_SP_ENTRY";
    case 264:
        return " 264 TAGID_SPACE_BITMAP";
    case 265:
        return " 265 TAGID_PART_INTEGRETY";
    case 266:
        return " 266 TAGID_EXTFENTRY";
    }

    return NULL;
}
#endif

/* It's required to either fail or deliver all the blocks asked for. */
static int DVDReadLBUDF( dvd_reader_t *device, uint32_t lb_number,
                         size_t block_count, unsigned char *data,
                         int encrypted )
{
    int ret;
    size_t count = block_count;
#ifdef DEBUG
    uint32_t from = lb_number;
#endif

    while(count > 0) {

        ret = UDFReadBlocksRaw(device, lb_number, count, data, encrypted);

        if(ret <= 0) {
            /* One of the reads failed or nothing more to read, too bad.
             * We won't even bother returning the reads that went ok. */
            return ret;
        }

        count -= (size_t)ret;
        lb_number += (uint32_t)ret;
    }

#ifdef DEBUG
    {
        uint16_t TagID;
        TagID = GETN2(0);
        fprintf(stderr, "* Read Block %3d which is %s\r\n",
                from, TagIDStr(TagID));
    }
#endif

    return block_count;
}

// Look for block in cache. Start from last found place.
static unsigned char *cache_has(dvd_reader_t *device, uint32_t lb_number)
{
    int i;
    static int index = 0;

    for (i = 0; i < NUM_UDF_CACHE; i++) {
        if (device->udf_cache[index].lbnumber == lb_number)
            return device->udf_cache[index].data;
        index++;
        if (index >= NUM_UDF_CACHE)
            index = 0;
    }
    return NULL;
}

static void cache_add(dvd_reader_t *device, uint32_t lb_number, unsigned char *data)
{
    device->udf_cache[ device->cache_index ].lbnumber = lb_number;
    memcpy(device->udf_cache[ device->cache_index ].data, data, DVD_VIDEO_LB_LEN);

    device->cache_index++;
    if (device->cache_index >= NUM_UDF_CACHE)
        device->cache_index = 0;
}



static int DVDReadLBUDFCached( dvd_reader_t *device, uint32_t lb_number,
                               size_t block_count, unsigned char *data,
                               int encrypted )
{
    // for each block requested, check if it is already in cache

    size_t count = block_count;
    unsigned char *cache_data;

    if (!device->udfcache_level) return DVDReadLBUDF(device, lb_number, block_count,
                                                     data, encrypted);


    while(count > 0) {

        if ((cache_data = cache_has(device, lb_number))) {
            // It would be nicer if we could just work on the cache buffer
            memcpy(data, cache_data, DVD_VIDEO_LB_LEN);
#ifdef DEBUG
            fprintf(stderr, "CACHE: Using %u\r\n", lb_number);
#endif

        } else {
            if (UDFReadBlocksRaw(device, lb_number, 1, data, encrypted) != 1)
                return 0;
            cache_add(device, lb_number, data);
#ifdef DEBUG
            fprintf(stderr, "CACHE: Adding %u\r\n", lb_number);
#endif
        }

        // Move forward a block
        data += DVD_VIDEO_LB_LEN;
        count--;
        lb_number++;
    }
    return block_count;
}






static int Unicodedecode( uint8_t *data, int len, char *target )
{
    int p = 1, i = 0;
    int err = 0;

    if( ( data[ 0 ] == 8 ) || ( data[ 0 ] == 16 ) ) do {
            if( data[ 0 ] == 16 ) err |= data[p++];  /* character cannot be converted to 8bit, return error */
            if( p < len ) {
                target[ i++ ] = data[ p++ ];
            }
        } while( p < len );

    target[ i ] = '\0';
    return !err;
}

static int UDFDescriptor( uint8_t *data, uint16_t *TagID )
{
    *TagID = GETN2(0);
    /* TODO: check CRC 'n stuff */
    return 0;
}

static int UDFExtentAD( uint8_t *data, uint32_t *Length, uint32_t *Location )
{
    *Length   = GETN4(0);
    *Location = GETN4(4);
    return 0;
}

static int UDFSpaceBitmap( uint8_t *data, struct space_bitmap *bm)
{
    bm->NumberOfBits  = GETN4(2); // this can't be right
    bm->NumberOfBytes = GETN4(6);
    return 0;
}

static int UDFShortAD( uint8_t *data, struct AD *ad,
                       struct Partition *partition )
{
    ad->Length = GETN4(0);
    ad->Flags = ad->Length >> 30;
    ad->Length &= 0x3FFFFFFF;
    ad->Location = GETN4(4);
    ad->Partition = partition->Number; /* use number of current partition */
    return 0;
}

static int UDFLongAD( uint8_t *data, struct AD *ad )
{
    ad->Length = GETN4(0);
    ad->Flags = ad->Length >> 30;
    ad->Length &= 0x3FFFFFFF;
    ad->Location = GETN4(4);
    ad->Partition = GETN2(8);
    /* GETN(10, 6, Use); */
    return 0;
}

static int UDFExtAD( uint8_t *data, struct AD *ad )
{
    ad->Length = GETN4(0);
    ad->Flags = ad->Length >> 30;
    ad->Length &= 0x3FFFFFFF;
    ad->Location = GETN4(12);
    ad->Partition = GETN2(16);
    /* GETN(10, 6, Use); */
    return 0;
}

static int UDFICB( uint8_t *data, uint8_t *FileType, uint16_t *Flags )
{
    *FileType = GETN1(11);
    *Flags = GETN2(18);
    return 0;
}

static int UDFGetRootICB(uint8_t *data, struct AD *RootICB)
{

    UDFLongAD( &data[ 400 ], RootICB );
#ifdef DEBUG
    fprintf(stderr, "RootICB at %d length %d\r\n", RootICB->Location,
            RootICB->Length);
#endif
    return 0;
}



static int UDFPartition( dvd_reader_t *device, uint8_t *data)
{
    device->partition.Flags = GETN2(20);
    device->partition.Number = GETN2(22);
    GETN(24, 32, device->partition.Contents);
    device->partition.AccessType = GETN4(184);
    device->partition.Start = GETN4(188);
    device->partition.Length = GETN4(192);
#ifdef DEBUG
    fprintf(stderr, "Partiton: number %d, start %d, length %d, AccessType %d\r\n",
            device->partition.Number,
            device->partition.Start,
            device->partition.Length,
            device->partition.AccessType);
#endif
    return 0;
}

int UDFMapMetadataPartition(uint8_t *data, struct MetadataPartition *md)
{
    int i;
    uint32_t location;
    //udf250.pfd page 39
    md->PartMapType = GETN1(0);  // "2"
    md->PartMapLen  = GETN1(1);  // "64"
    // bytes 2-3 reserved
    // Identifier Flags (1) + String (23) + Suffix (8)
    GETN(5, 23, md->IdentifierStr);

#ifdef DEBUG
    fprintf(stderr, "Partition identifier: '%23.23s'\r\n", md->IdentifierStr);
#endif

    if (!strncasecmp("*UDF Sparable Partition", (char *)md->IdentifierStr, 23)) {

        md->VolSeq      = GETN2(36);
        md->PartNum     = GETN2(38);
        md->PacketLen   = GETN2(40);
        md->N_ST   = GETN1(42); // Number of Sparing Tables
        // bytes 43 reserved
        md->EachSize   = GETN4(44); // Size of Each Sparing Table

#ifdef DEBUG
        fprintf(stderr, "Sparable Partition VolSeq %04X PartNum %04X PacketLen %04X, Num Sparing Tables: %02X\r\n",
                md->VolSeq, md->PartNum, md->PacketLen, md->N_ST);
#endif

        for (i = 0; i < md->N_ST; i++) {
            location = GETN4(48 + (i<<2));
#ifdef DEBUG
            fprintf(stderr, "Sparing Table %d at location %08X (Len %d)\r\n",
                    i, location, md->EachSize);
#endif
        }


    } else if (!strncasecmp("*UDF Metadata Partition",
                            (char *)md->IdentifierStr, 23)) {

        md->MainFileLocation   = GETN4(40);
        md->MirrorFileLocation = GETN4(44);
        md->BitmapFileLocation = GETN4(48);
        md->AllocationUnitSize = GETN4(52);
        md->AlignmentUnitSize  = GETN2(56);
        md->Flags              = GETN1(58);
        // 59-64 Reserved
#ifdef DEBUG
        fprintf(stderr, " Metadata Partition MainLoc %08X, MirrorLoc %08X, BitmapLoc %08X, AllocSize %08X, AlignSize %04X, Flags %01X.\r\n",
                md->MainFileLocation,md->MirrorFileLocation,md->BitmapFileLocation,
                md->AllocationUnitSize,md->AlignmentUnitSize,md->Flags);
#endif

    } else {
#ifdef DEBUG
        fprintf(stderr, "Unhandled partition identifier.\r\n");
#endif
    }

    return 0;
}

/**
 * Reads the volume descriptor and checks the parameters.  Returns 0 on OK, 1
 * on error.
 */
static int UDFLogVolume( dvd_reader_t *device, uint8_t *data)
{
    uint32_t lbsize, MT_L, N_PM;
    int i, index;
    uint8_t PM_type, PM_len;
    uint16_t VolSeqNum, PartNum;

    Unicodedecode(&data[84], 128, device->partition.VolumeDesc);
    lbsize = GETN4(212);  /* should be 2048 */
    MT_L = GETN4(264);    /* should be 6 */
    N_PM = GETN4(268);    /* should be 1 */
#ifdef DEBUG
    fprintf(stderr, "LogVolume %d:%d:%d\r\n", lbsize, MT_L, N_PM);
#endif

    index = 440;
    for (i = 0; i < N_PM; i++) {
        PM_type = data[index];
        PM_len  = data[index+1];

        switch(PM_type) {
        case 1: // Type 1.
            //PM_len == 6
            VolSeqNum = GETN2(index+2);
            PartNum = GETN2(index+4);
#ifdef DEBUG
            fprintf(stderr, "Volume %d type 01 (len %d) Seq %04X Part %04X\r\n",
                    i, PM_len, VolSeqNum, PartNum);
#endif
            break;
        case 2: // Type 2.
            //PM_len == 64;
#ifdef DEBUG
            fprintf(stderr, "Volume %d type 02:\r\n", i);
#endif
            UDFMapMetadataPartition(&data[index], &device->meta_partition);
            break;
        default:
#ifdef DEBUG
            fprintf(stderr, "Volume %d type unknown %02X\r\n", i, PM_type);
#endif
            break;
        }
        index += PM_len;
    }

    if (lbsize != DVD_VIDEO_LB_LEN) return 1;
    return 0;
}

static int UDFFileEntry( uint8_t *data, uint8_t *FileType,
                         struct Partition *partition, udf_file_t *fad )
{
    uint16_t flags;
    uint32_t L_EA, L_AD;
    unsigned int p;

    UDFICB( &data[ 16 ], FileType, &flags );

    /* Init ad for an empty file (i.e. there isn't a AD, L_AD == 0 ) */
    fad->Length = GETN8( 56 ); /* Really 8 bytes at 56 */
    fad->num_AD = 0;
    fad->flags = flags;

    L_EA = GETN4( 168 );
    L_AD = GETN4( 172 );

    if (176 + L_EA + L_AD > DVD_VIDEO_LB_LEN)
        return 0;

    p = 176 + L_EA;

    if (ICB_DATA_IN_AD_SPACE(flags)) {
        // This means that the files DATA is actually at "AD" (which is at
        // data + L_EA) for length of L_AD.
        fad->Length = L_AD; // According to ECMA-167 14.6.8
        fad->content_offset = p;

#ifdef DEBUG
        fprintf(stderr, "ICBTAG type 3. Actual file contents for %d bytes:\r\n", L_AD);
#endif
        return 0;
    }

    while( p < 176 + L_EA + L_AD ) {
        struct AD *ad;

        if (fad->num_AD >= UDF_MAX_AD_CHAINS) return 0;

        ad =  &fad->AD_chain[fad->num_AD];
        ad->Partition = partition->Number;
        ad->Flags = 0;
        fad->num_AD++;

        switch( flags & 0x0007 ) {
        case 0:
            UDFShortAD( &data[ p ], ad, partition );
            p += 8;
            break;
        case 1:
            UDFLongAD( &data[ p ], ad );
            p += 16;
            break;
        case 2:
            UDFExtAD( &data[ p ], ad );
            p += 20;
            break;
            // type 3 handled above.
        default:
            p += L_AD;
            break;
        }
    }
    return 0;
}

/* TagID 266
 * The differences in ExtFile are indicated below:
 * struct extfile_entry {
 *         struct desc_tag         tag;
 *         struct icb_tag          icbtag;
 *         uint32_t                uid;
 *         uint32_t                gid;
 *         uint32_t                perm;
 *         uint16_t                link_cnt;
 *         uint8_t                 rec_format;
 *         uint8_t                 rec_disp_attr;
 *         uint32_t                rec_len;
 *         uint64_t                inf_len;
 *         uint64_t                obj_size;        // NEW
 *         uint64_t                logblks_rec;
 *         struct timestamp        atime;
 *         struct timestamp        mtime;
 *         struct timestamp        ctime;           // NEW
 *         struct timestamp        attrtime;
 *         uint32_t                ckpoint;
 *         uint32_t                reserved1;       // NEW
 *         struct long_ad          ex_attr_icb;
 *         struct long_ad          streamdir_icb;   // NEW
 *         struct regid            imp_id;
 *         uint64_t                unique_id;
 *         uint32_t                l_ea;
 *         uint32_t                l_ad;
 *         uint8_t                 data[1];
 * } __packed;
 *
 * So old "l_ea" is now +216
 *
 */
static int UDFExtFileEntry( uint8_t *data, uint8_t *FileType,
                            struct Partition *partition, udf_file_t *fad )
{
    uint16_t flags;
    uint32_t L_EA, L_AD;
    unsigned int p;

    UDFICB( &data[ 16 ], FileType, &flags );
    fad->flags = flags;

#ifdef DEBUG
    fprintf(stderr, "rec_len %d, inf_len %d, obj_size %d, logblks_rec %d, l_ea %d, l_ad %d, FLAGS %04X (type %d)\r\n",
            GETN4(52), (int)GETN8(56), (int)GETN8(64), (int)GETN8(72), GETN4(208),GETN4(212), flags, flags&7);
#endif

    /* Bits 0-2: The value 0 shall mean that Short Allocation
     * Descriptors (4/14.14.1) are used. The value 1 shall mean that
     * Long Allocation Descriptors (4/14.14.2) are used. The value 2
     * shall mean that Extended Allocation Descriptors (4/14.14.3) are
     * used. The value 3 shall mean that the file shall be treated as
     * though it had exactly one allocation descriptor describing an
     * extent which starts with the first byte of the Allocation
     * Descriptors field and has a length, in bytes, recorded in the
     * Length of Allocation Descriptors field.
     */

    /* Init ad for an empty file (i.e. there isn't a AD, L_AD == 0 ) */
    fad->Length = GETN8(56); // 64-bit.
    fad->num_AD = 0;

    L_EA = GETN4( 208);
    L_AD = GETN4( 212);
    p = 216 + L_EA;


    if (ICB_DATA_IN_AD_SPACE(flags)) {
        // This means that the files DATA is actually at "AD" (which is at
        // data + L_EA) for length of L_AD.
        //fad->Length = L_AD; // According to ECMA-167 14.6.8
        fad->content_offset = p;

#ifdef DEBUG
        fprintf(stderr, "ICBTAG type 3. Actual file contents for %d bytes:\r\n", L_AD);
#endif
        return 0;
    }



    while( p < 216 + L_EA + L_AD ) {
        struct AD *ad;

        if (fad->num_AD >= UDF_MAX_AD_CHAINS) return 0;

        ad =  &fad->AD_chain[fad->num_AD];
        ad->Partition = partition->Number;
        ad->Flags = 0;
        fad->num_AD++;

        switch( flags & 0x0007 ) {
        case 0:
            UDFShortAD( &data[ p ], ad, partition );
            p += 8;
            break;
        case 1:
            UDFLongAD( &data[ p ], ad );
            p += 16;
            break;
        case 2:
            UDFExtAD( &data[ p ], ad );
            p += 20;
            break;
            // type 3 handled above
        default:
            p += L_AD;
            break;
        }
#ifdef DEBUG
        fprintf(stderr, "libdvdread: read AD chain %d (start %d len %d blocks %d)\r\n",
                fad->num_AD-1,
                fad->AD_chain[fad->num_AD-1].Location,
                fad->AD_chain[fad->num_AD-1].Length,
                fad->AD_chain[fad->num_AD-1].Length/2048);
#endif

    }
    return 0;
}

static int UDFFileIdentifier( uint8_t *data, uint8_t *FileCharacteristics,
                              char *FileName, struct AD *FileICB )
{
    uint8_t L_FI;
    uint16_t L_IU;

    *FileCharacteristics = GETN1(18);
    L_FI = GETN1(19);
    UDFLongAD(&data[20], FileICB);
    L_IU = GETN2(36);
    if (L_FI) {
        if (!Unicodedecode(&data[38 + L_IU], L_FI, FileName)) FileName[0] = 0;
    } else FileName[0] = '\0';
    return 4 * ((38 + L_FI + L_IU + 3) / 4);
}

/**
 * Maps ICB to FileAD
 * ICB: Location of ICB of directory to scan
 * FileType: Type of the file
 * File: Location of file the ICB is pointing to
 * return 1 on success, 0 on error;
 */
static int UDFMapICB( dvd_reader_t *device, struct AD ICB, uint8_t *FileType,
                      struct Partition *partition, udf_file_t *File )
{
    uint8_t LogBlock_base[DVD_VIDEO_LB_LEN + 2048];
    uint8_t *LogBlock = (uint8_t *)(((uintptr_t)LogBlock_base & ~((uintptr_t)2047)) + 2048);
    uint32_t lbnum;
    uint16_t TagID;

    //lbnum = partition->Start + ICB.Location + partition->Metadata_Main;
    //lbnum = partition->Start + ICB.Location;
    //lbnum = ICB.Location;

    lbnum = partition->fsd_location + ICB.Location;
    memset(File, 0, sizeof(*File));

#ifdef DEBUG
    fprintf(stderr, "MapICB starting at %d,%d -> %d (Metadata Mainfile num_AD %d)\r\n",
            partition->fsd_location, ICB.Location, lbnum,
            partition->Metadata_Mainfile.num_AD);
#endif


    do {
        if( DVDReadLBUDFCached( device, lbnum++, 1, LogBlock, 0 ) <= 0 )
            TagID = 0;
        else
            UDFDescriptor( LogBlock, &TagID );

        if( TagID == 261 ) {
            UDFFileEntry( LogBlock, FileType, partition, File );
#ifdef DEBUG
            fprintf(stderr, "UDFMapICB TagID %d File with filetype %d\r\n",
                    TagID, *FileType);
#endif
            return 1;
        };
        /* ExtendedFileInfo */
        if( TagID == 266 ) {
            UDFExtFileEntry( LogBlock, FileType, partition, File );
#ifdef DEBUG
            fprintf(stderr, "UDFMapICB TagID %d ExtFile with filetype %d\r\n",
                    TagID, *FileType);
#endif

#if 0
            if( *FileType == 4 )
                File->Partition_Start = device->partition.fsd_location;
            else
                File->Partition_Start = device->partition.Start;
#endif

            //File->Partition_Start = device->partition.fsd_location;
            File->info_location   = 0;

            if (ICB_DATA_IN_AD_SPACE(File->flags)) {
                // The FID data is in the AD part of the ExtFileInfo block.
                // The File.Partition_Start will be set to FSD, so we make the
                // File location to RootICB.
                File->info_location = lbnum-1;
#ifdef DEBUG
                fprintf(stderr, "Because of type 3, info_location is here %d\r\n",
                        lbnum-1);
#endif
            }

            return 1;
        }
    } while( ( lbnum <= partition->Start + ICB.Location + ( ICB.Length - 1 )
               / DVD_VIDEO_LB_LEN ) && ( TagID != 261 )  && (TagID != 266));

    return 0;
}


/**
 * Low-level function used by DVDReadDir to simulate readdir().
 * Returns ONE directory entry at a time, or NULL when finished/failed.
 *
 */
int UDFScanDirX( dvd_reader_t *device,
                 dvd_dir_t *dirp )
{
    char filename[ MAX_UDF_FILE_NAME_LEN ];
    uint8_t directory_base[ 2 * DVD_VIDEO_LB_LEN + 2048];
    uint8_t *directory = (uint8_t *)(((uintptr_t)directory_base & ~((uintptr_t)2047)) + 2048);
    uint32_t lbnum;
    uint16_t TagID;
    uint8_t filechar;
    unsigned int p;
    struct AD FileICB;
    udf_file_t File;
    uint8_t filetype;
    uint32_t offset = 0;

    memset(&File, 0, sizeof(File));

    /* Scan dir for ICB of file */
    lbnum = dirp->dir_current;

    offset = 0;


    // File content can be stored IN the FileInfo block, check this case:
    //if (!dirp->current_p && !lbnum && dirp->dir_file->info_location) {
    if (dirp->dir_file->info_location) {
#ifdef DEBUG
        fprintf(stderr, "ScanDirX as content is stored in INFO, reading FileInfo block at %d\r\n",
                dirp->dir_file->info_location);
#endif

        if( DVDReadLBUDFCached( device,
                          dirp->dir_file->info_location,
                          1, directory, 0 ) <= 0 ) {
            return 0;
        }
        UDFDescriptor( &directory[ 0 ], &TagID );
        if ((TagID == 266)) {
            UDFExtFileEntry( directory, &filetype, &device->partition, &File );
            if ((filetype == 4) && ICB_DATA_IN_AD_SPACE(File.flags)) {
                offset = File.content_offset;
#ifdef DEBUG
                fprintf(stderr, "Type 3 detected, offset now %d (dirlen %d)\r\n",
                        offset, dirp->dir_length);
#endif
            }
        }
    } else {
        // Data in file content, follow AD

#ifdef DEBUG
        fprintf(stderr, "ScanDirX starting at %d (looking for 257s, fsd %d) translatedto %d\r\n",
                lbnum,
                device->partition.fsd_location,
                UDFFileBlockDir(device, dirp->dir_file, lbnum)
                );
#endif

        // We read 2 dirs here, since FIDs (40b) dont fit nicely in 2048b blocks
        // which enables us to get the last FID in, before reading next.
        if( DVDReadLBUDFCached( device,
                          UDFFileBlockDir(device, dirp->dir_file, lbnum),
                          1, directory, 0 ) <= 0 ) {
            return 0;
        }

        if( DVDReadLBUDFCached( device,
                          UDFFileBlockDir(device, dirp->dir_file, lbnum+1),
                          1, &directory[DVD_VIDEO_LB_LEN], 0 ) <= 0 ) {
            return 0;
        }
    }


    /* I have cut out the caching part of the original UDFScanDir() function
     * one day we should probably bring it back. */



    p = dirp->current_p;

#ifdef DEBUG
    fprintf(stderr, "scanning starting from p %d, offset %d for length %d\r\n",
            p, offset, dirp->dir_length);
#endif

    while( p < dirp->dir_length ) { // p + offset < len + offset

        // Go to next block? This should use filelen for type = 3 ?
        if( (p + offset) > DVD_VIDEO_LB_LEN ) {
            ++lbnum;
            p -= DVD_VIDEO_LB_LEN;

            /*Dir.Length -= DVD_VIDEO_LB_LEN;*/
            if (dirp->dir_length >= DVD_VIDEO_LB_LEN)
                dirp->dir_length -= DVD_VIDEO_LB_LEN;
            else
                dirp->dir_length = 0;

#ifdef DEBUG
            fprintf(stderr, "Out of data, reading next block\r\n");
#endif

            //dirp->current_p = 0;
            //p = 0;

            if( DVDReadLBUDFCached( device,
                              UDFFileBlockDir(device, dirp->dir_file, lbnum),
                              1, directory, 0 ) <= 0 ) {
                return 0;
            }
            if( DVDReadLBUDFCached( device,
                              UDFFileBlockDir(device, dirp->dir_file, lbnum+1),
                              1, &directory[DVD_VIDEO_LB_LEN], 0 ) <= 0 ) {
                return 0;
            }

        }


        // Process block for FIDs
        UDFDescriptor( &directory[ p + offset], &TagID );

#ifdef DEBUG
        fprintf(stderr, "TagID %d: p %d\n", TagID, p);
#endif

        if( TagID == 257 ) { // FID

            p += UDFFileIdentifier( &directory[ p + offset], &filechar,
                                    filename, &FileICB );

#ifdef DEBUG
            fprintf(stderr, "Read entryname '%s', FileChar %02X\r\n", filename, filechar);
#endif

            if ((filechar & 1)) continue; // Existence, don't show, like dot-dirs
            //if ((filechar & 2)) ; // Is Directory
            if ((filechar & 4)) continue; // Deleted, don't show
            if ((filechar & 8)) continue; // Parent Directory


            dirp->current_p = p;
            dirp->dir_current = lbnum;

            if (!*filename)  /* No filename, simulate "." dirname */
                strcpy((char *)dirp->entry.d_name, ".");
            else {
                strncpy((char *)dirp->entry.d_name, filename,
                        sizeof(dirp->entry.d_name)-1);
                dirp->entry.d_name[ sizeof(dirp->entry.d_name) - 1 ] = 0;
            }

            /* Look up the Filedata */
            if( !UDFMapICB( device, FileICB, &filetype, &device->partition,
                            &(dirp->entry.dir_file)))
                return 0;

            if (filetype == 4)
                dirp->entry.d_type = DVD_DT_DIR;
            else
                dirp->entry.d_type = DVD_DT_REG;
            /* Add more types? */

            dirp->entry.d_filesize = dirp->entry.dir_file.Length;

#ifdef DEBUG
            fprintf(stderr, "Returning 1 valid dirp: location %d. infoloc %d\r\n",
                    dirp->entry.dir_file.AD_chain[0].Location, dirp->entry.dir_file.info_location);
#endif
            return 1;

        } else {
#ifdef DEBUG
            fprintf(stderr, "failed - not 257, whats next: \r\n");

            if (TagID == 266) {
                UDFExtFileEntry( directory, &filetype, &device->partition, &File );

                fprintf(stderr, "TagID %d with filetype %d\r\n",
                        TagID, filetype);
            }
#endif

            /* Not TagID 257 */
            return 0;
        }
    }
    /* End of DIR contents */
#ifdef DEBUG
    fprintf(stderr, "UDFScanDirX: Reach EOF of directory\r\n");
#endif
    dirp->current_p = 0;
    return 0;
}

static int UDFGetAVDP( dvd_reader_t *device,
                       struct avdp_t *avdp)
{
    uint8_t Anchor_base[ DVD_VIDEO_LB_LEN + 2048 ];
    uint8_t *Anchor = (uint8_t *)(((uintptr_t)Anchor_base & ~((uintptr_t)2047)) + 2048);
    uint32_t lbnum, MVDS_location, MVDS_length;
    uint16_t TagID;
    uint32_t lastsector;
    int terminate;
    struct avdp_t;

    /* Find Anchor */
    lastsector = 0;
    lbnum = 256;   /* Try #1, prime anchor */
    terminate = 0;

    for(;;) {
        if( DVDReadLBUDF( device, lbnum, 1, Anchor, 0 ) > 0 ) {
            UDFDescriptor( Anchor, &TagID );
        } else {
            TagID = 0;
        }
        if (TagID != 2) {
            /* Not an anchor */
            if( terminate ) return 0; /* Final try failed */

            if( lastsector ) {
                /* We already found the last sector.  Try #3, alternative
                 * backup anchor.  If that fails, don't try again.
                 */
                lbnum = lastsector;
                terminate = 1;
            } else {
                /* TODO: Find last sector of the disc (this is optional). */
                if( lastsector )
                    /* Try #2, backup anchor */
                    lbnum = lastsector - 256;
                else
                    /* Unable to find last sector */
                    return 0;
            }
        } else
            /* It's an anchor! We can leave */
            break;
    }
    /* Main volume descriptor */
    UDFExtentAD( &Anchor[ 16 ], &MVDS_length, &MVDS_location );
    avdp->mvds.location = MVDS_location;
    avdp->mvds.length = MVDS_length;

    /* Backup volume descriptor */
    UDFExtentAD( &Anchor[ 24 ], &MVDS_length, &MVDS_location );
    avdp->rvds.location = MVDS_location;
    avdp->rvds.length = MVDS_length;

    return 1;
}

/**
 * Looks for partition on the disc.  Returns 1 if partition found, 0 on error.
 *   partnum: Number of the partition, starting at 0.
 *   part: structure to fill with the partition information
 */
static int UDFFindPartition( dvd_reader_t *device, int partnum,
                             struct Partition *part )
{
    uint8_t LogBlock_base[ DVD_VIDEO_LB_LEN + 2048 ];
    uint8_t *LogBlock = (uint8_t *)(((uintptr_t)LogBlock_base & ~((uintptr_t)2047)) + 2048);
    uint32_t lbnum, MVDS_location, MVDS_length;
    uint16_t TagID;
    int i, volvalid;
    struct avdp_t avdp;

    if(!UDFGetAVDP(device, &avdp))
        return 0;

    /* Main volume descriptor */
    MVDS_location = avdp.mvds.location;
    MVDS_length = avdp.mvds.length;

    memset(&device->partition, 0, sizeof(device->partition));
    memset(&device->meta_partition, 0, sizeof(device->meta_partition));

    volvalid = 0;

    i = 1;
    do {
        /* Find Volume Descriptor */
        lbnum = MVDS_location;
        do {

            if( DVDReadLBUDF( device, lbnum++, 1, LogBlock, 0 ) <= 0 )
                TagID = 0;
            else
                UDFDescriptor( LogBlock, &TagID );

            if( ( TagID == 5 ) && ( !part->valid ) ) {
                /* Partition Descriptor */
                UDFPartition( device, LogBlock );
                part->valid = ( partnum == part->Number );
            } else if( ( TagID == 6 ) && ( !volvalid ) ) {
                /* Logical Volume Descriptor */
                if( UDFLogVolume( device, LogBlock ) ) {
                    /* TODO: sector size wrong! */
                } else
                    volvalid = 1;
            }

        } while( ( lbnum <= MVDS_location + ( MVDS_length - 1 )
                   / DVD_VIDEO_LB_LEN ) && ( TagID != 8 )
                 && ( ( !part->valid ) || ( !volvalid ) ) );

        if( ( !part->valid) || ( !volvalid ) ) {
            /* Backup volume descriptor */
            MVDS_location = avdp.mvds.location;
            MVDS_length = avdp.mvds.length;
        }
    } while( i-- && ( ( !part->valid ) || ( !volvalid ) ) );

#ifdef DEBUG
    fprintf(stderr, "returning Start %d\r\n", part->Start);
#endif
    /* We only care for the partition, not the volume */
    return part->valid;
}


/*
 * Release a UDF file descriptor
 */
void UDFFreeFile(dvd_reader_t *device, udf_file_t *File)
{
    free(File);
}


/*
 * The API users will refer to block 0 as start of file and going up.
 * We need to convert that, to actual disk block; partition_start +
 * file_start + offset, but keep in mind that file_start is chained,
 * and not contiguous. There are gap blocks between chains.
 *
 * We return "0" as error, since a File can not start at physical block 0
 *
 * It is perhaps unfortunate that Location is in blocks, but Length is
 * in bytes..? Can bytes be uneven blocksize in the middle of a chain?
 *
 */
uint32_t UDFFileBlockRaw(dvd_reader_t *device, udf_file_t *File, uint32_t file_block)
{
    uint32_t result, i, offset;

    if (!File) return 0;

    /* Look through the chain to see where this block would belong. */
    for (i = 0, offset = 0; i < File->num_AD; i++) {
        /* Is "file_block" inside this chain? Then use this chain. */
        if (file_block < (offset +
                          (File->AD_chain[i].Length /  DVD_VIDEO_LB_LEN))) break;
        // offset += (File->AD_chain[i].Length /  DVD_VIDEO_LB_LEN);
    }

    /* If it is not defined in the AD chains, we should return an error, but
     * in the interest of being backward (in)compatible with the original
     * libdvdread, we revert back to the traditional view that UDF file are
     * one contiguous long chain, in case some API software out there relies on
     * this incorrect behavior.
     */
    if (i >= File->num_AD) i = 0;

#ifdef DEBUG
    fprintf(stderr, " BlockPos: mapping +%d into file, found in chain %d starting at %d (for len %d) (offset %d, with FSD/Start at %d) resulting at: %d. Flags are %02X\r\n",
            file_block, i, File->AD_chain[i].Location,
            File->AD_chain[i].Length,
            offset,
            result,
            result + File->AD_chain[i].Location + file_block - offset,
            File->flags);
#endif

    result = File->AD_chain[i].Location + file_block - offset;

    return result;
}

// Positions for Directories, based off FSD
uint32_t UDFFileBlockDir(dvd_reader_t *device, udf_file_t *File, uint32_t file_block)
{
    return UDFFileBlockRaw(device, File, file_block) + device->partition.fsd_location;

}

// Positions for Files, based off Partition.Start
uint32_t UDFFileBlockFile(dvd_reader_t *device, udf_file_t *File, uint32_t file_block)
{
    return UDFFileBlockRaw(device, File, file_block) + device->partition.Start;

}



udf_file_t *UDFFindFile( dvd_reader_t *device, char *filename,
                         uint64_t *filesize )
{
    udf_file_t *finder = NULL;
    udf_file_t *result;
    udf_file_t subdir;
    char tokenline[ MAX_UDF_FILE_NAME_LEN ];
    char *token;
    dvd_dir_t dirp;

#ifdef DEBUG
    fprintf(stderr, "UDFFindFile('%s')\r\n", filename);
#endif

    tokenline[0] = '\0';
    strncat(tokenline, filename, MAX_UDF_FILE_NAME_LEN - 1);

    // If it is Root, we have it already
    finder = &device->RootDirectory;

    // Traverse tree..
    token = strtok(tokenline, "/");

    while( token != NULL ) {
        int found = 0;

#ifdef DEBUG
        fprintf(stderr, "FindFile() calling ScanDir('%s')\r\n", token);
#endif

        dirp.dir_file = finder; // Start at root
        dirp.dir_location = 0;// untranslated +0. ScanDir translated
        dirp.dir_current  = 0;
        dirp.current_p    = 0;
        dirp.dir_length   = finder->Length;

        while (UDFScanDirX(device, &dirp)) {
            if( !strcasecmp( token, (char *)dirp.entry.d_name ) ) {
                found = 1;
                break;
            }
        }

        if (!found) {
#ifdef DEBUG
            fprintf(stderr, "FindFile not found, returning failure\r\n");
#endif
            return NULL;
        }

#ifdef DEBUG
        fprintf(stderr, "FindFile() located ('%s')\r\n", token);
#endif

        // Keep a copy of this subdir
        // copy from dirp, since it will be overwritten if we descend...
        memcpy(&subdir, &dirp.entry.dir_file, sizeof(subdir));
        finder = &subdir;

        token = strtok( NULL, "/" );
    } // while slashes in path


    dirp.dir_length   = finder->Length;
    if (filesize)
        *filesize = finder->Length;

    // Allocate a UDF_FILE node for the API user
    result = (udf_file_t *) malloc(sizeof(*result));
    if (!result) return NULL;

    // Copy over the information
    memcpy(result, finder, sizeof(*result));

#ifdef DEBUG
    fprintf(stderr, "Returning result OK with partition.Start at %d (length %d), num AD chains %d, and first AD chain.Location %d with AD chain.partition %d\r\n",
            device->partition.Start,
            (int)finder->Length,
            finder->num_AD,
            finder->AD_chain[0].Location,
            finder->AD_chain[0].Partition);
#endif

#if 0
    if( filetype == 4 )
        //result->Partition_Start = partition.fsd_location ;
        result->Partition_Start = device->partition.fsd_location ;
    else
        result->Partition_Start = device->partition.Start;
#endif

#ifdef DEBUGX
    fprintf(stderr, "fad->Partition_Start then set to %d AD_chain %d\r\n\r\n\r\n\r\n",
            result->Partition_Start,
            result->AD_chain[0].Location);
#endif
    return result;
}




int UDFOpen( dvd_reader_t *device )
{
    uint8_t LogBlock_base[ DVD_VIDEO_LB_LEN + 2048 ];
    uint8_t *LogBlock = (uint8_t *)(((uintptr_t)LogBlock_base & ~((uintptr_t)2047)) + 2048);
    uint32_t lbnum;
    uint16_t TagID;
    struct AD RootICB;
    udf_file_t File;
    uint8_t filetype;

#ifdef DEBUG
    dvd_dir_t dirp;

    fprintf(stderr, "UDFOpen\r\n");
#endif
    /* Find partition, 0 is the standard location for DVD Video.*/
    if( !UDFFindPartition( device, 0, &device->partition ) ) return 0;

#ifdef DEBUG
    fprintf(stderr,"Found partition at %d length %d\r\n", device->partition.Start,
            device->partition.Length);
#endif

    /* Find root dir ICB */
    lbnum = device->partition.Start + device->meta_partition.MainFileLocation;
#ifdef DEBUG
    fprintf(stderr, "Starting scan from %d (metadata adjusted)\r\n", lbnum);
#endif

    do {
        if( DVDReadLBUDFCached( device, lbnum++, 1, LogBlock, 0 ) <= 0 )
            TagID = 0;
        else
            UDFDescriptor( LogBlock, &TagID );

#ifdef DEBUG
        fprintf(stderr, "   loop1 TagID %d\r\n",
                TagID);
#endif

        if (TagID == 264) { // TAGID_SPACE_BITMAP
            struct space_bitmap bitmap;
            UDFSpaceBitmap(LogBlock, &bitmap);
#ifdef DEBUG
            fprintf(stderr, "Found 264: SPACE_BITMAP: bits %u bytes %u. %08X %08X\r\n",
                    bitmap.NumberOfBits, bitmap.NumberOfBytes,
                    bitmap.NumberOfBits, bitmap.NumberOfBytes);
#endif
        }


        if( TagID == 266 ) {

            UDFExtFileEntry( LogBlock, &filetype, &device->partition, &File );

#ifdef DEBUG
            fprintf(stderr, "TagID %d with filetype %d\r\n",
                    TagID, filetype);
#endif

            if (filetype == 248) {
#ifdef DEBUG
                fprintf(stderr, "Filetype 248 VAT found\r\n");
#endif
            }

            if (filetype == 250) { // UDF_ICB_FILETYPE_META_MAIN
#ifdef DEBUG
                fprintf(stderr, "   Metadata Main at location %d (+partition.start %d)\r\n",
                        File.AD_chain[0].Location,
                        device->partition.Start + File.AD_chain[0].Location
                        );
#endif
                lbnum = device->partition.Start + File.AD_chain[0].Location;
#ifdef DEBUG
                fprintf(stderr, "Now Scanning from %d\r\n", lbnum);
#endif
                //device->partition.Metadata_Main = lbnum-1;
                // Save the Metadata file so we can reference it
                memcpy(&device->partition.Metadata_Mainfile, &File, sizeof(File));
                continue;
            }

            if (filetype == 251) { // UDF_ICB_FILETYPE_META_MIRROR
#ifdef DEBUG
                fprintf(stderr, "   Metadata Mirror at location %d (+partition.start %d)\r\n",
                        File.AD_chain[0].Location,
                        device->partition.Start + File.AD_chain[0].Location
                        );
#endif
                //partition.Metadata_Mirror = lbnum-1;
                // Save the Metadata file so we can reference it
                memcpy(&device->partition.Metadata_Mirrorfile,&File, sizeof(File));
                continue;
            }

            if (filetype == 252) { // UDF_ICB_FILETYPE_META_BITMAP
#ifdef DEBUG
                fprintf(stderr, "   Metadata Bitmap at location %d (+partition.start %d)\r\n",
                        File.AD_chain[0].Location,
                        device->partition.Start + File.AD_chain[0].Location
                        );
#endif
                continue;
            }

        } // 266 ExtFile


        /* File Set Descriptor */
        if( TagID == 256 )  /* File Set Descriptor */
            UDFGetRootICB(LogBlock, &RootICB);

    } while( ( lbnum < device->partition.Start + device->partition.Length )
             && ( TagID != 8 ) && ( TagID != 256 ) );

#ifdef DEBUG
    fprintf(stderr, "  stopped first loop at %d TagID %d\r\n", lbnum-1, TagID);
#endif

    /* Sanity checks. */
    if( TagID != 256 )
        return 0;
    /* This following test will fail under UDF2.50 images, as it is no longer
     * valid */
    /*if( RootICB.Partition != 0 )
      return NULL;*/

    device->partition.fsd_location = lbnum-1; // Since we lbnum++; upstairs.

    //RootICB.Location += fsd_location;

    /* Find root dir */
    if( !UDFMapICB( device, RootICB, &filetype, &device->partition,
                    &device->RootDirectory ) ) {
#ifdef DEBUG
        fprintf(stderr, "Failed to map RootICB\r\n");
#endif
        return 0;
    }

#ifdef DEBUG
    fprintf(stderr, "Part.Start %d FSD loc %d RootICB %d (len %d) File has loc %d. Translated FileLoc %d (num_AD %d)\r\n",
            device->partition.Start,
            device->partition.fsd_location,
            RootICB.Location,
            RootICB.Length,
            device->RootDirectory.AD_chain[0].Location,
            UDFFileBlockDir(device, &device->RootDirectory, 0),
            device->RootDirectory.num_AD
            );
#endif

    if( filetype != 4 )
        return 0;  /* Root dir should be dir */

#if 0
    if (ICB_DATA_IN_AD_SPACE(device->RootDirectory.flags)) {
        // The FID data is in the AD part of the ExtFileInfo block.
        // The File.Partition_Start will be set to FSD, so we make the
        // File location to RootICB.
        device->RootDirectory.AD_chain[0].Location = RootICB.Location;
        device->RootDirectory.num_AD = 1;
#ifdef DEBUG
        fprintf(stderr, "Because of type 3, forcing location of Root to %d\r\n",
                RootICB.Location);
#endif
    }
#endif

    /* Sanity check. */
    if( device->RootDirectory.AD_chain[0].Partition != 0 ) {
#ifdef DEBUG
        fprintf(stderr, "ADChain partition is not 0 (%d).\r\n",
                device->RootDirectory.AD_chain[0].Partition);
#endif
        return 0;
    }


    //device->RootDirectory.Partition_Start = device->partition.fsd_location;
#ifdef DEBUG
    if (0) {
        struct UDF_FILE subdir;
        int first;

        fprintf(stderr, "\r\n\r\n\r\nTESTING OPENDIR ON /\r\n");

        dirp.dir_location = 0;// untranslated +0. ScanDir translated
        dirp.dir_current  = 0;
        dirp.dir_length   = device->RootDirectory.Length;
        dirp.dir_file     = &device->RootDirectory;
        dirp.current_p = 0;

        first = 2;

        while(UDFScanDirX(device, &dirp)) {
            fprintf(stderr, "  '%s'\r\n", dirp.entry.d_name);

            if (first) {
                first--;
                if (!first) {
                    memcpy(&subdir, &dirp.entry.dir_file, sizeof(subdir));
                    fprintf(stderr, "Copying first SUBDIR: Location %d num_AD %d. info_loc %d\r\n",
                            subdir.AD_chain[0].Location, subdir.num_AD,
                            subdir.info_location);
                }
            }
        }

        fprintf(stderr, "\r\n\r\n\r\nTESTING OPENDIR ON FIRST SUBDIR\r\n");

        dirp.dir_location = 0;// untranslated +0. ScanDir translated
        dirp.dir_current  = 0;
        dirp.dir_length   = subdir.Length;
        dirp.dir_file     = &subdir;
        dirp.current_p = 0;

        while(UDFScanDirX(device, &dirp)) {
            fprintf(stderr, "  '%s'\r\n", dirp.entry.d_name);

        }

    }
#endif


    return 1;
}



/**
 * Gets a Descriptor .
 * Returns 1 if descriptor found, 0 on error.
 * id, tagid of descriptor
 * bufsize, size of BlockBuf (must be >= DVD_VIDEO_LB_LEN).
 */
static int UDFGetDescriptor( dvd_reader_t *device, int id,
                             uint8_t *descriptor, int bufsize)
{
    uint32_t lbnum, MVDS_location, MVDS_length;
    struct avdp_t avdp;
    uint16_t TagID;
    uint32_t lastsector;
    int i, terminate;
    int desc_found = 0;
    /* Find Anchor */
    lastsector = 0;
    lbnum = 256;   /* Try #1, prime anchor */
    terminate = 0;
    if(bufsize < DVD_VIDEO_LB_LEN)
        return 0;

    if(!UDFGetAVDP(device, &avdp))
        return 0;

    /* Main volume descriptor */
    MVDS_location = avdp.mvds.location;
    MVDS_length = avdp.mvds.length;

    i = 1;
    do {
        /* Find  Descriptor */
        lbnum = MVDS_location;
        do {
            if( DVDReadLBUDF( device, lbnum++, 1, descriptor, 0 ) <= 0 )
                TagID = 0;
            else
                UDFDescriptor( descriptor, &TagID );
            if( (TagID == id) && ( !desc_found ) )
                /* Descriptor */
                desc_found = 1;
        } while( ( lbnum <= MVDS_location + ( MVDS_length - 1 )
                   / DVD_VIDEO_LB_LEN ) && ( TagID != 8 )
                 && ( !desc_found) );

        if( !desc_found ) {
            /* Backup volume descriptor */
            MVDS_location = avdp.rvds.location;
            MVDS_length = avdp.rvds.length;
        }
    } while( i-- && ( !desc_found )  );

    return desc_found;
}


static int UDFGetPVD(dvd_reader_t *device, struct pvd_t *pvd)
{
    uint8_t pvd_buf_base[DVD_VIDEO_LB_LEN + 2048];
    uint8_t *pvd_buf = (uint8_t *)(((uintptr_t)pvd_buf_base & ~((uintptr_t)2047)) + 2048);

    if(!UDFGetDescriptor( device, 1, pvd_buf, DVD_VIDEO_LB_LEN))
        return 0;

    memcpy(pvd->VolumeIdentifier, &pvd_buf[24], 32);
    memcpy(pvd->VolumeSetIdentifier, &pvd_buf[72], 128);
    return 1;
}

/**
 * Gets the Volume Identifier string, in 8bit unicode (latin-1)
 * volid, place to put the string
 * volid_size, size of the buffer volid points to
 * returns the size of buffer needed for all data
 */
int UDFGetVolumeIdentifier(dvd_reader_t *device, char *volid,
                           unsigned int volid_size)
{
    struct pvd_t pvd;
    unsigned int volid_len;

    /* get primary volume descriptor */
    if(!UDFGetPVD(device, &pvd))
        return 0;

    volid_len = pvd.VolumeIdentifier[31];
    if(volid_len > 31)
        /* this field is only 32 bytes something is wrong */
        volid_len = 31;
    if(volid_size > volid_len)
        volid_size = volid_len;
    Unicodedecode(pvd.VolumeIdentifier, volid_size, volid);

    return volid_len;
}

/**
 * Gets the Volume Set Identifier, as a 128-byte dstring (not decoded)
 * WARNING This is not a null terminated string
 * volsetid, place to put the data
 * volsetid_size, size of the buffer volsetid points to
 * the buffer should be >=128 bytes to store the whole volumesetidentifier
 * returns the size of the available volsetid information (128)
 * or 0 on error
 */
int UDFGetVolumeSetIdentifier(dvd_reader_t *device, uint8_t *volsetid,
                              unsigned int volsetid_size)
{
    struct pvd_t pvd;

    /* get primary volume descriptor */
    if(!UDFGetPVD(device, &pvd))
        return 0;


    if(volsetid_size > 128)
        volsetid_size = 128;

    memcpy(volsetid, pvd.VolumeSetIdentifier, volsetid_size);

    return 128;
}
