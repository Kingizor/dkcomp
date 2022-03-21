/* SPDX-License-Identifier: MIT
 * Big Data Compression Library
 * Copyright (c) 2020-2021 Kingizor */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
#include <setjmp.h>
#include "dk_internal.h"

static jmp_buf error_handler;

/* Certain cases have parameters ([a]ddr, [c]ount, etc) */
/* If a relevant case is active, choose_cases will set appropriate values */
struct CASE_WINDOW {
    unsigned short addr;
    unsigned char count;
};
struct CASE_PARAMS {
    struct CASE_WINDOW w[3]; /* 10, 11, 12 */
    unsigned short n;        /*  0 */
    unsigned char rle[3];    /*  3,  4,  5 */
    unsigned char wwin;      /*  9 */
    unsigned char wlut;      /* 15 */
};
struct NODES {
    struct CASE_PARAMS cp;
    struct NODES *prev; /* Which node brought us here */
    struct NODES *next; /* Which node brought us here */
    double ratio;       /* Accumulative ratio of all steps taken so far */
    unsigned cases;     /* Which cases are possible for this node? */
    unsigned char c;    /* Which case brought us here? */
};

struct BYTELOC {
    struct BYTELOC *next;
    int index;
};


/* Read/Write functions */

static int rb (struct COMPRESSOR *cmp) { /* Read byte */
    if (cmp->inpos >= cmp->input_len) {
        dk_set_error("Error reading from input stream.");
        longjmp(error_handler, 1);
    }
    return cmp->input[cmp->inpos++];
}

static void wn (struct COMPRESSOR *cmp, unsigned val) { /* Write nibble */
    if (cmp->outpos >= OUTPUT_LIMIT) {
        dk_set_error("Output size has grown larger than the input size.");
        longjmp(error_handler, 1);
    }
    if (cmp->half == 0) { /* Hi first */
        cmp->output[cmp->outpos] = val << 4;
    }
    else { /* Lo second */
        cmp->output[cmp->outpos++] |= val & 15;
    }
    cmp->half ^= 1;
}
static void wb (struct COMPRESSOR *cmp, unsigned val) { /* Write byte */
    wn(cmp,  15 & (val >> 4));
    wn(cmp,  15 &  val);
}
static void ww (struct COMPRESSOR *cmp, unsigned val) { /* Write word */
    wb(cmp, 255 & (val >> 8));
    wb(cmp, 255 &  val);
}


/* ratio = bytes_in / bytes_out */

/* How many bytes the current case outputs */
static int bytes_out (struct CASE_PARAMS *cp, int c) {
    switch (c) {
        case  0:          { return cp->n;                  }
        case  1: case  7:
        case  8: case 13: { return 1;                      }
        case  2:
        case  6: case  9:
        case 14: case 15: { return 2;                      }
        case  3:
        case  4: case  5: { return cp->rle[c-3];           }
        case 10:
        case 11: case 12: { return cp->w[(c & 3)^2].count; }
    }
    return 0;
}

/* Number of bytes the current case requires */
static double bytes_in (struct CASE_PARAMS *cp, int c) {
    switch (c) {
        case  0:          { return (1.0 + cp->n); }
        case  6: case  7:
        case  8: case 13:
        case 14:          { return 0.5; }
        case  4: case  5:
        case  9: case 15: { return 1.0; }
        case  1:          { return 1.5; }
        case  3: case 10: { return 2.0; }
        case  2: case 11: { return 2.5; }
        case 12:          { return 3.0; }
    }
    return 0;
}



/* Constant generation */

/* A compressed data block starts with a table of constants. */
/* First two entries are bytes used with RLE. */
/* Next three are two bytes and a word entry that have dedicated cases. */
/* Finally there are a further sixteen indexed words. */
struct DATA_CONSTANT {
    unsigned count;
    unsigned index;
};
static int sort_count (const void *aa, const void *bb) {
    const struct DATA_CONSTANT *b = aa;
    const struct DATA_CONSTANT *a = bb;
    if (a->count < b->count)
        return -1;
    if (a->count > b->count)
        return  1;

/* Here we face a dilemma. If the counts are the same we end up with
   multiple candidate constants, and there is no telling which ones will
   be the most effective without trying them. One option would be to
   decide randomly, but it's better to be deterministic. Better yet we
   could try and figure out what makes a particular choice better. */

    if (a->index < b->index)
        return  1;
    /* if (a->index > b->index) */
    return -1;
}
static void write_constant (
    struct COMPRESSOR *cmp,
    struct DATA_CONSTANT *dc,
    int dc_count,
    void (*write)(struct COMPRESSOR*, unsigned),
    int write_count
) {
    int i;
    int units = 0;

    /* Sort by most frequent occurrences */
    qsort(dc, dc_count, sizeof(struct DATA_CONSTANT), sort_count);

    /* Write the n most frequently used units */
    for (i = 0; i < dc_count && units < write_count; i++, units++)
        write(cmp, dc[i].index);
}

static void choose_constants (
    struct COMPRESSOR *cmp,
    struct NODES *nodes,
    struct DATA_CONSTANT *iw
) {
    struct NODES *n = &nodes[0];

    /* Find the best: */
    struct DATA_CONSTANT *cb; /*   contiguous bytes */
    struct DATA_CONSTANT *ib; /* incontiguous bytes */
    ib = iw + 65536;
    cb = ib +   256;

    {
        int i;
        for (i = 0; i < 256; i++) {
            cb[i].index = i;
            ib[i].index = i;
        }
        for (i = 0; i < 65536; i++)
            iw[i].index = i;
    }

    while (n->next != NULL && (n->next - nodes) < cmp->input_len) {

        /* Determine the best non-constant case */
        unsigned char *is = &cmp->input[n->next - &nodes[0]];
        int dist = n->next - n;
        int i;

        switch (n->next->c) {
            case 0: {
                for (i = 0; i < dist; i++)
                    ib[is[i]].count++;
                for (i = 0; i < dist-1; i++)
                    iw[(is[i*2] << 8) | is[i*2+1]].count++;
                break;
            }
            case 2: {
                iw[(*is << 8) | is[1]].count++;
                ib[ is[1]].count++;
            } /* Fallthrough */
            case 1: { ib[*is].count++; break; }
            case 3: { cb[*is].count++; break; }
        }
        n = n->next;
    }

    /* Reset input position */
    cmp->inpos = 0;

    /* Data starts with an unused value */
    wb(cmp, 0);

    /* Write constants to output buffer */
    write_constant(cmp, cb,   256, wb,  2); /* Best   contiguous bytes */
    write_constant(cmp, ib,   256, wb,  2); /* Best incontiguous bytes */
    write_constant(cmp, iw, 65536, ww, 17); /* Best incontiguous words */
}






/* The window cases require backtracking which is expensive, so we use
   a table of linked lists to speed it up considerably. The first 256 entries */
static void generate_byteloc (
    struct COMPRESSOR *cmp,
    struct BYTELOC *byteloc
) {
    int byteloc_size = 0;
    int i;

    for (i = 0; i < cmp->input_len; i++) {
        struct BYTELOC *b = &byteloc[cmp->input[i]];

        while (b->next != NULL)
            b = b->next;

        b->next = &byteloc[256 + byteloc_size++];
        b->index = i;
    }
}

/* Determine which non-constant cases could be used for current block */
static unsigned choose_cases_1 (
    struct COMPRESSOR *cmp,
    struct CASE_PARAMS *cp,
    struct BYTELOC *byteloc
) {
    unsigned char *is = &cmp->input[cmp->inpos];
    unsigned cases = 0;
    int i;

#define add_case(X) cases |= (1 << X)

    { /* RLE (input) */
        int x  = cmp->input_len - cmp->inpos;

        /* Check the next ~18 bytes */
        if (x > 18)
            x = 18;

        /* Are the next 3-18 bytes identical? */
        for (i = 1; i < x; i++)
            if (*is != is[i])
                break;

        /* If they are, then we can use an input byte for RLE */
        if (i > 2) {
            cp->rle[0] = i;
            add_case(3);
        }
    }

    { /* Byte Window */

        struct BYTELOC *b = &byteloc[is[0]];

        /* Window can be 8, 12 or 16 bits */
        cp->w[0].count = 0;
        cp->w[1].count = 0;
        cp->w[2].count = 0;

        for (; b != NULL; b = b->next) {

            int i = b->index;
            int j = 18;
            int k;
            int pos = cmp->inpos - i;

            if (i > cmp->inpos)
                break;

            /* How many bytes to compare */
            if (cmp->input_len - cmp->inpos < j)
                j = cmp->input_len - cmp->inpos;
            if (pos < j)
                j = pos;

            /* How many bytes match */
            for (k = 0; k < j; k++)
                if (is[k] != cmp->input[i+k])
                    break;

            if (k > 2) {

                #define add_window(X,Y,Z) \
                    if (k > X.count) {\
                        X.count = k;\
                        X.addr  = pos - Z;\
                        add_case(Y);\
                    }


                /* A continual source of problems */
                if (pos+k < 256) {
                    add_window(cp->w[0], 10, k);   /*  8-bit */
                }
                if (pos >= 259 && pos <= 4095+259) {
                    add_window(cp->w[1], 11, 259); /* 12-bit */
                }
                add_window(cp->w[2], 12, 0);       /* 16-bit */
            }
            if (k >= 2 && pos < 18) { /* Word */
                cp->wwin = pos;
                add_case(9);
            }
        }
    }

    { /* Repeats */

        /* Byte */
        if (cmp->inpos && *is == is[-1])
            add_case(13);

        /* Word */
        if ((cmp->inpos > 1)
        && ( cmp->inpos + 1 < cmp->input_len)
        && (is[0] == is[-2])
        && (is[1] == is[-1]))
            add_case(14);

    }

    { /* Direct Copy */
        add_case(0); /* n  Bytes   */
        add_case(1); /* 1  Byte    */
        add_case(2); /* 1  Word    */
    }

    return cases;
}

/* Determine which constant cases are possible */
static unsigned choose_cases_2 (
    struct COMPRESSOR *cmp,
    struct CASE_PARAMS *cp,
    struct BYTELOC *byteloc /* unused */
) {
    unsigned char *is = &cmp->input[cmp->inpos];
    int i;
    unsigned cases = 0;

    { /* RLE (constant) */
        int x  = cmp->input_len - cmp->inpos;

        /* Check the next ~18 bytes */
        if (x > 18)
            x = 18;

        /* Are the next 3-18 bytes identical? */
        for (i = 1; i < x; i++)
            if (*is != is[i])
                break;

        if (i > 2 && cmp->outpos) {
            if (*is == cmp->output[1]) {
                cp->rle[1] = i;
                add_case(4);
            }
            if (*is == cmp->output[2]) {
                cp->rle[2] = i;
                add_case(5);
            }
        }
    }

    /* Byte Constants */
    if (cmp->outpos) {
        if (*is == cmp->output[3])
            add_case(7);
        if (*is == cmp->output[4])
            add_case(8);
    }

    /* Word LUT */
    if (cmp->outpos) {
        if (cmp->inpos+1 < cmp->input_len) {
            if ((is[0] == cmp->output[5])
            &&  (is[1] == cmp->output[6]))
                add_case(6);

            for (i = 7; i < 38; i+=2) {
                if ((is[0] == cmp->output[i])
                &&  (is[1] == cmp->output[i+1])
                ) {
                    cp->wlut = (i - 7) / 2;
                    add_case(15);
                    break;
                }
            }
        }
    }
    return cases;
    (void) byteloc;
}

static void choose_cases (
    struct COMPRESSOR *cmp,
    struct NODES *nodes,
    struct BYTELOC *byteloc,
    unsigned (*cc)()
) {
    for (cmp->inpos = 0; cmp->inpos < cmp->input_len; cmp->inpos++) {
        struct NODES *n = &nodes[cmp->inpos];
        n->cases |= cc(cmp, &n->cp, byteloc);
    }
}



static void encode_case (
    struct COMPRESSOR *cmp,
    struct CASE_PARAMS *cp,
    int c,
    int len
) {

    /*
    printf("%04X-%04X: %2d ", cmp->outpos, cmp->inpos, c);
    switch (c) {
        case  1:
        case  2:
        case  6:
        case  7:
        case  8:
        case 13:
        case 14:
        case 15: { printf("\n"); break; }
        case  9: { printf("with wwin %d\n", cp->wwin - 2); break; }
        case  0:
        case  3:
        case  4:
        case  5: { printf("with len %2d\n", len); break; }
        case 10: {
            printf("with len %2d and addr %02X\n",
                   len-3, cp->w[0].addr);
            break;
        }
        case 11: {
            printf("with len %2d and addr %03X\n",
                   len-3, cp->w[1].addr
            );
            break;
        }
        case 12: {
            printf("with len %2d and addr %04X\n",
                   len-3, cp->w[2].addr
            );
            break;
        }
    }
    */


    /* Write case */
    wn(cmp, c);

#define add_pos(X) cmp->inpos += X

    switch (c) {

        /* n bytes */
        case 0: {
            wn(cmp, len);
            while (len--)
                wb(cmp, rb(cmp));
            break;
        }

        /* 2 bytes or 1 byte */
        case 2: { wb(cmp, rb(cmp)); } /* Fallthrough */
        case 1: { wb(cmp, rb(cmp)); break; }

        /* RLE input (3-18) */
        case 3: {
            wn(cmp, len - 3);
            wb(cmp, rb(cmp));
            add_pos(len - 1);
            break;
        }

        /* RLE constant (3-18) */
        case 4:
        case 5: {
            wn(cmp, len - 3);
            add_pos(len);
            break;
        }

        /* Single constants or repeats */
        case 7: case  8: case 13: { add_pos(1); break; }
        case 6: case 14:          { add_pos(2); break; }

       /* Word window */
        case 9: {
            wn(cmp, cp->wwin - 2);
            add_pos(2);
            break;
        }

        /* 8-bit window */
        case 10: {
            wn(cmp, len - 3);
            wb(cmp, cp->w[0].addr + (cp->w[0].count - len));
            add_pos(len);
            break;
        }

        /* 12-bit window */
        case 11: {
            wn(cmp, len - 3);
            wb(cmp, cp->w[1].addr >> 4);
            wn(cmp, cp->w[1].addr & 15);
            add_pos(len);
            break;
        }

        /* 16-bit window */
        case 12: {
            wn(cmp, len - 3);
            ww(cmp, cp->w[2].addr);
            add_pos(len);
            break;
        }

        /* Word LUT */
        case 15: {
            wn(cmp, cp->wlut);
            add_pos(2);
            break;
        }
    }
}






/* We test every case in every node, and only keep the best ones that
   lead to each node. */
static void test_node (
    struct COMPRESSOR *cmp,
    struct NODES *nodes,
    int c
) {
    struct NODES *n = &nodes[cmp->inpos];
    int    bytes = bytes_out(&n->cp, c);
    int      end = cmp->inpos + bytes;
    double ratio = ((n->ratio * cmp->inpos) + bytes_in(&n->cp, c))
                 / (cmp->inpos + bytes);

    /* Decompressing too much is also fine */
    if (end > cmp->input_len)
        end = cmp->input_len;

    if (nodes[end].ratio > ratio) {
        nodes[end].ratio = ratio;
        nodes[end].prev  = n;
        nodes[end].c     = c;
    }

}

/* Test every variant of every valid case */
static void generate_cases (
    struct COMPRESSOR *cmp,
    struct NODES *nodes
) {
    struct NODES *n;

    for (cmp->inpos = 0; cmp->inpos < cmp->input_len; cmp->inpos++) {

        unsigned char c; /* Current case */
        struct NODES *n = &nodes[cmp->inpos];
        struct CASE_PARAMS default_params = n->cp;

        for (c = 0; c <= 15; c++) {

            struct CASE_PARAMS *cp = &n->cp;

            if (!(n->cases & (1 << c)))
                continue;

            switch (c) {
                case  1: case  2: case  6:
                case  7: case  8: case  9:
                case 13: case 14: case 15: { /* FIXED */
                    test_node(cmp, nodes, c);
                    break;
                }
                case  0: { /* COPY */
                    for (cp->n = 1; cp->n <= 15; cp->n++) {
                        if ((cmp->inpos + cp->n) > cmp->input_len)
                            break;
                        test_node(cmp, nodes, c);
                    }
                    break;
                }
                case  3: case  4: case  5: { /* RLE */
                    while (cp->rle[c-3] >= 3) {
                        test_node(cmp, nodes, c);
                        cp->rle[c-3] -= 1;
                    }
                    break;
                }
                case 10: case 11: case 12: { /* WIN */
                    while (cp->w[c-10].count >= 3) {
                        test_node(cmp, nodes, c);
                        cp->w[c-10].count--;
                    }
                    break;
                }
            }
            *cp = default_params;
        }
    }
    cmp->inpos = 0;

    /* Turn our node-list into a list of cases */
    n = &nodes[cmp->input_len];
    while (n->prev != NULL) {
        n->prev->next = n;
        n = n->prev;
    }
}

/* Send the cases to the output stream */
static void write_cases (struct COMPRESSOR *cmp, struct NODES *nodes) {
    struct NODES *n = &nodes[0];
    while (n->next != NULL) {
        encode_case(cmp, &n->cp, n->next->c, n->next - n);
        n = n->next;
    }

    /* End with a nul command */
    wb(cmp, 0);
    if (cmp->half)
        wn(cmp, 0);
}

/* Default state */
static void clear_nodes (struct COMPRESSOR *cmp, struct NODES *nodes) {
    int i;
    nodes[cmp->input_len].next = NULL;
    nodes[0].prev          = NULL;
    nodes[0].cp.rle[1]     =
    nodes[0].cp.rle[2]     =
    nodes[0].cp.wlut       = 0;
    for (i = 1; i <= cmp->input_len; i++) {
        nodes[i].next = nodes[i].prev = NULL;
        nodes[i].cp.rle[1] =
        nodes[i].cp.rle[2] =
        nodes[i].cp.wlut   = 0;
        nodes[i].cases    &= 0x7E0F; /* Keep the non-constant cases */
        nodes[i].ratio     = DBL_MAX;
    }
}

static int complex_method (
    struct COMPRESSOR *cmp,
    struct NODES *nodes,
    struct DATA_CONSTANT *iw,
    struct BYTELOC *byteloc
) {

    /* Madness */
    generate_byteloc(cmp, byteloc);

    /* Get all possible cases for each position that don't use constants */
    clear_nodes(cmp, nodes);
    choose_cases(cmp, nodes, byteloc, choose_cases_1);

    /* Find the optimal path and generate a linked list */
    generate_cases(cmp, nodes);
    /*
    for (cmp->inpos = 0; cmp->inpos < cmp->input_len; cmp->inpos++)
        nodes[cmp->inpos].next = &nodes[cmp->inpos+1];
    */

    /* Pick suitable constants and get all matching cases */
    choose_constants(cmp, nodes, iw);
    clear_nodes(cmp, nodes);
    choose_cases(cmp, nodes, byteloc, choose_cases_2);

    /* Find a new path and write it */
    generate_cases(cmp, nodes);
    write_cases(cmp, nodes);

    return 0;
}



static int allocate_data (
    struct COMPRESSOR *cmp,
    struct NODES **nodes,
    struct DATA_CONSTANT **iw,
    struct BYTELOC **byteloc
) {
    *nodes = calloc(cmp->input_len + 1, sizeof(struct NODES));
    if (*nodes == NULL) {
        dk_set_error("Failed to allocate memory for case data.");
        return 1;
    }

    *iw = calloc(256+256+65536, sizeof(struct DATA_CONSTANT));
    if (*iw == NULL) {
        dk_set_error("Failed to allocate memory for constant search table.");
        free(*nodes);
        return 1;
    }

    *byteloc = calloc(256 + cmp->input_len, sizeof(struct BYTELOC));
    if (*byteloc == NULL) {
        dk_set_error("Failed to allocate data for byte map.");
        free(*nodes);
        free(*iw);
        return 1;
    }

    return 0;
}

int bd_compress (struct COMPRESSOR *cmp) {

    struct DATA_CONSTANT *iw = NULL;
    struct NODES *nodes      = NULL;
    struct BYTELOC *byteloc  = NULL;

    if (allocate_data(cmp, &nodes, &iw, &byteloc))
        return 1;

    if (setjmp(error_handler)) {
        free(nodes);
        free(iw);
        free(byteloc);
        return 1;
    }

    if (complex_method(cmp, nodes, iw, byteloc)) {
        free(nodes);
        free(iw);
        free(byteloc);
        return 1;
    }

    free(nodes);
    free(iw);
    free(byteloc);
    return 0;
}

