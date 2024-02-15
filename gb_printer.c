/* SPDX-License-Identifier: MIT
 * Copyright (c) 2024 Kingizor
 * dkcomp library - GB Printer Compression */

/* file should be split into 640 (0x280) byte chunks before compressing */

#include <stdlib.h>
#include "dk_internal.h"

#define RB(X) if ((X = read_byte(gb)) == -1) return DK_ERROR_OOB_INPUT
#define WB(X) if (write_byte(gb, X))         return DK_ERROR_OOB_OUTPUT_W

static int read_byte (struct COMPRESSOR *gb) {
    if (gb->in.pos >= gb->in.length)
        return -1;
    return gb->in.data[gb->in.pos++];
}
static int write_byte (struct COMPRESSOR *gb, unsigned char v) {
    if (gb->out.pos >= gb->out.limit)
        return 1;
    gb->out.data[gb->out.pos++] = v;
    return 0;
}


int gbprinter_decompress (struct COMPRESSOR *gb) {
    while (gb->in.pos < gb->in.length && gb->out.pos < 0x280) {
        int a, count;
        RB(a);
        if (a & 0x80) { /* repeat */
            count = (a & ~0x80) + 2;
            RB(a);
            while (count--)
                WB(a);
        }
        else { /* copy */
            count = (a & ~0x80) + 1;
            while (count--) {
                RB(a);
                WB(a);
            }
        }
    }
    return 0;
}


struct RLE { size_t pos, length; };

static int next_rle (struct COMPRESSOR *gb, struct RLE *rle) {
    struct FILE_STREAM *in = &gb->in;
    int a = -1, b;
    rle->pos = rle->length = 0;

    if (in->pos >= in->length-1)
        return 0;

    /* find a match */
    RB(b);
    while (a != b && in->pos < in->length) {
        a = b;
        RB(b);
    }
    if (in->pos == in->length)
        return 0;

    /* we found a match */
    rle->pos = in->pos - 2;
    rle->length = 1;

    /* how many matches */
    while (a == b && in->pos < in->length) {
        rle->length++;
        RB(a);
    }
    gb->in.pos--;
    return 0;
}

static int write_raw (struct COMPRESSOR *gb, int n) {
    WB(n-1);
    while (n--) {
        int a;
        RB(a);
        WB(a);
    }
    return 0;
}

static int write_compressed (struct COMPRESSOR *gb, struct RLE *rle) {
    int a;
    if (!rle->length || gb->in.pos != rle->pos)
        return 0;
    RB(a);
    gb->in.pos = rle->pos + rle->length;
    while (rle->length > 0x81) {
        int count = 0x81;
        if (rle->length == 0x82)
            count--;
        rle->length -= count;
        WB(0x80|(count-2));
        WB(a);
    }
    if (rle->length) {
        WB(0x80|(rle->length-2));
        WB(a);
        rle->length = 0;
    }
    return 0;
}

int gbprinter_compress (struct COMPRESSOR *gb) {

    struct FILE_STREAM *in = &gb->in;
    int e;

    if (gb->in.length < 0x280) return DK_ERROR_INPUT_SMALL;
    if (gb->in.length > 0x280) return DK_ERROR_INPUT_LARGE;

    while (in->pos < in->length) {
        struct RLE b, c;
        size_t pos = in->pos;
        int count = 0;

        /* find the next two occurrences of RLE */
        if (next_rle(gb, &b)
        ||  next_rle(gb, &c))
            return DK_ERROR_OOB_INPUT;

        /* skip repeats unless b>2 or b..c && c>2 */
        while (in->pos <= in->length
         &&  b.length == 2
         && (b.length + b.pos != c.pos || c.length == 2)
         ) {
            b.pos    = c.pos;
            b.length = c.length;
            in->pos = b.pos + b.length;
            if (next_rle(gb, &c))
                return DK_ERROR_OOB_INPUT;
        }
        in->pos = pos;

        /* determine how many uncompressed bytes to write */
        count += (in->pos <= b.pos) ? b.pos : in->length;
        count -=  in->pos;

        /* write any uncompressed data */
        while (count > 0x80) {
            count -= 0x80;
            if ((e = write_raw(gb, 0x80)))
                return e;
        }
        if (count && (e = write_raw(gb, count)))
            return e;

        /* write compressed data */
        if ((e = write_compressed(gb, &b))
        ||  (e = write_compressed(gb, &c)))
            return e;
    }

    return 0;
}

