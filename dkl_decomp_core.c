/* SPDX-License-Identifier: MIT
 * Copyright (c) 2021-2022 Kingizor
 * dkcomp library - DKL layout decompressor */

/* clumsy, needs more work! */

#include <stdlib.h>
#include "dk_internal.h"

/* Read a nibble from input */
static int read_nibble_z (struct COMPRESSOR *dk) {
    unsigned char z;
    if (dk->in.pos >= dk->in.length)
        return -1;
    z = (dk->in.data[dk->in.pos] >> dk->in.bitpos) & 15;
    if (!dk->in.bitpos)
        dk->in.pos++;
    dk->in.bitpos ^= 4;
    return z;
}

static int read_byte_z (struct COMPRESSOR *dk) {
    int n1, n2;
    if ((n1 = read_nibble_z(dk)) < 0
    ||  (n2 = read_nibble_z(dk)) < 0)
        return -1;
    return n2 | (n1 << 4);
}

/* Write a byte to output */
static int write_byte_z (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit)
        return 1;
    dk->out.data[dk->out.pos++] = val;
    return 0;
}

#define read_nibble(X) \
    if ((X = read_nibble_z(dk)) < 0)\
        return DK_ERROR_OOB_INPUT;

#define read_byte(X) \
    if ((X =   read_byte_z(dk)) < 0)\
        return DK_ERROR_OOB_INPUT;

#define write_byte(X) \
    if (write_byte_z(dk, X))\
        return DK_ERROR_OOB_OUTPUT_W;

static int a52 (struct COMPRESSOR *dk, int a, int n) {

    dk->out.data[dk->out.pos] = n;
    n = a;
    while (n--) {
        int t;
        read_nibble(t);
        a = t | dk->out.data[dk->out.pos];
        write_byte(a);
        a &= 0xF0;
        dk->out.data[dk->out.pos] = a;
    }
    return 0;
}

int dkl_decompress (struct COMPRESSOR *dk) {

    dk->in.bitpos = 4;

    for (;;) {
        int a,n;
        read_nibble(a);

        if (a < 12) {
            int b = a << 4;
            read_nibble(a);
            a |= b;
            if (a == 0xBE) { /* write a byte, n+3 times */
                read_byte(a); read_nibble(n); n += 3;
                while (n--) write_byte(a++);
            }
            else if (a > 0xBE) { /* write a word, n+2 times */
                int v0,v1;
                read_byte(v0); read_byte(v1); read_nibble(n); n += 2;
                while (n--) { write_byte(v0); write_byte(v1); }
            }
            else { /* write a byte */
                write_byte(a);
            }
        }
        else {
            a = (a - 13) & 0xFF;
            if (a > 0xF2) { /* copy a byte from output, n+4 times */
                int outpos = 0;
                a = 0;
                read_byte(outpos);
                if (outpos & 1) {
                    read_nibble(a);
                    outpos |= a << 8;
                }
                outpos >>= 1;
                read_nibble(a);
                if (a == 15) {
                    read_byte(a);
                }

                n = a + 4;
                while (n--) {
                    size_t addr = dk->out.pos - outpos - 1;
                    if (addr > dk->out.pos)
                        return DK_ERROR_OOB_OUTPUT_R;
                    write_byte(dk->out.data[addr]);
                }
            }
            else {
                enum DK_ERROR e;
                if (a) {
                    if (--a) {
                        int t;
                        read_byte(t);
                        read_nibble(a);
                        if (a & 8) {
                            read_nibble(n);
                            a = (n | ((a & 7) << 4)) + 11;
                        }
                        else {
                            a += 3;
                        }
                        while (a--) /* write a byte, 'a' times */
                            write_byte(t);
                        continue;
                    }
                    else {
                        read_nibble(a);
                        if (a == 0x0E)
                            return 0;
                        n = a << 4;
                        read_nibble(a);
                        a += 4;
                    }
                }
                else { /* modify and write? 19 times */
                    read_nibble(n);
                    n = (n >> 4) | ((n << 4) & 0xF0);
                    read_byte(a);
                    if ((e = a52(dk, 0x13, n)))
                        return e;
                    a++;
                }
                if ((e = a52(dk, a, n))) /* modify and write? 'a' times */
                    return e;
            }
        }
    }
    return 0;
}

