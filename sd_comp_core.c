/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2022 Kingizor
 * dkcomp library - SNES DKC3 small data compressor and decompressor */

#include <stdlib.h>
#include "dk_internal.h"

/* decompressor */

static int read_byte (struct COMPRESSOR *sd, size_t addr) {
    if (addr >= sd->in.length) {
        dk_set_error("Tried to read out of bounds.");
        return -1;
    }
    return sd->in.data[addr];
}
static int read_word (struct COMPRESSOR *sd, size_t addr) {
    int r0, r1;
    if ((r0 = read_byte(sd, addr))   < 0
    ||  (r1 = read_byte(sd, addr+1)) < 0)
        return -1;
    return r0 | (r1 << 8);
}

/* read n bits from input */
static int read_bits (struct COMPRESSOR *sd, int count) {
    unsigned val = 0;
    while (count--) {
        unsigned char bit;
        int v = read_byte(sd, sd->in.pos);
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
static int modify_word (struct COMPRESSOR *sd, size_t addr, int val) {
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
        if ((loop  = read_bits(sd, 1)) < 0)
            return 1;
        if ((val   = read_bits(sd, val_size)) < 0)
            return 1;
        val <<= shift;
        if (loop) {
            if ((count = read_bits(sd, count_size)) < 0)
                return 1;
        }
        else {
            count = 1;
        }
        if (!count)
            break;
        while (count--)
            if (modify_word(sd, addr++, val))
                return 1;
    }
    return 0;
}

/* This routine determines values to be placed in the low 10 bits */
static int  main_decompress (struct COMPRESSOR *sd) {

    size_t addr = 0;

    for (;;) {
        unsigned char  mode = read_bits(sd,  2);
        unsigned short val  = read_bits(sd, 10);
        int count;

        if (!mode) { /* write single value once */
            count = 1;
        }
        else if (mode == 1) { /* write single value 1-63 times  */
            if ((count = read_bits(sd, 6)) < 0)
                return 1;
            if (!count) /* Quit if zero */
                break;
        }
        else { /* write incrementing or decrementing value 1-15 times */
            if ((count = read_bits(sd, 4)) < 0)
                return 1;

            /* These cases don't have the exit condition,
               so the game would write 65536 values in succession */
            if (!count) {
                dk_set_error(
                    "Encountered an exit condition in a case without "
                    "an exit check."
                );
                return 1;
            }
        }

        while (count--) {
            if (modify_word(sd, addr++, val))
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
    if ((subs = read_byte(sd, sd->in.pos + 0)) < 0)
        return 1;
    subs &= 7;

    /* next word is the output size in words */
    if ((i = read_word(sd, sd->in.pos + 1)) < 0)
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







/* compressor */

/* push bits to the output */
static int write_bits (struct COMPRESSOR *sd, int count, unsigned val) {

    /* for each bit */
    while (count--) {

        /* OR the bit into the output */
        sd->out.data[sd->out.pos] |= ((val >> count) & 1)
                               << (sd->out.bitpos ^ 7);

        /* Increment the bit position */
        sd->out.bitpos = (sd->out.bitpos + 1) & 7;

        /* increment position if we've pushed a full byte */
        if (!sd->out.bitpos)
            sd->out.pos++;

        /* don't go past the end of the buffer */
        if (sd->out.pos >= sd->out.limit) {
            dk_set_error("Attempted to write past end of buffer.");
            return 1;
        }

    }
    return 0;
}

/* check if the corresponding bits are present */
static int bits_active (struct COMPRESSOR *sd, unsigned char bit) {
    size_t i;
    for (i = 1; i < sd->in.length; i+=2)
        if (sd->in.data[i] & bit)
            return 1;
    return 0;
}

/* Variants of this subroutine are called up to four times,
   each enabling 1-3 bits in each word in the output */
static int encode_subs (
    struct COMPRESSOR *sd,
    unsigned char bit_val, /* which bits to process */
    size_t loop_limit
) {
    size_t i;
    int count; /* How many bits?            (6 or 4) */
    int shift; /* How many trailing zeroes? (1 or 3) */
    {
        unsigned char b;
        for (b = bit_val, shift = 0; !(b & 1); shift++, b >>=    1);
        for (b = bit_val, count = 0;        b; count++, b &= b - 1);
    }

    for (i = 1; i < sd->in.length;) {
        unsigned char word = sd->in.data[i] & bit_val; /* high byte only */
        size_t j; /* how many future words have the same bits set */
        unsigned val;

        /* check the consistency over the next n words */
        for (j = 2; j < loop_limit*2 && j < (sd->in.length - i); j+=2)
            if ((sd->in.data[i+j] & bit_val) ^ word)
                break;

        i += j;
        j >>= 1;

        val = (word & bit_val) >> shift;

        /* Sometimes it's better to have multiple singles than a loop */
        if ((j * (1 + count)) < 8) { /* Singles */
            while (j--)
                if (write_bits(sd, 1 + count, val))
                    return 1;
        }
        else {
            val = ((val | (1 << count)) << (7 - count)) | j;
            if (write_bits(sd, 8, val))
                return 1;
        }

    }

    /* Quit */
    if (write_bits(sd, 8, 1 << 7))
        return 1;

    return 0;
}

static int encode_main (struct COMPRESSOR *sd) {

    size_t i;
    for (i = 0; i < sd->in.length;) {

        enum { UNIQUE, SAME, INC, DEC } mode = UNIQUE;
        int LC;

        /* Read current word */
        int w;
        if ((w = read_word(sd, i)) < 0)
            return 1;
        unsigned short w1 = w & 0x3FF;

        if (i < sd->in.length-2) {

            size_t addr = i + 2;
            int lim;

            /* Read next word */
            if ((w = read_word(sd, addr)) < 0)
                return 1;
            unsigned short w2 = w & 0x3FF;

            /* Determine the pattern */
            signed short diff = w2 - w1;
            switch (diff) {
                default: { mode = UNIQUE; lim =  0; break; }
                case  0: { mode =   SAME; lim = 63; break; }
                case  1: { mode =    INC; lim = 15; break; }
                case -1: { mode =    DEC; lim = 15; break; }
            }

            /* How many future words match the pattern? */
            for (LC = 2; LC < lim; LC++) {
                unsigned short w3;
                addr += 2;
                if (addr >= sd->in.length)
                    break;
                if ((w = read_word(sd, i)) < 0)
                    return 1;
                w3 = w & 0x3FF;
                if ((w3 - w2) != diff)
                    break;
                w2 = w3;
            }

        }
        {
        unsigned short val = (mode << 10) | w1;

        if (!mode) { /* Single */
            if (write_bits(sd, 12, val))
                return 1;
            i += 2;
        }
        else { /* Loop */
            int LS = (mode == 1) ? 6 : 4;
            if (write_bits(sd, 12+LS, (val << LS) | LC))
                return 1;
            i += LC*2;
        }
        }
    }

    /* Quit */
    if (write_bits(sd, 18, 1 << 16))
        return 1;

    return 0;
}

int sd_compress (struct COMPRESSOR *sd) {

    int i;

    /* output size (i.e. word count) */
    sd->out.data[2] = sd->in.length >> 9;
    sd->out.data[1] = sd->in.length >> 1;
    sd->out.pos = 3;

    /* the first three subroutines are optional (2000,4000,8000) */
    for (i = 0; i < 3; i++) {
        if (bits_active(sd, 0x20 << i)) {
            sd->out.data[0] |= 1 << i;
            if (encode_subs(sd, 0x20 << i, 63))
                return 1;
        }
    }

    /* the fourth subroutine is mandatory (1C00) */
    encode_subs(sd, 0x1C, 15);

    /* The main loop (03FF) */
    if (encode_main(sd))
        return 1;

    if (sd->out.bitpos)
        sd->out.pos++;
    return 0;
}

