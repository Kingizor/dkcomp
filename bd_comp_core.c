#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

static int read_byte (struct COMPRESSOR *dk) {
    if (dk->in.pos >= dk->in.length) {
        dk_set_error("Tried to read out of bounds (input).");
        return -1;
    }
    return dk->in.data[dk->in.pos++];
}

static int write_nibble (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit) {
        dk_set_error("Tried to write out of bounds (output).");
        return 1;
    }
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


struct PATH {
    struct PATH *link;
    size_t used; /* smallest number of nibbles to get here */
    unsigned char ncase;  /* which case brought us here? */
    unsigned short arg; /* 9,10,11,12 */
};
struct BIN {
    struct COMPRESSOR *dk;
    struct PATH *steps;
};

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


static void clear_path (struct BIN *bin) {
    size_t i;
    for (i = 0; i <= bin->dk->in.length; i++) {
        static const struct PATH p = { NULL, -1llu, 0, 0 };
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

struct DATA_CONSTANT {
    unsigned short count;
    unsigned short index;
};

static int write_constants (
    int (*write)(struct COMPRESSOR*, unsigned),
    struct COMPRESSOR *dk,
    struct DATA_CONSTANT *buf,
    int count
) {
    int i;
    for (i = 0; i < count; i++) {
        if (write(dk, buf[i].index))
            return 1;
    }
    return 0;
}

static int sort_count (const void *aa, const void *bb) {
    const struct DATA_CONSTANT *a = aa, *b = bb;
    return (a->count < b->count) ?  1
         : (a->count > b->count) ? -1
         : (a->index < b->index); /* may affect compression ratio */
}
static void init_constants (struct DATA_CONSTANT *dc, unsigned count) {
    unsigned i;
    for (i = 0; i < count; i++) {
        dc[i].count = 0;
        dc[i].index = i;
    }
}

static int constants (struct COMPRESSOR *dk) {
    struct DATA_CONSTANT *lut_bytes, *lut_words, *rle_bytes;
    size_t i, j, consecutive = 0;
    lut_words = malloc((65536+256+256) * sizeof(struct DATA_CONSTANT));

    if (lut_words == NULL) {
        dk_set_error("Failed to allocate memory for constant table");
        return 1;
    }

    lut_bytes = lut_words+65536;
    rle_bytes = lut_bytes+256;

    init_constants(lut_words, 65536);
    init_constants(lut_bytes, 256);
    init_constants(rle_bytes, 256);

    /* simple counting */
    for (i = 0; i < dk->in.length; i++) {
        unsigned char *data = &dk->in.data[i];

        lut_bytes[data[0]].count++;

        if ((i+1) < dk->in.length) {
            lut_words[data[1] | (data[0] << 8)].count++;
        }

        if (i && data[0] == data[-1]) {
            if (consecutive++ >= 3)
                rle_bytes[data[0]].count++;
        }
        else {
            consecutive = 0;
        }
    }

    /* choose the most common */
    qsort(rle_bytes,   256, sizeof(struct DATA_CONSTANT), sort_count);
    qsort(lut_bytes,   256, sizeof(struct DATA_CONSTANT), sort_count);
    qsort(lut_words, 65536, sizeof(struct DATA_CONSTANT), sort_count);

    /* using a LUT word is equivalent to using two byte constants, */
    /* so by forbidding those combinations we get a few extra words */
    {
    unsigned short forbidden[] = {
        lut_bytes[0].index | (lut_bytes[0].index << 8),
        lut_bytes[0].index | (lut_bytes[1].index << 8),
        lut_bytes[1].index | (lut_bytes[0].index << 8),
        lut_bytes[1].index | (lut_bytes[1].index << 8)
    };
    int skip = 0;
    for (i = 0; i < 21; i++)
        for (j = 0; j < 4; j++)
            if (lut_words[i].index == forbidden[j]) {
                lut_words[i].count = 0;
                skip++;
                break;
            }
    if (skip)
        qsort(lut_words, 21, sizeof(struct DATA_CONSTANT), sort_count);
    }

    /* write to output buffer */
    if (write_byte(dk, 0)
    ||  write_constants(write_byte, dk, rle_bytes,  2)
    ||  write_constants(write_byte, dk, lut_bytes,  2)
    ||  write_constants(write_word, dk, lut_words, 17)) {
        free(lut_words);
        return 1;
    }

    free(lut_words);
    return 0;
}



static void test_rle (struct BIN *bin, size_t i) {
    struct COMPRESSOR *dk = bin->dk;
    struct PATH *step = &bin->steps[i];
    unsigned char *data = &dk->in.data[i];
    int ncase;
    size_t j = 0;
    size_t limit = 18;

    /* 3,4,5 (3-18 bytes RLE) */
    if (limit > dk->in.length-i-1)
        limit = dk->in.length-i-1;

    /* count matching bytes */
    while (++j < limit && dk->in.data[i] == dk->in.data[i+j]);

    while (j >= 3) {
        struct PATH *next = &step[j];
        struct PATH p = { step, 2+step->used, 3+in_rle(dk, *data), 0};
        if (p.ncase == 3)
            p.used += 2;
        if (next->used > p.used)
           *next = p;
        j--;
    }

    /* 7,8 (byte LUT) */
    if ((ncase = in_blut(dk, *data))
    &&  (1+step->used) < step[1].used) {
        struct PATH p = { step, 1+step->used, 4+ncase, 0 };
        step[1] = p;
    }

    /* 6  (word constant) */
    /* 15 (word LUT) */
    if ((i+1) < dk->in.length
    && (ncase = in_wlut(dk, *data|(data[1] << 8)))) {
        size_t used = 1 + step->used + 2*(ncase > 5);
        if (used < step[2].used) {
            struct PATH p = { step, used, (ncase > 5) ? 15:6, (ncase-7)/2 };
            step[2] = p;
        }
    }

    /* 13 (repeat byte) */
    if (i && data[-1] == *data)
        if ((1+step->used) < step[1].used) {
            struct PATH p = { step, 1+step->used, 13, 0 };
            step[1] = p;
        }

    /* 14 (repeat word) */
    if (i > 1
    && (i + 1) < dk->in.length
    && (data[-2] == *data
    &&  data[-1] ==  data[1]))
        if (step[2].used > 1+step->used) {
            struct PATH p = { step, 1+step->used, 14, 0 };
            step[2] = p;
        }
}

static void test_copy (struct BIN *bin, size_t i) {
    struct PATH *step = &bin->steps[i];
    struct PATH p = { step, 0, 0, 0 };
    size_t limit = bin->dk->in.length - i - 1;
    size_t j;
    if (limit > 16)
        limit = 16;

    /* 0 (copy 3-18 bytes) */
    for (j = 3; j < limit; j++) {
        p.used = 2+(2*j) + step->used;
        if (p.used < step[j].used)
            step[j] = p;
    }

    /* 1 (single byte) */
    p.used  = 3+step->used;
    p.ncase = 1;
    if (limit && p.used < step[1].used)
        step[1] = p;

    /* 2 (single word) */
    p.ncase = 2;
    p.used  = 5+step->used;
    if (limit > 1 && p.used < step[2].used)
        step[2] = p;
}

static void test_win (struct BIN *bin, size_t i) {
    struct PATH *step = &bin->steps[i];
    unsigned char *data = bin->dk->in.data;
    unsigned short count = 0;
    size_t j = 0;

    /* 9 (recent word) */
    if ((i+1) < bin->dk->in.length) {
        if (i >= 17)
            j = i - 17;

        for (; (j+1) < i; j+=2) {
            if ((2+step->used) > step[2].used)
                break;
            if (data[i] == data[j] && data[i+1] == data[j+1]) {
                struct PATH p = { step, 2+step->used, 9, .arg = i-j-2 };
                step[2] = p;
                break;
            }
        }
    }

    /* (3-18 bytes from 8/12/16 bit window) */

    /* input should never exceed 65536, so this check is unnecessary */
    j = 0;
    if (i > (1 << 16))
    j = i - (1 << 16);

    for (; j < i; j++) {
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

        while (matched > 3) {
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
            matched--;
        }
        if (matched == limit)
            break;
    }
}

static void test_cases (struct BIN *bin) {
    struct COMPRESSOR *dk = bin->dk;
    size_t i;
    for (i = 0; i < dk->in.length; i++) {
        test_rle (bin, i);
        test_copy(bin, i);
        test_win (bin, i);
    }
}



static int encode_case (struct BIN *bin, struct PATH *step, struct PATH *next) {

    struct COMPRESSOR *dk = bin->dk;
    int len = next - step;
    int z;

    /* write case */
    if (write_nibble(dk, next->ncase))
        return 1;

    switch (next->ncase) {

        /* n bytes */
        case 0: {
            if (write_nibble(dk, len))
                return 1;
            while (len--)
                if ((z = read_byte(dk)) < 0
                ||  write_byte(dk, z))
                    return 1;
            break;
        }

        /* 2 bytes or 1 byte */
        case 2: {
            if ((z = read_byte(dk)) < 0
            ||   write_byte(dk, z))
                return 1;
        } /* Fallthrough */
        case 1: {
            if ((z = read_byte(dk)) < 0
            ||   write_byte(dk, z))
                return 1;
            break;
        }

        /* RLE input (3-18) */
        case 3: {
            if (write_nibble (dk, len - 3)
            || (z = read_byte(dk)) < 0
            ||  write_byte   (dk, z))
                return 1;
            dk->in.pos += len-1;
            break;
        }

        /* RLE constant (3-18) */
        case 4: case 5: {
            if (write_nibble(dk, len - 3))
                return 1;
            dk->in.pos += len;
            break;
        }

        /* Single constants or repeats */
        case  7: case  8: case 13: { dk->in.pos += 1; break; }
        case  6: case 14:          { dk->in.pos += 2; break; }

        /* Word window */
        case 9: {
            if (write_nibble(dk, next->arg))
                return 1;
            dk->in.pos += 2;
            break;
        }

        /* 8-bit window */
        case 10: {
            if (write_nibble(dk, len - 3)
            ||  write_byte  (dk, next->arg))
                return 1;
            dk->in.pos += len;
            break;
        }

        /* 12-bit window */
        case 11: {
            if (write_nibble(dk, len - 3)
            ||  write_byte  (dk, next->arg >> 4)
            ||  write_nibble(dk, next->arg & 15))
                return 1;
            dk->in.pos += len;
            break;
        }

        /* 16-bit window */
        case 12: {
            if (write_nibble(dk, len - 3)
            ||  write_word  (dk, next->arg))
                return 1;
            dk->in.pos += len;
            break;
        }

        /* Word LUT */
        case 15: {
            if (write_nibble(dk, next->arg))
                return 1;
            dk->in.pos += 2;
            break;
        }
    }
    return 0;
}



static int write_output (struct BIN *bin) {
    struct PATH *step = bin->steps;
    while (step != &bin->steps[bin->dk->in.length]) {
        if (encode_case(bin, step, step->link))
            return 1;
        step = step->link;
    }
    if (write_byte(bin->dk, 0)
    || (bin->dk->out.bitpos && write_nibble(bin->dk, 0)))
        return 1;
    return 0;
}

int bd_compress (struct COMPRESSOR *dk) {
    struct PATH *steps = malloc((dk->in.length+1) * sizeof(struct PATH));
    struct BIN bin = { dk, steps };
    if (steps == NULL) {
        dk_set_error("Failed to allocate memory for path");
        return 1;
    }
    clear_path(&bin);
    if (constants(bin.dk)) {
        free(steps);
        return 1;
    }
    test_cases  (&bin);
    reverse_path(&bin);
    if (write_output(&bin)) {
        free(steps);
        return 1;
    }

    free(steps);
    return 0;
}

