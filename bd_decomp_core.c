/* SPDX-License-Identifier: MIT
 * Big Data Compression Library
 * Copyright (c) 2020-2021 Kingizor */ 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

static int write_out (struct COMPRESSOR *dc, unsigned char val) {
    if (dc->outpos > 0xFFFF) {
        dk_set_error("Attempted to write out of bounds.");
        return -1;
    }
    dc->output[dc->outpos++] = val;
    return 0;
}
static int read_out (struct COMPRESSOR *dc, unsigned short v) {

    int addr = dc->outpos - v;

    if (addr < 0 || addr > 0xFFFF) {
        dk_set_error("Attempted to read out of bounds.");
        return -1;
    }

    return dc->output[addr];
}

static unsigned char rn (struct COMPRESSOR *dc) { /* read nibble */
    if (dc->inpos >= dc->input_len) {
        dk_set_error("Attempted to read past end of input.");
        return -1;
    }

    dc->half ^= 1;

    if (dc->half) {
        return dc->input[dc->inpos  ] >> 4; /* hi 1st */
    }   return dc->input[dc->inpos++] & 15; /* lo 2nd */
}
static int rb (struct COMPRESSOR *dc) { /* read byte */
    int hi,lo;
    if ((hi = rn(dc)) < 0
    ||  (lo = rn(dc)) < 0)
        return -1;
    return (hi << 4)|lo;
}

static int decode_case (struct COMPRESSOR *dc) {

    int c = rn(dc);
    if (c < 0)
        return 2;

    switch (c) {

        /* Copy n bytes */
        case 0: {
            int i = rn(dc);
            if (!i)
                return 1;
            while (i--) 
                if (write_out(dc, rb(dc)))
                    return 2;
            break;
        }

        /* Write two bytes */
        case 2: {
            if (write_out(dc, rb(dc)))
                return 2;
        } /* FALLTHROUGH */

        /* Write a byte */
        case 1: {
            if (write_out(dc, rb(dc)))
                return 2;
            break;
        }

        /* Write a byte 3-18 */
        case 3: {
            int i,z;
            if ((i = rn(dc)) < 0
            ||  (z = rb(dc)) < 0)
                return 2;
            i += 3;
            while (i--)
                if (write_out(dc, z))
                    return 2;
            break;
        }

        /* Write a constant 3-18 */
        case  4: case  5: {
            int i = rn(dc) + 3;
            while (i--)
                if (write_out(dc, dc->input[1 + (c & 1)]))
                    return 2;
            break;
        }

        /* Write a word constant */
        case 6: {
            if (write_out(dc, dc->input[5]))
                return 2;
            if (write_out(dc, dc->input[6]))
                return 2;
            break;
        }

        /* Write a byte constant */
        case 7: case 8: {
            if (write_out(dc, dc->input[3 + ((c ^ 1) & 1)]))
                return 2;
            break;
        }

        /* Write a recent word */
        case 9: {
            int addr = rn(dc) + 2;
            if (write_out(dc, read_out(dc, addr)))
                return 2;
            if (write_out(dc, read_out(dc, addr)))
                return 2;
            break;
        }

        /* 8-bit window */
        case 10: {
            int i    = rn(dc) + 3;
            int addr = rb(dc) + i;
            while (i--)
                if (write_out(dc, read_out(dc, addr)))
                    return 2;
            break;
        }

        /* 12-bit window */
        case 11: {
            int i     = rn(dc) + 3;
            int addr  = rb(dc) << 4;
                addr |= rn(dc);
                addr += 0x103;
            while (i--)
                if (write_out(dc, read_out(dc, addr)))
                    return 2;
            break;
        }

        /* 16-bit window */
        case 12: {
            int i     = rn(dc) + 3;
            int addr  = rb(dc) << 8;
                addr |= rb(dc);
            while (i--)
                if (write_out(dc, read_out(dc, addr)))
                    return 2;
            break;
        }

        /* Repeat last byte */
        case 13: {
            if (write_out(dc, read_out(dc, 1)))
                return 2;
            break;
        }

        /* Repeat last word */
        case 14: {
            if (write_out(dc, read_out(dc, 2)))
                return 2;
            if (write_out(dc, read_out(dc, 2)))
                return 2;
            break;
        }

        /* Word LUT */
        case 15: {
            int addr = (rn(dc) << 1) + 7;
            if (write_out(dc, dc->input[addr++]))
                return 2;
            if (write_out(dc, dc->input[addr  ]))
                return 2;
            break;
        }
    }
    return 0;
}

int bd_decompress (struct COMPRESSOR *dc) {

    dc->outpos = 0;
    dc->inpos  = 0x27;

    for (;;) {
        switch (decode_case(dc)) {
            case 0: { continue; }
            case 1: { return 0; }
            case 2: { return 1; }
        }
    }
    return 0;
}

