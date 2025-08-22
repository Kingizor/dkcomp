/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2025 Kingizor
 * dkcomp library */

#ifndef DK_INTERNAL
#define DK_INTERNAL

#include <stddef.h>
#define BUILD_DKCOMP
#include "dkcomp.h"

enum DK_ERROR {
    DK_SUCCESS,

    DK_ERROR_OOB_INPUT,
    DK_ERROR_OOB_OUTPUT_R,
    DK_ERROR_OOB_OUTPUT_W,

    DK_ERROR_ALLOC,

    DK_ERROR_NULL_INPUT,
    DK_ERROR_FILE_INPUT,
    DK_ERROR_FILE_OUTPUT,
    DK_ERROR_SEEK_INPUT,
    DK_ERROR_FREAD,
    DK_ERROR_FWRITE,

    DK_ERROR_OFFSET_BIG,
    DK_ERROR_OFFSET_NEG,
    DK_ERROR_OFFSET_DIFF,

    DK_ERROR_INPUT_SMALL,
    DK_ERROR_INPUT_LARGE,
    DK_ERROR_OUTPUT_SMALL,

    DK_ERROR_SIZE_WRONG,
    DK_ERROR_EARLY_EOF,

    DK_ERROR_BAD_FORMAT,
    DK_ERROR_GBA_DETECT,
    DK_ERROR_SIG_WRONG,

    DK_ERROR_COMP_NOT,
    DK_ERROR_DECOMP_NOT,

    DK_ERROR_SD_BAD_EXIT,
    DK_ERROR_LZ77_HIST,
    DK_ERROR_HUFF_WRONG,
    DK_ERROR_HUFF_LEAF,
    DK_ERROR_HUFF_DIST,
    DK_ERROR_HUFF_NO_LEAF,
    DK_ERROR_HUFF_OUTSIZE,
    DK_ERROR_HUFF_STACKS,
    DK_ERROR_HUFF_NODES,
    DK_ERROR_HUFF_NODELIM,
    DK_ERROR_HUFF_LEAFVAL,

    DK_ERROR_TABLE_RANGE,
    DK_ERROR_TABLE_VALUE,
    DK_ERROR_TABLE_ZERO,

    DK_ERROR_VERIFY_DEC,
    DK_ERROR_VERIFY_SIZE,
    DK_ERROR_VERIFY_DATA,

    DK_ERROR_INVALID,
    DK_ERROR_LIMIT
};

struct FILE_STREAM {
    unsigned char *data;
    size_t length;
    size_t limit;
    size_t pos;
    unsigned char bytepos; /* which byte in a word */
    unsigned char bitpos;  /* which bit  in a byte */
};

struct COMPRESSOR {
    struct FILE_STREAM in;
    struct FILE_STREAM out;
};

int          bd_compress (struct COMPRESSOR*);
int        bd_decompress (struct COMPRESSOR*);
int          sd_compress (struct COMPRESSOR*);
int        sd_decompress (struct COMPRESSOR*);
int    dkcchr_decompress (struct COMPRESSOR*);
int      dkcchr_compress (struct COMPRESSOR*);
int      dkcgbc_compress (struct COMPRESSOR*);
int    dkcgbc_decompress (struct COMPRESSOR*);
int         dkl_compress (struct COMPRESSOR*);
int       dkl_decompress (struct COMPRESSOR*);
int gbahuff60_decompress (struct COMPRESSOR*);
int   gbahuff60_compress (struct COMPRESSOR*);
int gbahuff50_decompress (struct COMPRESSOR*);
int   gbahuff50_compress (struct COMPRESSOR*);
int gbahuff20_decompress (struct COMPRESSOR*);
int   gbahuff20_compress (struct COMPRESSOR*);
int   gbalz77_decompress (struct COMPRESSOR*);
int     gbalz77_compress (struct COMPRESSOR*);
int    gbarle_decompress (struct COMPRESSOR*);
int      gbarle_compress (struct COMPRESSOR*);
int       gba_decompress (struct COMPRESSOR*);
int   gbprinter_compress (struct COMPRESSOR*);
int gbprinter_decompress (struct COMPRESSOR*);

const char *dk_get_error (int);

#endif
