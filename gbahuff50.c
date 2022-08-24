/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA Huffman (50) compressor and decompressor */

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

/* another Huffman variant */
/* this one stores the frequency of each byte near the start */
/* and constructs the binary tree based on that */

static int read_byte (struct COMPRESSOR *dk) {
    if (dk->in.pos >= dk->in.length)
        return -1;
    return dk->in.data[dk->in.pos++];
}

static int read_bit (struct COMPRESSOR *dk) {
    int v;
    if (dk->in.pos >= dk->in.length)
        return -1;
    v = (dk->in.data[dk->in.pos] >> dk->in.bitpos++) & 1;
    if (dk->in.bitpos == 8) {
        dk->in.bitpos  = 0;
        dk->in.pos++;
    }
    return v;
}

static int write_byte (struct COMPRESSOR *dk, unsigned char out) {
    if (dk->out.pos >= dk->out.limit)
        return DK_ERROR_OOB_OUTPUT_W;
    dk->out.data[dk->out.pos++] = out;
    return 0;
}
static int write_bit (struct COMPRESSOR *dk, int bit) {
    if (dk->out.pos >= dk->out.limit)
        return DK_ERROR_OOB_OUTPUT_W;
    dk->out.data[dk->out.pos] |= bit << dk->out.bitpos++;
    if (dk->out.bitpos == 8) {
        dk->out.bitpos  = 0;
        dk->out.pos++;
        if (dk->out.pos < dk->out.limit)
            dk->out.data[dk->out.pos] = 0;
    }
    return 0;
}




/* decompressor (some parts are shared with the compressor) */

struct NODE {
    struct NODE *parent;
    size_t count;
    union {
        int value;
        struct { struct NODE *left, *right; } dir;
    };
    enum NODE_TYPE { CNODE, CLEAF } type;
};

struct VLUT {
    unsigned pattern;
    int bits;
};

struct BIN {
    struct COMPRESSOR *dk;
    struct NODE tree[513];
    struct VLUT vlut[257];
    struct NODE   *root;
    int node_count;
};

static void add_leaf (struct NODE *n, size_t count, int value) {
    n->type   = CLEAF;
    n->parent = NULL;
    n->count  = count;
    n->value  = value;
}

/* these data segments have a 0x50 signature byte */
/* followed by 24-bit length */
int read_header (struct COMPRESSOR *dk, size_t *length) {
    int c,i;

    if ((c = read_byte(dk)) < 0)
        return DK_ERROR_OOB_INPUT;

    if (c != 0x50)
        return DK_ERROR_SIG_WRONG;

    *length = 0;
    for (i = 0; i < 3; i++) {
        if ((c = read_byte(dk)) < 0)
            return DK_ERROR_OOB_INPUT;
        *length |= c << (i << 3);
    }
    return 0;
}

/* sort by count and index ascending, with zero counts at the end */
static int sort_nodes (const void *aa, const void *bb) {
    const struct NODE *a = aa, *b = bb;
    switch ((!!a->count) | ((!!b->count) << 1)) {
        case 0: { return  0; }
        case 1: { return -1; }
        case 2: { return  1; }
        case 3: { break;     }
    }
    return (a->count < b->count) ? -1
         : (a->count > b->count) ?  1
         : (a->value < b->value) ? -1
         : (b->value < a->value) ?  1
         :  0;
}

static int init_nodes (struct BIN *bin) {

    int i;
    bin->node_count = 0;

    /* read the frequency values from ROM */
    for (;;) {
        int a,b;
        if ((a = read_byte(bin->dk)) < 0
        ||  (b = read_byte(bin->dk)) < 0)
            return DK_ERROR_OOB_INPUT;
        if (bin->node_count && !a)
            break;
        if (a > b)
            return DK_ERROR_TABLE_RANGE;
        for (; a <= b; a++) {
            int c;
            if ((c = read_byte(bin->dk)) < 0)
                return DK_ERROR_OOB_INPUT;
            if (bin->node_count >= 256) {
                return DK_ERROR_TABLE_VALUE;
            }
            add_leaf(&bin->tree[bin->node_count++], c, a);
        }
    }

    /* quit leaf */
    add_leaf(&bin->tree[bin->node_count++], 1, 256);

    /* sort the frequencies */
    qsort(bin->tree, bin->node_count, sizeof(struct NODE), sort_nodes);

    for (i = 0; i < 257; i++)
        if (!bin->tree[i].count)
            break;
    bin->node_count = i;

    /* adjust the output position */
    if ((bin->dk->in.pos & 3) < 2)
         bin->dk->in.pos &= ~1;

    return 0;
}


/* set the parent value for each node */
static void init_parent (struct NODE *n) {
    n->dir. left->parent = n;
    if (n->dir. left->type == CNODE)
        init_parent(n->dir. left);

    n->dir.right->parent = n;
    if (n->dir.right->type == CNODE)
        init_parent(n->dir.right);
}

/* construct a tree from the leaf nodes */
static int init_tree (struct BIN *bin) {

    struct NODE leaf_queue[257];
    struct NODE node_queue[256];
    int nqc = 0, nqp = 0, lqp = 0; /* leaf/node pos/count */
    int lqc = bin->node_count;
    int ts = 0; /* tree size */

    memcpy(leaf_queue, bin->tree, bin->node_count * sizeof(struct NODE));

    while (lqp < lqc || nqp < nqc) {
        int i;
        for (i = 0; i < 2; i++) {
            if (lqp < lqc && nqp < nqc) {
                if (leaf_queue[lqp].count <= node_queue[nqp].count)
                    bin->tree[ts++] = leaf_queue[lqp++];
                else
                    bin->tree[ts++] = node_queue[nqp++];
            }
            else if (lqp < lqc) { bin->tree[ts++] = leaf_queue[lqp++]; }
            else if (nqp < nqc) { bin->tree[ts++] = node_queue[nqp++]; }
        }

        if (ts & 1) {
            bin->root = &bin->tree[ts-1];
            bin->root->parent = NULL;
            break;
        }
        else {
            struct NODE *nn = &node_queue[nqc++];
            nn->type        = CNODE;
            nn->dir. left   = &bin->tree[ts-2];
            nn->dir.right   = &bin->tree[ts-1];
            nn->count       = nn->dir. left->count
                            + nn->dir.right->count;
        }
    }
    init_parent(bin->root);

    bin->node_count = ts;
    return 0;
}

static int decode_input (struct BIN *bin) {
    struct NODE *current = bin->root;
    for (;;) {
        int bit;
        if ((bit = read_bit(bin->dk)) < 0)
            return DK_ERROR_OOB_INPUT;

        if (bit)
            current = current->dir.right;
        else
            current = current->dir.left;

        if (current->type == CLEAF) {
            if (current->value == 256) /* quit */
                break;
            if (write_byte(bin->dk, current->value))
                return DK_ERROR_OOB_OUTPUT_W;
            current = bin->root;
        }
    }
    return 0;
}

int gbahuff50_decompress (struct COMPRESSOR *dk) {
    struct BIN bin;
    size_t length;
    enum DK_ERROR e;

    memset(&bin, 0, sizeof(struct BIN));
    bin.dk = dk;

    if ((e = read_header (dk, &length))
    ||  (e = init_nodes  (&bin))
    ||  (e = init_tree   (&bin))
    ||  (e = decode_input(&bin)))
        return e;

    if (dk->out.pos != length)
        return DK_ERROR_SIZE_WRONG;

    return 0;
}






/* compressor */

static void generate_vlut (struct BIN *bin) {
    int i;
    for (i = 0; i < 257; i++) {
        struct NODE *current;
        int j;

        /* find the value in the tree */
        for (j = 0; j < bin->node_count; j++)
            if (bin->tree[j].type  == CLEAF
            &&  bin->tree[j].value == i)
                break;

        /* value not found, proceed to next */
        if (j == bin->node_count)
            continue;

        /* decipher the path to the node */
        current = &bin->tree[j];
        while (current != bin->root) {
            bin->vlut[i].pattern <<= 1;
            bin->vlut[i].bits++;
            if (current == current->parent->dir.right)
                bin->vlut[i].pattern |= 1;
            current = current->parent;
        }
    }
}

/* counts affect the tree and therefore the size of the data. */
/* the most frequent entries should have the highest counts!  */
static int scale_counts (struct BIN *bin) {
    int i;
    size_t hi = 0;
    double scale;

    for (i = 0; i < 256; i++)
        if (hi < bin->tree[i].count)
            hi = bin->tree[i].count;
    if (!hi)
        return DK_ERROR_TABLE_ZERO;

    scale = hi / 255.0;

    for (i = 0; i < 256; i++) {
        int v = bin->tree[i].count / scale;
        if (bin->tree[i].count)
            bin->tree[i].count = v ? v : 1;
    }
    return 0;
}

static int write_block (struct BIN *bin, int p, int i) {
    if (write_byte(bin->dk, p)
    ||  write_byte(bin->dk, i))
        return DK_ERROR_OOB_OUTPUT_W;
    for (; p <= i; p++)
        if (write_byte(bin->dk, bin->tree[p].count))
            return DK_ERROR_OOB_OUTPUT_W;
    return 0;
}

/* tables are preceded by a pair of bytes denoting the first and last    */
/* entries. an entry occurring zero times can be stored with a value of  */
/* zero or omitted entirely. Omitting entries is more efficient if there */
/* are three or more consecutive zeros. */
static int write_tables (struct BIN *bin) {

    int i,s;

    /* find the first non-zero count */
    for (s = 0; s < 256; s++)
        if (bin->tree[s].count)
            break;

    /* examine the data and write any intermediary tables */
    for (i = s+1; i < 256; i++) {
        if (!bin->tree[i].count) {
            int j;
            for (j = i+1; j < 256; j++)
                if (bin->tree[j].count)
                    break;
            if (j - i > 2) {
                if (write_block(bin, s, i-1))
                    return DK_ERROR_OOB_OUTPUT_W;
                i = j+1;
                s = j;
            }
            if (j == 256)
                break;
        }
    }

    /* find the last non-zero count */
    for (i = 255; i >= 0; i--)
        if (bin->tree[i].count)
            break;

    /* write the final table if there is data remaining */
    if (s < 256)
        if (write_block(bin, s, i))
            return DK_ERROR_OOB_OUTPUT_W;

    /* tables are terminated by double zero */
    if (write_byte(bin->dk, 0)
    ||  write_byte(bin->dk, 0))
        return DK_ERROR_OOB_OUTPUT_W;

    return 0;
}

static int write_header (struct BIN *bin) {

    enum DK_ERROR e;

    /* write signature byte and 24-bit length */
    if (write_byte(bin->dk, 0x50)
    ||  write_byte(bin->dk, bin->dk->in.length)
    ||  write_byte(bin->dk, bin->dk->in.length >>  8)
    ||  write_byte(bin->dk, bin->dk->in.length >> 16))
        return DK_ERROR_OOB_OUTPUT_W;

    /* scale the leaf counts */
    if ((e = scale_counts(bin)))
        return e;

    /* write the table(s) */
    if ((e = write_tables(bin)))
        return e;

    return 0;
}

static int init_bytes (struct BIN *bin) {
    size_t i;
    enum DK_ERROR e;

    /* initialise the leafs */
    for (i = 0; i < 257; i++)
        add_leaf(&bin->tree[i], 0, i);
    bin->tree[256].count = 1;

    /* count how many times each value occurs */
    for (i = 0; i < bin->dk->in.length; i++)
        bin->tree[bin->dk->in.data[i]].count++;

    /* write the header and the count table(s) */
    if ((e = write_header(bin)))
        return e;

    /* sort ascending */
    qsort(bin->tree, 257, sizeof(struct NODE), sort_nodes);

    /* figure out how many bytes are actually used */
    for (i = 0; i < 257; i++)
        if (!bin->tree[i].count)
            break;
    bin->node_count = i;

    return 0;
}

static int write_pattern (struct BIN *bin, struct VLUT copy) {
    while (copy.bits--) {
        if (write_bit(bin->dk, copy.pattern & 1))
            return DK_ERROR_OOB_OUTPUT_W;
        copy.pattern >>= 1;
    }
    return 0;
}

static int encode_output (struct BIN *bin) {

    /* generate a lookup table for traversal */
    generate_vlut(bin);

    /* encode each byte from input */
    while (bin->dk->in.pos < bin->dk->in.length) {
        int c;
        if ((c = read_byte(bin->dk)) < 0)
            return DK_ERROR_OOB_INPUT;
        if (write_pattern(bin, bin->vlut[c]))
            return DK_ERROR_OOB_OUTPUT_W;
    }

    /* quit signal */
    if (write_pattern(bin, bin->vlut[256]))
        return DK_ERROR_OOB_OUTPUT_W;

    /* write the excess */
    bin->dk->out.pos += !!bin->dk->out.bitpos;

    return 0;
}

int gbahuff50_compress (struct COMPRESSOR *dk) {
    struct BIN bin;
    enum DK_ERROR e;

    memset(&bin, 0, sizeof(struct BIN));
    bin.dk = dk;

    if ((e = init_bytes   (&bin))
    ||  (e = init_tree    (&bin))
    ||  (e = encode_output(&bin)))
        return e;

    return 0;
}

