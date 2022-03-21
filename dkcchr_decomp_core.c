/* SPDX-License-Identifier: MIT
 * DKC CHR Compression Library
 * Copyright (c) 2020 Kingizor */

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "dk_internal.h"

jmp_buf error_handler;

/* Read a byte from input */
static int rb (struct COMPRESSOR *dk, unsigned short *addr) {
    if (*addr >= dk->input_len) {
        dk_set_error("Tried to read out of bounds. (input)");
        return -1;
    }
    unsigned char z = dk->input[*addr];
    *addr += 1;
    return z;
}
static int rw (struct COMPRESSOR *dk, unsigned short *addr) {
    int o1, o2;
    if ((o1 = rb(dk, addr)) < 0
    ||  (o2 = rb(dk, addr)) < 0)
        return -1;
    return o1 | (o2 << 8);
}

/* Read a byte from output */
static int rbo (struct COMPRESSOR *dk, unsigned short addr) {
    if (addr >= dk->outpos) {
        dk_set_error("Tried to read out of bounds. (output)");
        return -1;
    }
    return dk->output[addr];
}

/* Write a byte from output */
static int wb (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->outpos >= OUTPUT_LIMIT) {
        dk_set_error("Tried to write out of bounds.");
        return -1;
    }
    dk->output[dk->outpos++] = val;
    return 0;
}

int dkcchr_decompress (struct COMPRESSOR *dk) {

    int n;
    unsigned short addr = 0x80; /* data begins at 0x80 */

    while ((n = rb(dk, &addr)) > 0) {
        unsigned char jmp = n >> 6;
        if (jmp)
            n &= 0x3F;

        switch (jmp) {
            case 0: { /* Copy n bytes from input */
                while (n--)
                    if (wb(dk, rb(dk, &addr)))
                        return 1;
                break;
            }
            case 1: { /* Write a byte, n times */
                unsigned char c = rb(dk, &addr);
                while (n--)
                    if (wb(dk, c))
                        return 1;
                break;
            }
            case 2: { /* Copy n bytes from output */
                int v;
                if ((v = rw(dk, &addr)) < 0)
                    return 1;
                if (v >= dk->outpos) {
                    dk_set_error("Encountered an invalid output offset.");
                    return 1;
                }
                while (n--)
                    if (wb(dk, rbo(dk, v++)))
                        return 1;
                break;
            }
            case 3: { /* Copy a word from the input LUT (00-7F) */
                unsigned short v = n << 1;
                if (wb(dk, rb(dk, &v))
                ||  wb(dk, rb(dk, &v)))
                    return 1;
                break;
            }
        }
    }
    if (n < 0)
        return 1;
    return 0;
}

