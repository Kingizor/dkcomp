/* SPDX-License-Identifier: MIT
 * Big Data Compression Library
 * Copyright (c) 2020 Kingizor */

#ifndef DK_INTERNAL
#define DK_INTERNAL

struct COMPRESSOR {
    unsigned char *input;
    unsigned char *output;
    int input_len;
    int inpos;  /* Output offset */
    int outpos; /* Input  offset */
    int half;   /* nibble position */
    int bitpos; /* bit position */
};

#define OUTPUT_LIMIT 0x10000

int       bd_compress (struct COMPRESSOR*);
int     bd_decompress (struct COMPRESSOR*);
int       sd_compress (struct COMPRESSOR*);
int     sd_decompress (struct COMPRESSOR*);
int dkcchr_decompress (struct COMPRESSOR*);
int dkcgbc_decompress (struct COMPRESSOR*);
int    dkl_decompress (struct COMPRESSOR*);

void dk_set_error (const char*);

#endif
