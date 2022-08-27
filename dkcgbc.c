/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - DKC GBC compressor and decompressor */

#include <stdlib.h>
#include "dk_internal.h"

/* (format is similar to the DKC SNES tileset format) */

static int read_byte (struct COMPRESSOR *gbc) {
    if (gbc->in.pos >= gbc->in.length)
        return -1;
    return gbc->in.data[gbc->in.pos++];
}
static int read_out (struct COMPRESSOR *gbc, size_t addr) {
    addr = gbc->out.pos - addr;
    if (addr >= gbc->out.pos)
        return -1;
    return gbc->out.data[addr];
}
static int write_byte (struct COMPRESSOR *gbc, unsigned char val) {
    if (gbc->out.pos >= gbc->out.limit)
        return -1;
    gbc->out.data[gbc->out.pos++] = val;
    return 0;
}

int dkcgbc_decompress (struct COMPRESSOR *gbc) {

    int n;
    while ((n = read_byte(gbc)) > 0) {
        int v;
        switch (n >> 6) {
            default: { /* Single byte, 1-127 times */
                if ((v = read_byte(gbc)) < 0)
                    return DK_ERROR_OOB_INPUT;
                while (n--)
                    if (write_byte(gbc, v))
                        return DK_ERROR_OOB_OUTPUT_W;
                break;
            }
            case 2: { /* 1-63 bytes from input */
                n &= 0x3F;
                while (n--) {
                    if ((v = read_byte(gbc)) < 0)
                        return DK_ERROR_OOB_INPUT;
                    if (write_byte(gbc, v))
                        return DK_ERROR_OOB_OUTPUT_W;
                }
                break;
            }
            case 3: { /* 1-63 bytes from output */
                int pos;
                if ((pos = read_byte(gbc)) < 0)
                    return DK_ERROR_OOB_INPUT;
                n &= 0x3F;
                while (n--) {
                    if ((v = read_out(gbc, pos)) < 0)
                        return DK_ERROR_OOB_OUTPUT_R;
                    if (write_byte(gbc, v))
                        return DK_ERROR_OOB_OUTPUT_W;
                }
                break;
            }
        }
    }
    return (n < 0) ? DK_ERROR_OOB_INPUT : 0;
}























/* copy/pasted from DKC SNES with a few small changes */

struct PATH {
    struct PATH *link;
    size_t used;
    struct NCASE {
        unsigned char addr;
        unsigned char mode:2, count:6;
    } nc;
};

struct BIN {
    struct COMPRESSOR *gbc;
    struct PATH *steps;
};

static void reverse_path (struct BIN *bin) {
    struct PATH *prev = &bin->steps[bin->gbc->in.length];
    struct PATH *step = prev->link;
    while (step != NULL) {
        struct PATH *next = step->link;
        step->link = prev;
        prev = step;
        step = next;
    }
}

/* case 0/1: RLE */
static void test_case_1 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *gbc = bin->gbc;
    size_t j = 1;
    struct PATH *step = &bin->steps[i];
    size_t used = step->used + 2;
    size_t limit = (128 < gbc->in.length - i)
                 ?  128 : gbc->in.length - i;

    /* count how many bytes match */
    while (j < limit) {
        if (gbc->in.data[i] != gbc->in.data[i+j++])
            break;
    }

    /* test all subsequent nodes */
    while (j--) {
        struct PATH *next = &bin->steps[i+j];
        if (next->used > used) {
            struct PATH p = { step, used, { 0, !!(j & 64), j & 63 } };
            *next = p;
        }
    }
}

/* case 2: copy input */
static void test_case_2 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *gbc = bin->gbc;
    size_t j;
    struct PATH *step = &bin->steps[i];
    size_t limit = (64 < gbc->in.length+1 - i)
                 ?  64 : gbc->in.length+1 - i;

    /* test all subsequent nodes */
    for (j = 1; j < limit; j++) {
        struct PATH *next = &bin->steps[i+j];
        size_t used = step->used + 1 + j;
        if (next->used > used) {
            struct PATH p = { step, used, { 0, 2, j } };
            *next = p;
        }
    }
}

/* case 3: copy output */
static void test_case_3 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *gbc = bin->gbc;
    size_t j = 0;
    struct PATH *step = &bin->steps[i];
    struct NCASE max = { 0,0,0 };
    size_t used = step->used + 2;
    size_t limit = (64 < gbc->in.length - i)
                 ?  64 : gbc->in.length - i;
    if (i > (1 << 8))
    j = i - (1 << 8) + 1;

    /* find the longest match */
    for (; j < i; j++) {
        size_t match;
        for (match = 0; match < limit; match++)
            if (gbc->in.data[i+match] != gbc->in.data[j+match])
                break;
        if (max.count < match) {
            max.count = match;
            max.addr  = i-j;
        }
        if (max.count == 63)
            break;
    }

    /* test all subsequent nodes */
    for (j = 2; j <= max.count; j++) {
        struct PATH *next = &bin->steps[i+j];
        if (next->used > used) {
            struct PATH p = { step, used, { max.addr, 3, j } };
            *next = p;
        }
    }
}


/* traverse the path and write data */
static int write_data (struct BIN *bin) {
    struct COMPRESSOR *gbc = bin->gbc;
    struct PATH *step = bin->steps;
    int i;

    /* encode the input data */
    while (step != &bin->steps[gbc->in.length]) {
        struct PATH *next = step->link;
        struct NCASE *nc = &next->nc;
        int v;

        /* control byte */
        if (write_byte(gbc, (nc->mode << 6) | nc->count))
            return DK_ERROR_OOB_OUTPUT_W;

        /* data bytes */
        switch (nc->mode) {
            case 0:
            case 1: { /* RLE */
                if ((v = read_byte(gbc)) < 0)
                    return DK_ERROR_OOB_INPUT;
                if (write_byte(gbc, v))
                    return DK_ERROR_OOB_OUTPUT_W;
                gbc->in.pos += 64*nc->mode + nc->count - 1;
                break;
            }
            case 2: { /* copy input */
                for (i = 0; i < nc->count; i++) {
                    if ((v = read_byte(gbc)) < 0)
                        return DK_ERROR_OOB_INPUT;
                    if (write_byte(gbc, v))
                        return DK_ERROR_OOB_OUTPUT_W;
                }
                break;
            }
            case 3: { /* copy output */
                gbc->in.pos += nc->count;
                if (write_byte(gbc, nc->addr))
                    return DK_ERROR_OOB_OUTPUT_W;
                break;
            }
        }
        step = next;
    }

    /* terminating byte */
    if (write_byte(gbc, 0))
        return DK_ERROR_OOB_OUTPUT_W;

    return 0;
}


int dkcgbc_compress (struct COMPRESSOR *gbc) {

    struct PATH *steps = malloc(sizeof(struct PATH) * (gbc->in.length+1));
    struct BIN bin = { gbc, steps };
    size_t i;
    enum DK_ERROR e;

    if (steps == NULL)
        return DK_ERROR_ALLOC;

    /* happy defaults! */
    for (i = 0; i <= gbc->in.length; i++) {
        static const struct PATH p = { NULL, (size_t)-1, {0,0,0} };
        steps[i] = p;
    }
    steps[0].used = 0;

    /* test cases */
    for (i = 0; i < gbc->in.length; i++) {
        test_case_1(&bin, i);
        test_case_2(&bin, i);
        test_case_3(&bin, i);
    }

    reverse_path(&bin);

    struct PATH *step = steps;
    while (step != &bin.steps[gbc->in.length]) {
        struct PATH *next = step->link;
        int count;
            count = next->nc.count;
            if (next->nc.mode == 1)
                count += 64;
        step = step->link;
    }

    e = write_data(&bin);

    free(steps);
    return e;
}

