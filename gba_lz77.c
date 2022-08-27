/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA BIOS LZ77 compressor and decompressor */

#include <stdlib.h>
#include "dk_internal.h"

static int read_byte (struct COMPRESSOR *gba) {
    if (gba->in.pos >= gba->in.length)
        return -1;
    return gba->in.data[gba->in.pos++];
}

static int write_byte (struct COMPRESSOR *gba, unsigned char v) {
    if (gba->out.pos >= gba->out.limit)
        return 1;
    gba->out.data[gba->out.pos++] = v;
    return 0;
}

int gbalz77_decompress (struct COMPRESSOR *gba) {
    size_t output_size;

    if (gba->in.length < 5)
        return DK_ERROR_INPUT_SMALL;

    if ((gba->in.data[0] & 0xF0) != 0x10)
        return DK_ERROR_SIG_WRONG;

    output_size = (gba->in.data[3] << 16) | gba->in.data[1]
                | (gba->in.data[2] <<  8);
    gba->in.pos += 4;

    while (gba->out.pos < output_size) {
        int blocks, i;
        if ((blocks = read_byte(gba)) < 0)
            return DK_ERROR_OOB_INPUT;
        for (i = 0; i < 8; i++) {
            int v1;
            if ((v1  = read_byte(gba)) < 0)
                return DK_ERROR_OOB_INPUT;
            if (blocks & (1 << (7^i))) {
                unsigned short count, outpos;
                int v2;
                if ((v2 = read_byte(gba)) < 0)
                    return DK_ERROR_OOB_INPUT;
                count  =  (v1 >> 4) + 3;
                outpos = ((v1 & 15) << 8) | v2;
                if (outpos > gba->out.pos-1)
                    return DK_ERROR_LZ77_HIST;
                while (count--) {
                    if (write_byte(gba, gba->out.data[gba->out.pos-outpos-1]))
                        return DK_ERROR_OOB_OUTPUT_W;
                }
            }
            else {
                if (write_byte(gba, v1))
                    return DK_ERROR_OOB_OUTPUT_W;
            }
            if (gba->out.pos == output_size)
                break;
        }
    }
    return 0;
}




struct PATH {
    struct PATH *link;
    size_t used; /* bytes and traversals used to get to this point */
    struct NCASE { unsigned short count:4, offset:12; } ncase;
};

int gbalz77_compress (struct COMPRESSOR *gba) {

    struct PATH *steps = malloc((gba->in.length+1) * sizeof(struct PATH));
    struct PATH *step = NULL, *prev = NULL;
    size_t i;

    if (steps == NULL)
        return DK_ERROR_ALLOC;

    /* write header */
    if (write_byte(gba, 0x10)
    ||  write_byte(gba, gba->in.length)
    ||  write_byte(gba, gba->in.length >>  8)
    ||  write_byte(gba, gba->in.length >> 16))
        goto write_error;

    /* happy defaults */
    for (i = 0; i < gba->in.length+1; i++) {
        static const struct PATH p = { NULL, (size_t)-1, { 0,0 } };
        steps[i] = p;
    }
    steps[0].used = 0;

    /* determine the best path */
    for (i = 0; i < gba->in.length; i++) {
        struct NCASE max = {0,0};
        struct PATH *next;
        size_t used;
        size_t j = 0; /* we can look this far back in the window */

        step = &steps[i];
        used = step->used + 10;

        /* window is 12-bits max, data is up to 24-bit */
        if (i > (1 << 12))
        j = i - (1 << 12);

        /* find the longest matching block in history */
        for (; j < i; j++) {
            unsigned char *a = &gba->in.data[i];
            unsigned char *b = &gba->in.data[j];
            size_t cmplim = 18; /* don't compare past this point */
            size_t matched, k;

            if (cmplim > (gba->in.length - i))
                cmplim = (gba->in.length - i);

            /* how many bytes match up to n in these two buffers */
            for (matched = 0; matched < cmplim; matched++)
                if (*a++ != *b++)
                    break;

            /* test all possible history cases */
            if (matched >= 3 && max.count <= (matched-3)) {
                for (k = max.count; k <= matched-3; k++) {
                    next = &steps[i+k+3];
                    if (next->used > used) {
                        struct PATH p = { step, used, { k, i-j-1 } };
                        *next = p;
                    }
                }
                max.count  = matched-3;
                max.offset = j;
                if (max.count == 15)
                    break;
            }
        }

        /* test the default case */
        next = &steps[i+1];
        used = step->used + 9;
        if (next->used > used) {
            /* don't interpret the owl operator as a valid count */
            /* avoid that by checking adjacent distance first */
            struct PATH p = { step, used, { 0,0 } };
            *next = p;
        }
    }

    /* reverse path direction */
    prev = &steps[gba->in.length];
    step = prev->link;
    while (step != NULL) {
        struct PATH *next = step->link;
        step->link = prev;
        prev = step;
        step = next;
    }
    steps[gba->in.length].link = NULL;

    /* traverse the path and write data */
    step = steps;
    while (step != &steps[gba->in.length]) {
        struct PATH *node = step;
        unsigned char block = 0;
        int blockc;

        /* calculate the block byte by checking the next n <= 8 nodes */
        for (blockc = 0; blockc < 8; blockc++) {
            struct PATH *next = node->link;
            if (next == NULL)
                break;
            block <<= 1;
            if ((next - node) > 1)
                block |= 1;
            node = next;
        }
        if (write_byte(gba, block << (8 - blockc)))
            goto write_error;

        /* write the blocks */
        while (blockc--) {
            struct PATH *next = step->link;
            if ((next - step) == 1) { /* default case */
                if (write_byte(gba, gba->in.data[step-steps]))
                    goto write_error;
            }
            else { /* history case */
                struct NCASE *nc = &next->ncase;
                if (write_byte(gba, (nc->offset >> 8) | (nc->count << 4))
                ||  write_byte(gba,  nc->offset))
                    goto write_error;
            }
            step = next;
        }
    }
    free(steps);
    return 0;
write_error:
    free(steps);
    return DK_ERROR_OOB_OUTPUT_W;
}

