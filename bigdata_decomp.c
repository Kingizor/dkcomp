/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2021 Kingizor
 * dkcomp library - SNES DKC2/DKC3 big data decompressor */ 

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

static int write_byte (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit)
        return -1;
    dk->out.data[dk->out.pos++] = val;
    return 0;
}
static int read_out (struct COMPRESSOR *dk, unsigned short v) {
    size_t addr = dk->out.pos - v;
    if (addr > dk->out.pos || addr >= dk->out.limit)
        return -1;
    return dk->out.data[addr];
}

static int read_nibble (struct COMPRESSOR *dk) {
    if (dk->in.pos >= dk->in.length)
        return -1;
    dk->in.bitpos ^= 4;
    if (dk->in.bitpos) {
        return dk->in.data[dk->in.pos  ] >> 4; /* hi 1st */
    }   return dk->in.data[dk->in.pos++] & 15; /* lo 2nd */
}
static int read_byte (struct COMPRESSOR *dk) { /* read byte */
    int hi,lo;
    if ((hi = read_nibble(dk)) < 0
    ||  (lo = read_nibble(dk)) < 0)
        return -1;
    return (hi << 4)|lo;
}

static int relay_byte (struct COMPRESSOR *dk, int addr) {
    int r;
    if ((r = read_out(dk, addr)) < 0)
        return DK_ERROR_OOB_OUTPUT_R;
    if (write_byte(dk, r))
        return DK_ERROR_OOB_OUTPUT_W;
    return 0;
}

static int copy_byte (struct COMPRESSOR *dk) {
    int v;
    if ((v = read_byte(dk)) < 0)
        return DK_ERROR_OOB_INPUT;
    if (write_byte(dk, v))
        return DK_ERROR_OOB_OUTPUT_W;
    return 0;
}

static int bd_loop (struct COMPRESSOR *dk) {
    enum DK_ERROR e;
    for (;;) {
        int c;
        if ((c = read_nibble(dk)) < 0)
            return DK_ERROR_OOB_INPUT;

        switch (c) {

            /* Copy n bytes */
            case 0: {
                int i;
                if ((i = read_nibble(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                if (!i)
                    return 0; /* done! */
                while (i--) 
                    if ((e = copy_byte(dk)))
                        return e;
                break;
            }

            /* Copy two bytes */
            case 2: {
                if ((e = copy_byte(dk)))
                    return e;
            } /* FALLTHROUGH */

            /* Copy one byte */
            case 1: {
                if ((e = copy_byte(dk)))
                    return e;
                break;
            }

            /* Write a byte 3-18 */
            case 3: {
                int i,z;
                if ((i = read_nibble(dk)) < 0
                ||  (z = read_byte(dk))   < 0)
                    return DK_ERROR_OOB_INPUT;
                i += 3;
                while (i--)
                    if (write_byte(dk, z))
                        return DK_ERROR_OOB_OUTPUT_W;
                break;
            }

            /* Write a constant 3-18 */
            case  4: case  5: {
                int i;
                if ((i = read_nibble(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                i += 3;
                while (i--)
                    if (write_byte(dk, dk->in.data[1 + (c & 1)]))
                        return DK_ERROR_OOB_OUTPUT_W;
                break;
            }

            /* Write a word constant */
            case 6: {
                if (write_byte(dk, dk->in.data[5])
                ||  write_byte(dk, dk->in.data[6]))
                    return DK_ERROR_OOB_OUTPUT_W;
                break;
            }

            /* Write a byte constant */
            case 7: case 8: {
                if (write_byte(dk, dk->in.data[3 + ((c ^ 1) & 1)]))
                    return DK_ERROR_OOB_OUTPUT_W;
                break;
            }

            /* Write a recent word */
            case 9: {
                int addr;
                if ((addr = read_nibble(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                addr += 2;
                if ((e = relay_byte(dk, addr))
                ||  (e = relay_byte(dk, addr)))
                    return e;
                break;
            }

            /* 8-bit window */
            case 10: {
                int i,addr;
                if (  (i = read_nibble(dk)) < 0
                || (addr = read_byte  (dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                i += 3;
                addr += i;
                while (i--)
                    if ((e = relay_byte(dk, addr)))
                        return e;
                break;
            }

            /* 12-bit window */
            case 11: {
                int i,addr,lo;
                if (  (i = read_nibble(dk)) < 0
                || (addr = read_byte  (dk)) < 0
                ||   (lo = read_nibble(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                i += 3;
                addr = ((addr << 4) | lo) + 0x103;
                while (i--)
                    if ((e = relay_byte(dk, addr)))
                        return e;
                break;
            }

            /* 16-bit window */
            case 12: {
                int i,addr,lo;
                if ((   i = read_nibble(dk)) < 0
                ||  (addr =   read_byte(dk)) < 0
                ||  (  lo =   read_byte(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                i += 3;
                addr = (addr << 8) | lo;
                while (i--)
                    if ((e = relay_byte(dk, addr)))
                        return e;
                break;
            }

            /* Repeat last byte */
            case 13: {
                if ((e = relay_byte(dk, 1)))
                    return e;
                break;
            }

            /* Repeat last word */
            case 14: {
                if ((e = relay_byte(dk, 2))
                ||  (e = relay_byte(dk, 2)))
                    return e;
                break;
            }

            /* Word LUT */
            case 15: {
                int addr;
                if ((addr = read_nibble(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                addr = (addr << 1) + 7;
                if (write_byte(dk, dk->in.data[addr++])
                ||  write_byte(dk, dk->in.data[addr  ]))
                    return DK_ERROR_OOB_OUTPUT_W;
                break;
            }
        }
    }

}

int bd_decompress (struct COMPRESSOR *dk) {

    enum DK_ERROR e;

    if (dk->in.length < 0x27)
        return DK_ERROR_INPUT_SMALL;

    dk->out.pos = 0;
    dk->in.pos  = 0x27;

    e = bd_loop(dk);

    return e;
}

