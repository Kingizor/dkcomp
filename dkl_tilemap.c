/* SPDX-License-Identifier: MIT
 * Copyright (c) 2021-2022 Kingizor
 * dkcomp library - DKL layout compressor and decompressor */

/* note: this compression scheme is imperfect in that there are some
 * combinations of data that it cannot represent. */

#include <stdlib.h>
#include "dk_internal.h"

static int read_nibble_z (struct COMPRESSOR *dk) {
    unsigned char z;
    if (dk->in.pos >= dk->in.length)
        return -1;
    z = (dk->in.data[dk->in.pos] >> dk->in.bitpos) & 15;
    if (!dk->in.bitpos)
        dk->in.pos++;
    dk->in.bitpos ^= 4;
    return z;
}

static int read_byte_z (struct COMPRESSOR *dk) {
    int n1, n2;
    if ((n1 = read_nibble_z(dk)) < 0
    ||  (n2 = read_nibble_z(dk)) < 0)
        return -1;
    return n2 | (n1 << 4);
}

static int write_nibble_z (struct COMPRESSOR *dk, unsigned char val) {
    if (dk->out.pos >= dk->out.limit)
        return 1;
    if (dk->out.bitpos)
        dk->out.data[dk->out.pos  ] |= (val & 15) << 4;
    else
        dk->out.data[dk->out.pos++] |= (val & 15);
    dk->out.bitpos ^= 4;
    return 0;
}

static int write_byte_z (struct COMPRESSOR *dk, unsigned char val) {
    if (write_nibble_z(dk, val >> 4)
    ||  write_nibble_z(dk, val & 15))
        return 1;
    return 0;
}

/* these are used often enough that macro versions keep the code a bit less cluttered */
#define  read_nibble(X) if ((X = read_nibble_z(dk)) < 0) return DK_ERROR_OOB_INPUT;
#define    read_byte(X) if ((X =   read_byte_z(dk)) < 0) return DK_ERROR_OOB_INPUT;
#define   write_byte(X) if (      write_byte_z(dk, X))   return DK_ERROR_OOB_OUTPUT_W;
#define write_nibble(X) if (    write_nibble_z(dk, X))   return DK_ERROR_OOB_OUTPUT_W;

int dkl_decompress (struct COMPRESSOR *dk) {

    int quit = 0;
    dk-> in.bitpos = 4; /* high nibble first */
    dk->out.bitpos = 4;

    for (;;) {
        int a,b,n;
        read_nibble(a);
        switch (a) {
            default: {
                read_nibble(b);
                if (a < 11 || b < 14) { /* write a byte */
                    write_byte((a << 4) | b);
                }
                else if (b == 14) { /* write increments, 3 + 0..15 times */
                    read_byte(a);
                    read_nibble(n);
                    n += 3;
                    while (n--)
                        write_byte(a++);
                }
                else { /* n == 15, write a word, 2 + 0..15 times */
                    read_byte(a);
                    read_byte(b);
                    read_nibble(n);
                    n += 2;
                    while (n--) {
                        write_byte(a);
                        write_byte(b);
                    }
                }
                break;
            }
            case 12: { /* copy data from output, 4 + 0..251 times */
                read_byte(b);
                if (b & 1) {
                    read_nibble(a);
                    b |= a << 8;
                }
                b >>= 1;
                read_nibble(n);
                if (n == 15)
                    read_byte(n);
                n += 4;
                if (n  > 255)
                    n -= 256; /* values greater than 251 will overflow */
                while (n--) {
                    size_t addr = dk->out.pos - b - 1;
                    if (addr >= dk->out.pos)
                        return DK_ERROR_OOB_OUTPUT_R;
                    write_byte(dk->out.data[addr]);
                }
                break;
            }
            case 13: { /* write nibbles, 19 + 0..255 times */
                read_nibble(a);
                a <<= 4;
                read_byte(n);
                n += 20; /* no overflow */
                while (n--) {
                    read_nibble(b);
                    write_byte(a|b);
                }
                break;
            }
            case 14: { /* write nibbles, 4 + 0..15 times */
                read_nibble(a);
                if (a == 14) { /* quit */
                    quit = 1;
                    break;
                }
                a <<= 4;
                read_nibble(n);
                n += 4;
                while (n--) {
                    read_nibble(b);
                    write_byte(a|b);
                }
                break;
            }
            case 15: { /* write a byte, 3..138 times */
                read_byte(b);
                read_nibble(n);
                if (n & 8) {
                    read_nibble(a);
                    n = (a | ((n & 7) << 4)) + 8;
                }
                n += 3;
                while (n--)
                    write_byte(b);
                break;
            }
        }
        if (quit)
            break;
    }
    return 0;
}











/* compressor */

struct PATH {
    struct PATH *link;
    size_t used;         /* smallest number of nibbles to get here */
    unsigned char ncase; /* which case brought us here? */
    unsigned arg;        /* some cases need extra data */
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
    bin->steps[0].link = bin->steps; /* non-NULL */
}
static int reverse_path (struct BIN *bin) {
    struct PATH *prev = &bin->steps[bin->dk->in.length];
    struct PATH *step = prev->link;
    bin->steps[0].link = NULL;
    while (step != NULL) {
        struct PATH *next = step->link;
        step->link = prev;
        prev = step;
        step = next;
    }

    /* notify if a path from beginning to end can't be found */
    if (prev != bin->steps)
        return DK_ERROR_BAD_FORMAT;
    return 0;
}



/* write a byte once */
static void test_single (struct BIN *bin, size_t pos) { /* 0..11:13 */
    struct PATH *step = &bin->steps[pos];
    struct PATH p = { step, 2+step->used, 9, 0 };
    unsigned char c = bin->dk->in.data[pos];
    if ((c & 0xC0) == 0xC0
    &&  (c & 0x0F) >= 0x0E)
        return;
    if (step[1].used > p.used)
        step[1] = p;
}

/* incrementing values */
static void test_incs (struct BIN *bin, size_t pos) { /* 11:14 */
    struct PATH   *step = &bin->steps[pos];
    unsigned char *data = &bin->dk->in.data[pos];
    struct PATH p = { step, 5+step->used, 10, 0 };
    size_t i, limit = 17;
    if (limit > bin->dk->in.length - pos)
        limit = bin->dk->in.length - pos;
    for (i = 0; i < limit-1; i++)
        if (data[i]+1 != data[i+1])
            break;
    limit = i+1;
    for (i = 3; i <= limit; i++)
        if (step[i].used > p.used)
            step[i] = p;
}

/* repeating word */
static void test_words (struct BIN *bin, size_t pos) { /* 11:15 */
    struct PATH   *step = &bin->steps[pos];
    unsigned char *data = &bin->dk->in.data[pos];
    struct PATH p = { step, 7+step->used, 11, 0 };
    size_t i, limit  = 35;
    if (limit > bin->dk->in.length - pos)
        limit = bin->dk->in.length - pos;
    for (i = 2; i < limit-1; i+=2)
        if (data[i  ] != data[0]
        ||  data[i+1] != data[1])
            break;
    limit = i;
    for (i = 4; i <= limit; i+=2)
        if (step[i].used > p.used)
            step[i] = p;
}

/* repeating byte */
static void test_repeat (struct BIN *bin, size_t pos) { /* 15 */
    struct PATH   *step = &bin->steps[pos];
    unsigned char *data = &bin->dk->in.data[pos];
    struct PATH p = { step, 4+step->used, 15, 0 };
    size_t i, limit = 138;
    if (limit > bin->dk->in.length - pos)
        limit = bin->dk->in.length - pos;
    for (i = 1; i < limit; i++)
        if (data[0] != data[i])
            break;
    limit = i;
    for (i =  3; i < 11 && i <= limit; i++)
        if (step[i].used > p.used)
            step[i] = p;
    p.used++;
    for (i = 11; i <= limit; i++)
        if (step[i].used > p.used)
            step[i] = p;
}

/* we've seen this data before! */
static void test_win (struct BIN *bin, size_t pos) { /* 12 */
    struct PATH   *step = &bin->steps[pos];
    unsigned char *data = &bin->dk->in.data[pos];
    struct PATH p = { step, 0, 12, 0 };
    size_t j, i = (pos > 2047) ? (pos - 2047) : 0;
    size_t limit = 255; /* can't copy more than (251+4 = 255) bytes */
    struct MATCH {
        size_t size;
        size_t addr;
    } m[4];

    for (j = 0; j < 4; j++)
        m[j].size = m[j].addr = 0;

    if (limit > bin->dk->in.length - pos)
        limit = bin->dk->in.length - pos;

    for (; i < pos; i++) { /* i = output position */
        size_t match;
        struct MATCH *mm = m;
        for (match = 0; match < limit; match++)
            if (bin->dk->in.data[i+match] != data[match])
                break;
        if (match   >  18) mm += 1;
        if ((pos-i) > 127) mm += 2;
        if (mm->size < match) {
            mm->size = match;
            mm->addr = i;
        }
        if (match == 255)
            break;
    }

    for (i = 0; i < 4; i++) {
        for (j = 4; j <= m[i].size; j++) {
            p.used = step->used
                   + 4
                   + ((pos - m[i].addr) > 127) /* +1 if distance > 127 */
                   + ((j > 18) << 1);          /* +2 if match > 18 */
            p.arg  = pos - m[i].addr - 1;
            if (step[j].used > p.used)
                step[j] = p;
        }
    }
}

/* upper nibbles are all the same */
static void test_nibble (struct BIN *bin, size_t pos) { /* 13, 14 */
    struct PATH   *step = &bin->steps[pos];
    unsigned char *data = &bin->dk->in.data[pos];
    struct PATH p = { step, 0, 13, 0 };
    size_t i, limit = 255+20;

    if (limit > bin->dk->in.length - pos)
        limit = bin->dk->in.length - pos;

    for (i = 1; i < limit; i++)
        if ((data[0] & 0xF0) != (data[i] & 0xF0))
            break;

    limit = i+1;
    if (limit > bin->dk->in.length - pos)
        limit = bin->dk->in.length - pos;
    for (i = 4; i < limit; i++) {
        /* case 14 (the short variant) can't use 14 as the upper nibble */
        /* because 14:14 is the quit command */
        if (i < 20 && (data[i] & 0xF0) == 0xE0)
            continue;

        p.used  = step->used + 3 + i + !(i < 20);
        p.ncase = 13 + (i < 20);
        if (step[i].used > p.used)
            step[i] = p;
    }
}

static void test_cases (struct BIN *bin) {
    size_t i;
    for (i = 0; i < bin->dk->in.length; i++) {
        /* skip the current position if it can't be reached */
        if (bin->steps[i].link == NULL)
            continue;
        test_single(bin, i);
        test_incs  (bin, i);
        test_words (bin, i);
        test_repeat(bin, i);
        test_win   (bin, i);
        test_nibble(bin, i);
    }
}

static int encode_case (struct BIN *bin, struct PATH *step, struct PATH *next) {
    struct COMPRESSOR *dk = bin->dk;

    switch (next->ncase) {
        case 9: { /* single byte */
            unsigned char c = dk->in.data[step - bin->steps];
            write_nibble(c >> 4); /* hi */
            write_nibble(c & 15); /* lo */
            break;
        }
        case 10: { /* incrementing data */
            write_nibble(11); /* cmd #1 */
            write_nibble(14); /* cmd #2 */
            write_byte  (dk->in.data[step - bin->steps]); /* first */
            write_nibble(next - step - 3); /* count */
            break;
        }
        case 11: { /* repeating word */
            write_nibble(11); /* cmd #1 */
            write_nibble(15); /* cmd #2 */
            write_byte  (dk->in.data[  step-bin->steps]); /* lo */
            write_byte  (dk->in.data[1+step-bin->steps]); /* hi */
            write_nibble((next - step)/2 - 2); /* count */
            break;
        }
        case 12: { /* history window */
            size_t dist  = next->arg;
            size_t match = next - step;
            write_nibble(12); /* cmd */
            write_byte  ((dist << 1)|(dist > 127));  /* 0...6 */
            if (dist > 127)
                write_nibble(dist >> 7); /* 7..11 */
            if (match > 18) { /* count */
                write_nibble(15);
                write_byte(match - 4);
            }
            else 
                write_nibble(match - 4);
            break;
        }
        case 13: { /* lower nibbles (long) */
            size_t count = next - step;
            write_nibble(13);
            write_nibble(dk->in.data[step-bin->steps] >> 4);
            write_byte  (count - 20);
            while (count--)
                write_nibble(dk->in.data[next-count-bin->steps-1]);
            break;
        }
        case 14: { /* lower nibbles (short) */
            size_t count = next - step;
            write_nibble(14);
            write_nibble(dk->in.data[step-bin->steps] >> 4);
            write_nibble(count - 4);
            while (count--)
                write_nibble(dk->in.data[next-count-bin->steps-1]);
            break;
        }
        case 15: { /* repeating byte */
            size_t count = next - step;
            write_nibble(15);
            write_byte  (dk->in.data[step - bin->steps]);
            if (count > 10) {
                count -= 8;
                write_nibble(((count - 3) >> 4) | 8);
            }
            write_nibble(count - 3);
            break;
        }
    }
    return 0;
}


static int write_output (struct BIN *bin) {
    struct COMPRESSOR *dk = bin->dk;
    struct PATH *step = bin->steps;
    enum DK_ERROR e;
    while (step != &bin->steps[bin->dk->in.length]) {
        if ((e = encode_case(bin, step, step->link)))
            return e;
        step = step->link;
    }
    /* quit command */
    write_nibble(14);
    write_nibble(14);
    if (!bin->dk->out.bitpos)
        write_nibble(0);
    return 0;
}

int dkl_compress (struct COMPRESSOR *dk) {
    struct PATH *steps = malloc((dk->in.length+1) * sizeof(struct PATH));
    struct BIN bin = { dk, steps };
    enum DK_ERROR e;
    dk->out.bitpos = 4;

    if (steps == NULL)
        return DK_ERROR_ALLOC;

    clear_path(&bin);
    test_cases(&bin);

    if ((e = reverse_path(&bin))
    ||  (e = write_output(&bin))) {
        free(steps);
        return e;
    }
    free(steps);
    return 0;
}

