/* SPDX-License-Identifier: MIT
 * Big Data Compression Library
 * Copyright (c) 2020 Kingizor */

#ifndef DK_INTERNAL
#define DK_INTERNAL

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

void dk_set_error (const char*);

#endif
