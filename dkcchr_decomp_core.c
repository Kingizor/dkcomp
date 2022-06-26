/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2022 Kingizor
 * dkcomp library - DKC CHR compressor and decompressor */

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

/* Read a byte from input */
static int read_byte (struct COMPRESSOR *dk) {
    if (dk->in.pos >= dk->in.length) {
        dk_set_error("Tried to read out of bounds. (input)");
        return -1;
    }
    return dk->in.data[dk->in.pos++];
}
static int read_word (struct COMPRESSOR *dk) {
    int o1, o2;
    if ((o1 = read_byte(dk)) < 0
    ||  (o2 = read_byte(dk)) < 0)
        return -1;
    return o1 | (o2 << 8);
}
static int read_lut (struct COMPRESSOR *dk, unsigned char addr) {
    size_t inp = dk->in.pos;
    int val;
    dk->in.pos = addr;
    if ((val = read_word(dk)) < 0)
        return -1;
    dk->in.pos = inp;
    return val;
}

/* Read a byte from output */
static int read_out (struct COMPRESSOR *dk, size_t addr) {
    if (addr >= dk->out.pos) {
        dk_set_error("Tried to read out of bounds. (output)");
        return -1;
    }
    return dk->out.data[addr];
}

/* Write a byte from output */
static int write_byte (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit) {
        dk_set_error("Tried to write out of bounds");
        return -1;
    }
    dk->out.data[dk->out.pos++] = val;
    return 0;
}

int dkcchr_decompress (struct COMPRESSOR *dk) {

    int n;
    dk->in.pos = 0x80; /* LUT at 0x00, data at 0x80 */

    while ((n = read_byte(dk)) > 0) {
        int v;
        unsigned char mode;

        mode = n >> 6;
        n &= 0x3F;

        switch (mode) {
            case 0: { /* Copy n bytes from input */
                while (n--)
                    if ((v = read_byte(dk)) < 0
                    ||      write_byte(dk, v))
                        return 1;
                break;
            }
            case 1: { /* Write a byte, n times */
                if ((v = read_byte(dk)) < 0)
                    return 1;
                while (n--)
                    if (write_byte(dk, v))
                        return 1;
                break;
            }
            case 2: { /* Copy n bytes from output */
                int pos = read_word(dk);
                if (pos < 0)
                    return 1;
                while (n--)
                    if ((v = read_out(dk, pos++)) < 0
                    ||     write_byte(dk, v))
                        return 1;
                break;
            }
            case 3: { /* Copy a word from the input LUT */
                if ((v = read_lut(dk, n << 1)) < 0
                ||     write_byte(dk, v)
                ||     write_byte(dk, v >> 8))
                    return 1;
                break;
            }
        }
    }
    return 0;
}









/* compressor */

struct PATH {
    struct PATH *link;
    size_t used;
    struct NCASE {
        unsigned short addr; /* mode 2 */
        unsigned char mode:2, count:6;
    } nc;
};

/* LUT counters */
struct U16 {
    unsigned short index;
    unsigned count;
};

/* container */
struct BIN {
    struct COMPRESSOR *dk;
    struct PATH *steps;
    struct U16 *lutc;
    unsigned short lut[64];
};

static void reset_steps (struct BIN *bin) {
    size_t i;
    for (i = 0; i <= bin->dk->in.length; i++) {
        static const struct PATH p = { NULL, -1llu, {0,0,0} };
        bin->steps[i] = p;
    }
    bin->steps[0].used = 0;
}

static void reverse_path (struct BIN *bin) {
    struct PATH *prev = &bin->steps[bin->dk->in.length];
    struct PATH *step = prev->link;
    while (step != NULL) {
        struct PATH *next = step->link;
        step->link = prev;
        prev = step;
        step = next;
    }
}



static int sort_us (const void *aa, const void *bb) {
    const unsigned short *a = aa, *b = bb;
    return (*a > *b) ? -1 : (*a < *b);
}
static int sort_u16 (const void *aa, const void *bb) {
    const struct U16 *a = aa, *b = bb;
    return (a->count > b->count) ? -1 :
           (a->count < b->count);
}
static void u16_count (
    struct BIN *bin,
    size_t start,
    size_t end,
    int count_mode
) {
    unsigned char *dat = bin->dk->in.data;
    switch (count_mode) {
        case 0: { /* linear count */
            for (; start < end; start++) {
                bin->lutc[dat[start] | (dat[start+1] << 8)].count++;
            }
            break;
        }
        case 1: { /* odd/even count */
            for (; start < end; start += 2)
                bin->lutc[dat[start] | (dat[start+1] << 8)].count++;
            break;
        }
    }
}

static int lut_count (
    struct BIN *bin,
    int count_mode,
    int copy_mode,
    int skip_rle
) {
    unsigned short *lut = bin->lut;
    size_t i,j;

    for (i = 0; i < 65536; i++) {
        bin->lutc[i].index = i;
        bin->lutc[i].count = 0;
    }

    if (copy_mode) { /* only search copy cases */
        struct PATH *step = &bin->steps[bin->dk->in.length];
        while (step->link != NULL) {
            struct PATH *prev = step->link;
            if (!prev->nc.mode) {
                size_t start = prev - bin->steps;
                size_t end   = step - bin->steps;
                switch (count_mode) {
                    case 0: { u16_count(bin, start,   end-1, 0); break; }
                    case 1: { u16_count(bin, start,   end-2, 1); break; }
                    case 2: { u16_count(bin, start+1, end-2, 1); break; }
                }
            }
            step = prev;
        }
    }
    else { /* search everywhere */
        switch (count_mode) {
            case 0: { u16_count(bin, 0, bin->dk->in.length-1, 0); break; }
            case 1: { u16_count(bin, 0, bin->dk->in.length-2, 1); break; }
            case 2: { u16_count(bin, 1, bin->dk->in.length-2, 1); break; }
        }
    }

    /* keep the most common cases */
    qsort(bin->lutc, 65536, sizeof(struct U16), sort_u16);

    for (i = 0, j = 0; j < 64; i++) {
        unsigned short w = bin->lutc[i].index;
        if (skip_rle && (w & 255) == (w >> 8))
            continue;
        lut[j++] = bin->lutc[i].index;
    }

    /* ascending by value */
    qsort(bin->lut, 64, sizeof(unsigned short), sort_us);

    return 0;
}



/* case 0: copy input */
static void test_case_0 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *dk = bin->dk;
    size_t j;
    struct PATH *step = &bin->steps[i];
    size_t limit = (64 < dk->in.length+1 - i)
                 ?  64 : dk->in.length+1 - i;

    /* test all subsequent nodes */
    for (j = 1; j < limit; j++) {
        struct PATH *next = &bin->steps[i+j];
        size_t used = step->used + 1 + j;
        if (next->used > used) {
            struct PATH p = { step, used, { 0, 0, j } };
            *next = p;
        }
    }
}

/* case 1: RLE */
static void test_case_1 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *dk = bin->dk;
    size_t j = 1;
    struct PATH *step = &bin->steps[i];
    size_t used = step->used + 2;
    size_t limit = (64 < dk->in.length - i)
                 ?  64 : dk->in.length - i;

    /* count how many bytes match */
    while (j < limit)
        if (dk->in.data[i] != dk->in.data[i+j++])
            break;

    /* test all subsequent nodes */
    while (j--) {
        struct PATH *next = &bin->steps[i+j];
        if (next->used > used) {
            struct PATH p = { step, used, { 0, 1, j } };
            *next = p;
        }
    }
}

/* case 2: copy output */
static void test_case_2 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *dk = bin->dk;
    size_t j = 0;
    struct PATH *step = &bin->steps[i];
    struct NCASE max = { 0,0,0 };
    size_t used = step->used + 3;
    size_t limit = (64 < dk->in.length - i)
                 ?  64 : dk->in.length - i;

    /* using a smaller window can result in a speedup */
    /*
    if (i > (1 << 12))
    j = i - (1 << 12) + 1;
    */

    /* find the longest match */
    for (; j < i; j++) {
        size_t match;
        for (match = 0; match < limit; match++)
            if (dk->in.data[i+match] != dk->in.data[j+match])
                break;
        if (max.count < match) {
            max.count = match;
            max.addr = j;
        }
        if (max.count == 63)
            break;
    }

    /* test all subsequent nodes */
    for (j = 2; j <= max.count; j++) {
        struct PATH *next = &bin->steps[i+j];
        if (next->used > used) {
            struct PATH p = { step, used, { max.addr, 2, j } };
            *next = p;
        }
    }
}

/* case 3: LUT containing 64x16-bit words */
static void test_case_3 (struct BIN *bin, size_t i) {
    struct COMPRESSOR *dk = bin->dk;
    struct PATH *step = &bin->steps[i];
    struct PATH *next = &bin->steps[i+2];
    unsigned short *match;
    unsigned short word = (dk->in.data[i+1] << 8) | dk->in.data[i];
    size_t used = step->used + 1;

    /* search for the current word in the LUT */
    match = bsearch(&word, bin->lut, 64, sizeof(unsigned short), sort_us);
    if (match == NULL)
        return;

    if (next->used > used) {
        struct PATH p = { step, used, { 0, 3, match - bin->lut } };
        *next = p;
    }
}

static void test_cases (struct BIN *bin, int use_lut) {
    size_t i;
    reset_steps(bin);
    for (i = 0; i < bin->dk->in.length-1; i++) {
        test_case_0(bin, i); /* copy */
        test_case_1(bin, i); /* RLE */
        test_case_2(bin, i); /* window */
        if (use_lut)
            test_case_3(bin, i); /* LUT */
    }
    test_case_0(bin, i);
}

static int run_case (struct BIN *bin, int n) {
    memset(bin->lut, 0, 64*sizeof(unsigned short));
    reset_steps(bin);

#define CASE_COUNT 13
    switch (n) {
        case 0: { /* no LUT */
            test_cases(bin, 0);
            break;
        }
        case 1:   /* interleaved counting */
        case 2:   /* odd counting */
        case 3: { /* even counting */
            lut_count (bin, n-1, 0, 0);
            test_cases(bin, 1);
            break;
        }
        case 4: /* same, but only counting copy cases */
        case 5:
        case 6: {
            test_cases (bin, 0);
            lut_count  (bin, n-4, 1, 0);
            reset_steps(bin);
            test_cases (bin, 1);
            break;
        }
        case 7:
        case 8:
        case 9: {
            lut_count (bin, n-7, 0, 1);
            test_cases(bin, 1);
            break;
        }
        case 10:
        case 11:
        case 12: {
            test_cases (bin, 0);
            lut_count  (bin, n-10, 1, 1);
            reset_steps(bin);
            test_cases (bin, 1);
            break;
        }
    }
    return 0;
}


/* traverse the path and write data */
static int write_data (struct BIN *bin) {
    struct COMPRESSOR *dk = bin->dk;
    struct PATH *step = bin->steps;
    int i;

    /* write the LUT */
    for (i = 0; i < 64; i++)
        if (write_byte(dk, bin->lut[i])
        ||  write_byte(dk, bin->lut[i] >> 8))
            return 1;

    /* encode the input data */
    while (step != &bin->steps[dk->in.length]) {
        struct PATH *next = step->link;
        struct NCASE *nc = &next->nc;

        /* control byte */
        if (write_byte(dk, (nc->mode << 6) | nc->count))
            return 1;

        /* data bytes */
        switch (nc->mode) {
            case 0: { /* copy */
                for (i = 0; i < nc->count; i++)
                    if (write_byte(dk, dk->in.data[dk->in.pos++]))
                        return 1;
                break;
            }
            case 1: { /* RLE */
                if (write_byte(dk, dk->in.data[dk->in.pos]))
                    return 1;
                dk->in.pos += nc->count;
                break;
            }
            case 2: { /* history */
                dk->in.pos += nc->count;
                if (write_byte(dk, nc->addr)
                ||  write_byte(dk, nc->addr >> 8))
                    return 1;
                break;
            }
            case 3: {
                dk->in.pos += 2;
                break;
            }
        }
        step = next;
    }
    return 0;
}


int dkcchr_compress (struct COMPRESSOR *dk) {
    struct BIN bin;
    size_t least_used_c = -1llu;
    int    least_used_n = 0;
    int i;
    bin.dk = dk;

    bin.steps = malloc(sizeof(struct PATH) * (dk->in.length+1));
    if (bin.steps == NULL) {
        dk_set_error("Failed to allocate memory for path");
        return 1;
    }
    bin.lutc = malloc(65536*sizeof(struct U16));
    if (bin.lutc == NULL) {
        dk_set_error("Failed to allocate memory for LUT counter");
        free(bin.steps);
        return 1;
    }

    /* try a number of strategies */
    if (0) {
        for (i = 0; i < CASE_COUNT; i++) {
            if (run_case(&bin, i))
                goto error;
            if (least_used_c > bin.steps[dk->in.length].used) {
                least_used_c = bin.steps[dk->in.length].used;
                least_used_n = i;
            }
        }
        /* stick with the best case */
        if (run_case(&bin, least_used_n))
            goto error;
    }
    else {
        /* strategy #2 tends to work best for tilesets */
        if (run_case(&bin, 2))
            goto error;
    }

    /* reverse path direction */
    reverse_path(&bin);

    /* write the output */
    if (write_data(&bin))
        goto error;

    free(bin.steps);
    free(bin.lutc);
    return 0;
error:
    free(bin.steps);
    free(bin.lutc);
    return 1;
}

