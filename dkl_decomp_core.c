/* SPDX-License-Identifier: MIT
 * DKL Decompression Library
 * Copyright (c) 2022 Kingizor */

#include <stdio.h>
#include <stdlib.h>
#include "dk_internal.h"

/* Read a nibble from input */
static int rn (struct COMPRESSOR *dk) {
    if (dk->in.pos >= dk->in.length) {
        dk_set_error("Tried to read out of bounds. (input)");
        return -1;
    }
    unsigned char z = (dk->in.data[dk->in.pos] >> dk->in.bitpos) & 15;
    if (!dk->in.bitpos)
        dk->in.pos++;
    dk->in.bitpos ^= 4;
    return z;
}

static int rb (struct COMPRESSOR *dk) {
    int n1, n2;
    if ((n1 = rn(dk)) < 0
    ||  (n2 = rn(dk)) < 0)
        return -1;
    return n2 | (n1 << 4);
}

/* Read a byte from output */
static int rbo (struct COMPRESSOR *dk, size_t addr) {
    if (addr >= dk->out.pos) {
        dk_set_error("Tried to read out of bounds. (output)");
        return -1;
    }
    return dk->out.data[addr];
}

/* Write a byte to output */
static int wb (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit) {
        dk_set_error("Tried to write out of bounds.");
        return -1;
    }
    dk->out.data[dk->out.pos++] = val;
    return 0;
}

#define RN(X) if ((X = rn(dk)) < 0) return 1;
#define RB(X) if ((X = rb(dk)) < 0) return 1;
#define WB(X) if (wb(dk, X))        return 1;

static int a52 (struct COMPRESSOR *dk, int a, int n) {

    dk->out.data[dk->out.pos] = n;
    n = a;
    while (n--) {
        int t;
        RN(t);
        a = t | dk->out.data[dk->out.pos];
        WB(a);
        a &= 0xF0;
        dk->out.data[dk->out.pos] = a;
    }
    return 0;
}

int dkl_decompress (struct COMPRESSOR *dk) {

    dk->in.bitpos = 4;

    for (;;) {
        int a,n;
        RN(a);

        if (a < 12) {
            int b = a << 4;
            RN(a);
            a |= b;
            if (a == 0xBE) { /* write a byte, n+3 times */
                RB(a); RN(n); n += 3;
                while (n--) WB(a++);
            }
            else if (a > 0xBE) { /* write a word, n+2 times */
                int v0,v1;
                RB(v0); RB(v1); RN(n); n += 2;
                while (n--) { WB(v0); WB(v1); }
            }
            else { /* write a byte */
                WB(a);
            }
        }
        else {
            a = (a - 13) & 0xFF;
            if (a > 0xF2) { /* copy a byte from output, n+4 times */
                int outpos = 0;
                a = 0;
                RB(outpos);
                if (outpos & 1) {
                    RN(a);
                    outpos |= a << 8;
                }
                outpos >>= 1;
                RN(a);
                if (a == 15) {
                    RB(a);
                }

                n = a + 4;
                while (n--) {
                    int z = rbo(dk, dk->out.pos - outpos - 1);
                    if (z < 0)
                        return 1;
                    WB(z);
                }
            }
            else {
                if (a) {
                    if (--a) {
                        int t;
                        RB(t);
                        RN(a);
                        if (a & 8) {
                            RN(n);
                            a = (n | ((a & 7) << 4)) + 11;
                        }
                        else {
                            a += 3;
                        }
                        while (a--) /* write a byte, 'a' times */
                            WB(t);
                        continue;
                    }
                    else {
                        RN(a);
                        if (a == 0x0E)
                            return 0;
                        n = a << 4;
                        RN(a);
                        a += 4;
                    }
                }
                else { /* modify and write? 19 times */
                    RN(n);
                    n = (n >> 4) | ((n << 4) & 0xF0);
                    RB(a);
                    if (a52(dk, 0x13, n))
                        return 1;
                    a++;
                }
                if (a52(dk, a, n)) /* modify and write? 'a' times */
                    return 1;
            }
        }
    }
    return 0;
}

