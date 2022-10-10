/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2022 Kingizor
 * dkcomp library - SNES DKC2/DKC3 big data compressor */

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

/* use the better constant picking function */
#define COMPLEX_CONSTANTS


/* I/O functions */

static int read_byte (struct COMPRESSOR *dk) {
    if (dk->in.pos >= dk->in.length)
        return -1;
    return dk->in.data[dk->in.pos++];
}
static int write_nibble (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit)
        return 1;
    if (!dk->out.bitpos)
        dk->out.data[dk->out.pos  ]  = val << 4;
    else
        dk->out.data[dk->out.pos++] |= val & 15;
    dk->out.bitpos ^= 4;
    return 0;
}
static int write_byte (struct COMPRESSOR *dk, unsigned val) {
    return write_nibble(dk, (val >> 4))
        || write_nibble(dk,  val);
}
static int write_word (struct COMPRESSOR *dk, unsigned val) {
    return write_byte(dk, (val >> 8))
        || write_byte(dk,  val);
}



/* some data structures */

struct PATH {
    struct PATH *link;
    size_t used; /* smallest number of nibbles to get here */
    unsigned char ncase;  /* which case brought us here? */
    unsigned short arg; /* 9,10,11,12,15 */
};
struct BIN {
    struct COMPRESSOR *dk;
    struct PATH *steps;
};


/* path shenanigans */

static void clear_path (struct BIN *bin) {
    size_t i;
    for (i = 0; i <= bin->dk->in.length; i++) {
        static const struct PATH p = { NULL, (size_t)-1, 0, 0 };
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




/* constant search functions */

static int in_rle (struct COMPRESSOR *dk, unsigned char val) {
    int i;
    for (i = 1; i <= 2; i++)
        if (dk->out.data[i] == val)
            return i;
    return 0;
}
static int in_blut (struct COMPRESSOR *dk, unsigned char val) {
    int i;
    for (i = 3; i <= 4; i++)
        if (dk->out.data[i] == val)
            return i;
    return 0;
}
static int in_wlut (struct COMPRESSOR *dk, unsigned short val) {
    int i;
    for (i = 0; i < 17; i++)
        if (val == (dk->out.data[5+2*i]|(dk->out.data[6+2*i] << 8)))
            return 5+2*i;
    return 0;
}




/* case scanning */

static void test_rle (struct BIN *bin, size_t i) {
    unsigned char *data = &bin->dk->in.data[i];
    struct PATH *step = &bin->steps[i];
    size_t j = 0;
    size_t limit = 18;

    /* 3,4,5 (3-18 bytes RLE) */
    if (limit > bin->dk->in.length-i)
        limit = bin->dk->in.length-i;

    /* count matching bytes */
    while (++j < limit && data[0] == data[j]);

    while (j >= 3) {
        struct PATH *next = &step[j];
        struct PATH p = { step, 2+step->used, 3+in_rle(bin->dk, data[0]), 0};
        if (p.ncase == 3)
            p.used += 2;
        if (next->used > p.used)
           *next = p;
        j--;
    }
}

static void test_constants (struct BIN *bin, size_t i) {
    unsigned char *data = &bin->dk->in.data[i];
    struct PATH *step = &bin->steps[i];
    int ncase;

    /* 7,8 (byte LUT) */
    if ((ncase = in_blut(bin->dk, data[0]))
    &&  step[1].used > (1 + step->used)) {
        struct PATH p = { step, 1+step->used, 4+ncase, 0 };
        step[1] = p;
    }

    /* 6  (word constant) */
    /* 15 (word LUT) */
    if ((i+1) < bin->dk->in.length
    && (ncase = in_wlut(bin->dk, data[0] | (data[1] << 8)))) {
        size_t used = 1 + step->used + (ncase > 5);
        if (step[2].used > used) {
            struct PATH p = { step, used, (ncase > 5) ? 15:6, (ncase-7)/2 };
            step[2] = p;
        }
    }

}

static void test_repeat (struct BIN *bin, size_t i) {
    struct COMPRESSOR *dk = bin->dk;
    unsigned char *data = &bin->dk->in.data[i];
    struct PATH *step = &bin->steps[i];
    struct PATH p = { step, 1+step->used, 13, 0 };

    /* 13 (repeat byte) */
    if (i && data[-1] == data[0]
    &&  step[1].used > p.used)
        step[1] = p;

    /* 14 (repeat word) */
    p.ncase++;
    if (i > 1
    && (i + 1) < dk->in.length
    &&  data[-2] == data[0]
    &&  data[-1] == data[1]
    &&  step[2].used > p.used)
        step[2] = p;

}

static void test_copy (struct BIN *bin, size_t i) {
    struct PATH *step = &bin->steps[i];
    struct PATH p = { step, 0, 0, 0 };
    size_t limit = bin->dk->in.length - i;
    size_t j;
    if (limit > 16)
        limit = 16;

    /* 0 (copy 3-18 bytes) */
    for (j = 3; j < limit; j++) {
        p.used = 2+(2*j) + step->used;
        if (step[j].used > p.used)
            step[j] = p;
    }

    /* 1 (single byte) */
    p.ncase = 1;
    p.used  = 3+step->used;
    if (limit
    &&  step[1].used > p.used)
        step[1] = p;

    /* 2 (single word) */
    p.ncase = 2;
    p.used  = 5+step->used;
    if (limit > 1
    &&  step[2].used > p.used)
        step[2] = p;
}

static void test_win (struct BIN *bin, size_t i) {
    struct PATH *step = &bin->steps[i];
    unsigned char *data = bin->dk->in.data;
    unsigned short count = 0;
    size_t  j = 0;
    size_t lo = 0;

    /* 9 (recent word) */
    if ((i+1) < bin->dk->in.length) {
        if (i > 17)
            j = i - 17;

        for (; (j+1) < i; j++) {
            struct PATH p = { step, 2+step->used, 9, .arg = i-j-2 };
            if (step[2].used <= p.used)
                break;
            if (data[i] == data[j] && data[i+1] == data[j+1]) {
                step[2] = p;
                break;
            }
        }
    }

    /* (3-18 bytes from 8/12/16 bit window) */
    if (i < 3)
        return;

    /* input size should never be this large anyway */
    if  (i > (1 << 16))
    lo = i - (1 << 16);

    for (j = i-3; j > lo && j < i; j--) {
        unsigned char *a = &data[i];
        unsigned char *b = &data[j];
        size_t limit   = 18;
        size_t matched = 0;
        if (limit > i-j)
            limit = i-j;
        if (limit > bin->dk->in.length-i)
            limit = bin->dk->in.length-i;

        for (matched = 0; matched < limit; matched++)
            if (*a++ != *b++)
                break;

        if (count >= matched)
            continue;
        count = matched;

        for (; matched >= 3; matched--) {
            struct PATH p = { step, step->used, 10, 0 };
            size_t pos = i - j;
            if (pos < (256+matched)) {
                p.ncase = 10;
                p.used += 4;
                p.arg   = i - (j + matched);
            }
            else if (pos > 258 && pos <= (4095+259)) {
                p.ncase = 11;
                p.used += 5;
                p.arg   = i - (j + 0x103);
            }
            else {
                p.ncase = 12;
                p.used += 6;
                p.arg   = i - j;
            }
            if (p.used < step[matched].used)
                step[matched] = p;
        }
        if (matched == limit)
            break;
    }
}

static void test_cases (struct BIN *bin) {
    struct COMPRESSOR *dk = bin->dk;
    size_t i;
    for (i = 0; i < dk->in.length; i++) {
        test_constants(bin, i);
        test_repeat   (bin, i);
        test_copy     (bin, i);
        test_win      (bin, i);
        test_rle      (bin, i);
    }
}

#if defined(COMPLEX_CONSTANTS)
static void test_nc_cases (struct BIN *bin) {
    struct COMPRESSOR *dk = bin->dk;
    size_t i;
    for (i = 0; i < dk->in.length; i++) {
        test_repeat(bin, i);
        test_copy  (bin, i);
        test_win   (bin, i);
        test_rle   (bin, i);
    }
}
#endif




/* constant scanning */

struct DATA_CONSTANT {
    unsigned short count;
    unsigned short index;
};
struct CLUT {
    struct DATA_CONSTANT *rle;
    struct DATA_CONSTANT *byte;
    struct DATA_CONSTANT *word;
};

static int write_constants (struct COMPRESSOR *dk, struct CLUT *clut) {
    int i;
    if (write_byte(dk, 0))
        return 1;
    for (i = 0; i < 2; i++)
        if (write_byte(dk, clut->rle[i].index))
            return 1;
    for (i = 0; i < 2; i++)
        if (write_byte(dk, clut->byte[i].index))
            return 1;
    for (i = 0; i < 17; i++)
        if (write_word(dk, clut->word[i].index))
            return 1;
    return 0;
}

static int sort_count (const void *aa, const void *bb) {
    const struct DATA_CONSTANT *a = aa, *b = bb;
    return (a->count < b->count) ?  1
         : (a->count > b->count) ? -1
         : (a->index < b->index) ?  1
         : (a->index > b->index); /* may affect compression ratio */
}

static void init_constants (struct DATA_CONSTANT *dc, unsigned count) {
    unsigned i;
    for (i = 0; i < count; i++) {
        dc[i].count = 0;
        dc[i].index = i;
    }
}
static int init_constant_lut (struct CLUT *clut) {
    clut->rle = malloc((65536+256+256) * sizeof(struct DATA_CONSTANT));
    if (clut->rle == NULL)
        return DK_ERROR_ALLOC;
    clut->byte = clut-> rle    + 256;
    clut->word = clut->byte    + 256;
    init_constants(clut-> rle,   256);
    init_constants(clut->byte,   256);
    init_constants(clut->word, 65536);
    return 0;
}

/* count most frequent bytes/words/rle in a given range  */
static void constant_count_single (
    struct COMPRESSOR *dk,
    struct CLUT *clut,
    size_t a,
    size_t b
) {
    for (; a < b; a++) {
        unsigned char *data = &dk->in.data[a];
        clut->byte[data[0]].count++;
        if ((a+1) < b)
            clut->word[data[1]|(data[0] << 8)].count++;
    }
}
static void constant_count_rle (
    struct COMPRESSOR *dk,
    struct CLUT *clut,
    size_t a,
    size_t b
) {
    int consecutive = 0;
    for (; a < b; a++) {
        unsigned char *data = &dk->in.data[a];
        if (a && data[0] == data[-1]) {
            if (consecutive++ >= 3)
                clut->rle[data[0]].count++;
        }
        else {
            consecutive = 0;
        }
    }
}

/* using a LUT word is equivalent to using two byte constants, */
/* so by forbidding those combinations we get a few extra words */
static void filter_constants (struct CLUT *clut) {

    unsigned short forbidden[4];
    int i,j, skip = 0;

    /* choose the most common */
    qsort(clut-> rle,   256, sizeof(struct DATA_CONSTANT), sort_count);
    qsort(clut->byte,   256, sizeof(struct DATA_CONSTANT), sort_count);
    qsort(clut->word, 65536, sizeof(struct DATA_CONSTANT), sort_count);

    for (i = 0; i < 4; i++)
        forbidden[i] =  clut->byte[i >> 1].index
                     | (clut->byte[i  & 1].index << 8);

    for (i = 0; i < 21; i++)
        for (j = 0; j < 4; j++)
            if (clut->word[i].index == forbidden[j]) {
                clut->word[i].count  = 0;
                skip++;
                break;
            }
    if (skip)
        qsort(clut->word, 21, sizeof(struct DATA_CONSTANT), sort_count);
}


/* we have two methods for picking constants: */
/*  simple - iterate over all of the input data */
/* complex - iterate over data that can only be handled by copy cases */
/* the latter is slower but typically results in better compression */

#if defined(COMPLEX_CONSTANTS)

#define choose_constants complex_constants

static int complex_constants (struct BIN *bin) {
    struct PATH *step = bin->steps;
    struct CLUT clut;
    enum DK_ERROR e;

    if ((e = init_constant_lut(&clut)))
        return e;

    /* maybe better to do RLE all-at-once first? */
    constant_count_rle(bin->dk, &clut, 0, bin->dk->in.length);
    qsort(clut.rle, 256, sizeof(struct DATA_CONSTANT), sort_count);
    bin->dk->out.data[1] = clut.rle[0].index;
    bin->dk->out.data[2] = clut.rle[1].index;

    test_nc_cases(bin);
    reverse_path (bin);

    /* only count areas that aren't covered by better cases */
    while (step != &bin->steps[bin->dk->in.length]) {
        struct PATH *next = step->link;

        switch (next->ncase) {
            case 0: case 1: case 2: {
                constant_count_single(
                    bin->dk, &clut,
                    step - bin->steps,
                    next - bin->steps
                );
                break;
            }
        }
        step = next;
    }
    clear_path(bin);
    filter_constants(&clut);
    if (write_constants(bin->dk, &clut)) {
        free(clut.rle);
        return DK_ERROR_OOB_OUTPUT_W;
    }
    free(clut.rle);
    return 0;
}

#else

#define choose_constants simple_constants

static int simple_constants (struct BIN *bin) {
    struct CLUT clut;
    enum DK_ERROR e;
    if ((e = init_constant_lut(&clut)))
        return e;
    constant_count_rle   (bin->dk, &clut, 0, bin->dk->in.length);
    constant_count_single(bin->dk, &clut, 0, bin->dk->in.length);
    filter_constants(&clut);
    if (write_constants(bin->dk, &clut)) {
        free(clut.rle);
        return DK_ERROR_OOB_OUTPUT_W;
    }
    free(clut.rle);
    return 0;
}

#endif



/* data encoding */

static int encode_case (struct BIN *bin, struct PATH *step, struct PATH *next) {

    struct COMPRESSOR *dk = bin->dk;
    int len = next - step;
    int z;

    /* write case */
    if (write_nibble(dk, next->ncase))
        return DK_ERROR_OOB_OUTPUT_W;

    switch (next->ncase) {

        /* n bytes */
        case 0: {
            if (write_nibble(dk, len))
                return DK_ERROR_OOB_OUTPUT_W;
            while (len--) {
                if ((z = read_byte(dk)) < 0)
                    return DK_ERROR_OOB_INPUT;
                if (write_byte(dk, z))
                    return DK_ERROR_OOB_OUTPUT_W;
            }
            break;
        }

        /* 2 bytes or 1 byte */
        case 2: {
            if ((z = read_byte(dk)) < 0)
                return DK_ERROR_OOB_INPUT;
            if (write_byte(dk, z))
                return DK_ERROR_OOB_OUTPUT_W;
        } /* Fallthrough */
        case 1: {
            if ((z = read_byte(dk)) < 0)
                return DK_ERROR_OOB_INPUT;
            if (write_byte(dk, z))
                return DK_ERROR_OOB_OUTPUT_W;
            break;
        }

        /* RLE input (3-18) */
        case 3: {
            if (write_nibble (dk, len - 3))
                return DK_ERROR_OOB_OUTPUT_W;
            if ((z = read_byte(dk)) < 0)
                return DK_ERROR_OOB_INPUT;
            if (write_byte   (dk, z))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += len-1;
            break;
        }

        /* RLE constant (3-18) */
        case 4: case 5: {
            if (write_nibble(dk, len - 3))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += len;
            break;
        }

        /* Single constants or repeats */
        case  7: case  8: case 13: { dk->in.pos += 1; break; }
        case  6: case 14:          { dk->in.pos += 2; break; }

        /* Word window */
        case 9: {
            if (write_nibble(dk, next->arg))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += 2;
            break;
        }

        /* 8-bit window */
        case 10: {
            if (write_nibble(dk, len - 3)
            ||  write_byte  (dk, next->arg))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += len;
            break;
        }

        /* 12-bit window */
        case 11: {
            if (write_nibble(dk, len - 3)
            ||  write_byte  (dk, next->arg >> 4)
            ||  write_nibble(dk, next->arg & 15))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += len;
            break;
        }

        /* 16-bit window */
        case 12: {
            if (write_nibble(dk, len - 3)
            ||  write_word  (dk, next->arg))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += len;
            break;
        }

        /* Word LUT */
        case 15: {
            if (write_nibble(dk, next->arg))
                return DK_ERROR_OOB_OUTPUT_W;
            dk->in.pos += 2;
            break;
        }
    }
    return 0;
}

static int write_output (struct BIN *bin) {
    struct PATH *step = bin->steps;
    enum DK_ERROR e;
    while (step != &bin->steps[bin->dk->in.length]) {
        if ((e = encode_case(bin, step, step->link)))
            return e;
        step = step->link;
    }
    if (write_byte(bin->dk, 0)
    || (bin->dk->out.bitpos && write_nibble(bin->dk, 0)))
        return DK_ERROR_OOB_OUTPUT_W;
    return 0;
}


int bd_compress (struct COMPRESSOR *dk) {
    struct PATH *steps = malloc((dk->in.length+1) * sizeof(struct PATH));
    struct BIN bin = { dk, steps };
    enum DK_ERROR e;

    if (steps == NULL)
        return DK_ERROR_ALLOC;

    clear_path(&bin);

    if ((e = choose_constants(&bin))) {
        free(steps);
        return e;
    }

    test_cases  (&bin);
    reverse_path(&bin);

    if ((e = write_output(&bin))) {
        free(steps);
        return e;
    }

    free(steps);
    return 0;
}

