/* SPDX-License-Identifier: MIT
 * Small Data Compression Library
 * Copyright (c) 2020 Kingizor */

#include <stdio.h>
#include <stdlib.h>
#include "dk_internal.h"

static int rb (struct COMPRESSOR *sd, size_t addr) {
    if (addr >= sd->in.length) {
        dk_set_error("Tried to read out of bounds.");
        return -1;
    }
    return sd->in.data[addr];
}
static int rw (struct COMPRESSOR *sd, size_t addr) {
    int r0, r1;
    if ((r0 = rb(sd, addr))   < 0
    ||  (r1 = rb(sd, addr+1)) < 0)
        return -1;
    return r0 | (r1 << 8);
}

/* read n bits from input */
static int bits (struct COMPRESSOR *sd, int count) {
    unsigned val = 0;
    while (count--) {
        unsigned char bit;
        int v = rb(sd, sd->in.pos);
        if (v < 0)
            return -1;
        bit = !!(v & (1 << (sd->in.bitpos ^ 7)));
        val |= bit << count;
        if (++sd->in.bitpos == 8) {
            sd->in.pos++;
            sd->in.bitpos = 0;
        }
    }
    return val;
}

/* modify word (all writes are rmw) */
static int mw (struct COMPRESSOR *sd, size_t addr, int val) {
    addr <<= 1;
    if (addr+1 >= sd->out.limit) {
        dk_set_error("Attempted to write out of bounds.");
        return -1;
    }
    sd->out.data[addr  ] |= val;
    sd->out.data[addr+1] |= val >> 8;
    return 0;
}


/* These routines (four variants) determine the upper 6 bits */
static int sub_decompress (struct COMPRESSOR *sd, int mode) {

    size_t addr = 0;
    unsigned shift, count_size, val_size;
    if (mode == 3) {
          val_size = 3;
        count_size = 4;
        shift = 10;
    }
    else {
          val_size = 1;
        count_size = 6;
        shift = 13 + mode;
    }

    for (;;) {
        int loop, val, count;
        if ((loop  = bits(sd, 1)) < 0)
            return 1;
        if ((val   = bits(sd, val_size)) < 0)
            return 1;
        val <<= shift;
        if (loop) {
            if ((count = bits(sd, count_size)) < 0)
                return 1;
        }
        else {
            count = 1;
        }
        if (!count)
            break;
        while (count--)
            if (mw(sd, addr++, val))
                return 1;
    }
    return 0;
}

/* This routine determines values to be placed in the low 10 bits */
static int  main_decompress (struct COMPRESSOR *sd) {

    size_t addr = 0;

    for (;;) {
        unsigned char  mode = bits(sd,  2);
        unsigned short val  = bits(sd, 10);
        int count;

        if (!mode) { /* write single value once */
            count = 1;
        }
        else if (mode == 1) { /* write single value 1-63 times  */
            if ((count = bits(sd, 6)) < 0)
                return 1;
            if (!count) /* Quit if zero */
                break;
        }
        else { /* write incrementing or decrementing value 1-15 times */
            if ((count = bits(sd, 4)) < 0)
                return 1;

            /* These cases don't have the exit condition,
               so the game would write 65536 values in succession */
            if (!count) {
                fprintf(stderr,
                    "Encountered an exit condition in a case without "
                    "an exit check."
                );
                return 1;
            }
        }

        while (count--) {
            if (mw(sd, addr++, val))
                return 1;
            if (mode == 2)
                val++;
            else if (mode == 3)
                val--;
            val &= 0x3FF;
        }
    }
    return 0;
}

int sd_decompress (struct COMPRESSOR *sd) {

    int i, subs;

    /* first byte indicates which subs to call */
    if ((subs = rb(sd, sd->in.pos + 0)) < 0)
        return 1;
    subs &= 7;

    /* next word is the output size in words */
    if ((i = rw(sd, sd->in.pos + 1)) < 0)
        return 1;
    sd->out.pos  = i << 1;

    sd->in.pos  += 3;

    /* first three subs are optional */
    for (i = 0; i < 3; i++)
        if (subs & (1 << i))
            if (sub_decompress(sd, i)) /* {2,4,8}000 */
                return 1;

    /* fourth sub and the main routine are mandatory */
    if (sub_decompress(sd, 3)) /* 1C00 */
        return 1;
    if (main_decompress(sd))   /* 03FF */
        return 1;
    return 0;
}

