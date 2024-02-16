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


struct PATH {
    struct PATH *link;
    size_t used;
    int ncase;
};

static void test_case (struct PATH *steps, size_t i, size_t j, size_t used, int ncase) {
    struct PATH *a = &steps[i], *b = &steps[j];
    if (b->used  > a->used + used) {
        b->used  = a->used + used;
        b->link  = a;
        b->ncase = ncase;
    }
}
static void test_cases (struct COMPRESSOR *gb, struct PATH *steps) {
    size_t i,j;
    for (i = 0; i < gb->in.length; i++) {
        for (j = i+1; j < i+0x81 && j <= gb->in.length; j++) { /* raw */
            test_case(steps, i, j, 1+j-i, 0);
        }
        for (j = i+2; j < i+0x82 && j <= gb->in.length; j++) { /* RLE */
            if (gb->in.data[i] != gb->in.data[j-1])
                break;
            test_case(steps, i, j, 2, 1);
        }
    }
}

static void reverse_path (struct COMPRESSOR *gb, struct PATH *steps) {
    struct PATH *prev = &steps[gb->in.length];
    struct PATH *step = prev->link;
    while (step != NULL) {
        struct PATH *next = step->link;
        step->link = prev;
        prev = step;
        step = next;
    }
}
static int write_output (struct COMPRESSOR *gb, struct PATH *steps) {
    struct PATH *step = steps;
    while (step != &steps[gb->in.length]) {
        int a, count = step->link - step;
        gb->in.pos = step - steps;
        if (step->link->ncase) { /* rle */
            WB(0x80 | (count-2));
            RB(a); WB(a);
        }
        else { /* raw */
            WB(count-1);
            while (count--) { RB(a); WB(a); }
        }
        step = step->link;
    }
    return 0;
}

int gbprinter_compress (struct COMPRESSOR *gb) {
    struct PATH *steps;
    size_t i;
    int e;

    if (gb->in.length < 0x280) return DK_ERROR_INPUT_SMALL;
    if (gb->in.length > 0x280) return DK_ERROR_INPUT_LARGE;

    steps = malloc((gb->in.length+1) * sizeof(struct PATH));
    if (steps == NULL)
        return DK_ERROR_ALLOC;

    for (i = 0; i <= gb->in.length; i++) {
        static const struct PATH p = { NULL, (size_t)-1, 0 };
        steps[i] = p;
    }
    steps[0].used = 0;
    test_cases  (gb, steps);
    reverse_path(gb, steps);
    e = write_output(gb, steps);
    free(steps);
    return e;
}

