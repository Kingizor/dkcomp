/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA BIOS RLE compressor and decompressor */

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

int gbarle_decompress (struct COMPRESSOR *gba) {
    size_t output_size;

    if (gba->in.length < 5)
        return DK_ERROR_INPUT_SMALL;

    if ((gba->in.data[0] & 0xF0) != 0x30)
        return DK_ERROR_SIG_WRONG;

    output_size = (gba->in.data[3] << 16) | gba->in.data[1]
                | (gba->in.data[2] <<  8);
    gba->in.pos += 4;

    while (gba->out.pos < output_size) {
        int i, count, v;
        if ((v = read_byte(gba)) < 0)
            return DK_ERROR_OOB_INPUT;
        count = v & 0x7F;
        if (v & 0x80) {
            count += 3;
            if ((v = read_byte(gba)) < 0)
                return DK_ERROR_OOB_INPUT;
            for (i = 0; i < count; i++)
                if (write_byte(gba, v))
                    return DK_ERROR_OOB_OUTPUT_W;
        }
        else {
            count += 1;
            for (i = 0; i < count; i++) {
                if ((v = read_byte(gba)) < 0)
                    return DK_ERROR_OOB_INPUT;
                if (write_byte(gba, v))
                    return DK_ERROR_OOB_OUTPUT_W;
            }
        }
    }
    return 0;
}



struct PATH {
    struct PATH *link;
    size_t used;
    struct NCASE { unsigned char rle:1, count:7; } ncase;
};

int gbarle_compress (struct COMPRESSOR *gba) {

    struct PATH *steps = malloc((gba->in.length+1) * sizeof(struct PATH));
    struct PATH *step = NULL, *prev = NULL;
    size_t i;

    if (steps == NULL)
        return DK_ERROR_ALLOC;

    /* write header */
    if (write_byte(gba, 0x30)
    ||  write_byte(gba, gba->in.length)
    ||  write_byte(gba, gba->in.length >>  8)
    ||  write_byte(gba, gba->in.length >> 16))
        goto write_error;

    /* happy defaults */
    for (i = 0; i <= gba->in.length; i++) {
        static const struct PATH p = { NULL, -1llu, { 0,0 } };
        steps[i] = p;
    }
    steps[0].used = 0;

    /* determine the best path */
    for (i = 0; i < gba->in.length; i++) {
        int a = read_byte(gba);
        size_t count = 0, limit = 130;

        /* count how many subsequent bytes match */
        if (limit > (gba->in.length-i+1))
            limit =  gba->in.length-i+1;
        while (count++ < limit && a == read_byte(gba));
        gba->in.pos = i+1;

        /* test RLE cases */
        for (; count >= 3; count--) {
            struct PATH *next = &steps[i+count];
            size_t used = steps[i].used + 2.0;
            if (next->used > used) {
                struct PATH p = { &steps[i], used, { 1, count - 3 } };
                *next = p;
            }
        }

        /* test non-RLE cases */
        limit = 128;
        if (limit > (gba->in.length-i+1))
            limit = (gba->in.length-i+1);
        for (count = 1; count < limit; count++) {
            struct PATH *next = &steps[i+count];
            size_t used = steps[i].used + 1.0 + count;
            if (next->used > used) {
                struct PATH p = { &steps[i], used, { 0, count - 1 } };
                *next = p;
            }
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

    /* traverse the path and write data */
    step = steps;
    while (step != &steps[gba->in.length]) {
        struct PATH   *next = step->link;
        unsigned char *data = gba->in.data + (step - steps);
        unsigned char count = next->ncase.count;
        unsigned char type  = next->ncase.rle;

        /* control byte */
        if (write_byte(gba, count | (type << 7)))
            goto write_error;

        /* data bytes */
        if (type) {
            if (write_byte(gba, *data))
                goto write_error;
        }
        else {
            for (i = 0; i < count+1u; i++) {
                if (write_byte(gba, *data++))
                    goto write_error;
            }
        }
        step = next;
    }

    free(steps);
    return 0;
write_error:
    free(steps);
    return DK_ERROR_OOB_OUTPUT_W;
}

