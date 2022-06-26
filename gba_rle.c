/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA BIOS RLE compressor and decompressor */

#include <stdlib.h>
#include "dk_internal.h"

static int read_byte (struct COMPRESSOR *gba) {
    if (gba->in.pos >= gba->in.length) {
        dk_set_error("Tried to read past end of input");
        return -1;
    }
    return gba->in.data[gba->in.pos++];
}
static int write_byte (struct COMPRESSOR *gba, unsigned char v) {
    if (gba->out.pos >= gba->out.limit) {
        dk_set_error("Tried to write out of bounds");
        return 1;
    }
    gba->out.data[gba->out.pos++] = v;
    return 0;
}

int gbarle_decompress (struct COMPRESSOR *gba) {
    size_t output_size;

    if (gba->in.length < 5) {
        dk_set_error("Data too short for RLE");
        return 1;
    }
    if ((gba->in.data[0] & 0xF0) != 0x30) {
        dk_set_error("Incorrect identifier for RLE");
        return 1;
    }
    output_size = (gba->in.data[3] << 16) | gba->in.data[1]
                | (gba->in.data[2] <<  8);
    gba->in.pos += 4;

    while (gba->out.pos < output_size) {
        int i, count, v = read_byte(gba);
        if (v < 0) return 1;
        count = v & 0x7F;
        if (v & 0x80) {
            count += 3;
            v = read_byte(gba);
            for (i = 0; i < count; i++)
                if (write_byte(gba, v))
                    return 1;
        }
        else {
            count += 1;
            for (i = 0; i < count; i++) {
                v = read_byte(gba);
                if (v < 0 || write_byte(gba, v))
                    return 1;
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

    if (steps == NULL) {
        dk_set_error("Failed to allocate memory for path");
        return 1;
    }

    /* write header */
    if (write_byte(gba, 0x30)
    ||  write_byte(gba, gba->in.length)
    ||  write_byte(gba, gba->in.length >>  8)
    ||  write_byte(gba, gba->in.length >> 16))
        goto error;

    /* happy defaults */
    for (i = 0; i < gba->in.length+1; i++) {
        static const struct PATH p = { NULL, -1llu, { 0,0 } };
        steps[i] = p;
    }
    steps[0].used = 0;

    /* determine the best path */
    for (i = 0; i < gba->in.length; i++) {
        size_t rle_count, copy_count, copy_limit;
        int a = read_byte(gba);

        /* count how many subsequent bytes match */
        if (a < 0) break;
        for (rle_count = 1; rle_count < 130; rle_count++)
            if (a != read_byte(gba))
                break;
        gba->in.pos = i+1;

        /* test RLE cases */
        for (; rle_count >= 3; rle_count--) {
            struct PATH *next = &steps[i+rle_count];
            size_t used = steps[i].used + 2.0;
            if (next->used > used) {
                struct PATH p = { &steps[i], used, { 1, rle_count - 3 } };
                *next = p;
            }
        }

        /* test non-RLE cases */
        copy_limit = ((gba->in.length+1 - gba->in.pos) < 128) ?
                      (gba->in.length+1 - gba->in.pos) : 128;
        for (copy_count = 1; copy_count < copy_limit; copy_count++) {
            struct PATH *next = &steps[i+copy_count];
            size_t used = steps[i].used + 1.0 + copy_count;
            if (next->used > used) {
                struct PATH p = { &steps[i], used, { 0, copy_count - 1 } };
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
            goto error;

        /* data bytes */
        if (type) {
            if (write_byte(gba, *data))
                goto error;
        }
        else {
            for (i = 0; i < count+1u; i++) {
                if (write_byte(gba, *data++))
                    goto error;
            }
        }
        step = next;
    }

    free(steps);
    return 0;
error:
    free(steps);
    return 1;
}

