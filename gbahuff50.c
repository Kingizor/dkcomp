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
    if (dk->in.pos >= dk->in.length) {
        dk_set_error("Tried to read out of bounds");
        return -1;
    }
    return dk->in.data[dk->in.pos++];
}

static int read_bit (struct COMPRESSOR *dk) {
    int v;
    if (dk->in.pos >= dk->in.length) {
        dk_set_error("Tried to read out of bounds.");
        return -1;
    }
    v = (dk->in.data[dk->in.pos] >> dk->in.bitpos++) & 1;
    if (dk->in.bitpos == 8) {
        dk->in.bitpos  = 0;
        dk->in.pos++;
    }
    return v;
}

static int write_byte (struct COMPRESSOR *dk, unsigned char out) {
    if (dk->out.pos >= dk->out.limit) {
        dk_set_error("Tried to write out of bounds.");
        return 1;
    }
    dk->out.data[dk->out.pos++] = out;
    return 0;
}
static int write_bit (struct COMPRESSOR *dk, int bit) {
    if (dk->out.pos >= dk->out.limit) {
        dk_set_error("Tried to write out of bounds.");
        return 1;
    }
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
        return 1;
    if (c != 0x50) {
        dk_set_error("Incorrect signature byte");
        return 1;
    }

    *length = 0;
    for (i = 0; i < 3; i++) {
        if ((c = read_byte(dk)) < 0)
            return 1;
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
            return 1;
        if (bin->node_count && !a)
            break;
        if (a > b) {
            dk_set_error("Table range error. (a > b)");
            return 1;
        }
        for (; a <= b; a++) {
            int c;
            if ((c = read_byte(bin->dk)) < 0)
                return 1;
            if (bin->node_count >= 256) {
                dk_set_error("Data contains too many values");
                return 1;
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
            return 1;

        if (bit)
            current = current->dir.right;
        else
            current = current->dir.left;

        if (current->type == CLEAF) {
            if (current->value == 256) /* quit */
                break;
            if (write_byte(bin->dk, current->value))
                return 1;
            current = bin->root;
        }
    }
    return 0;
}

int gbahuff50_decompress (struct COMPRESSOR *dk) {
    struct BIN bin;
    size_t length;

    memset(&bin, 0, sizeof(struct BIN));
    bin.dk = dk;

    if ((read_header (dk, &length))
    ||  (init_nodes  (&bin))
    ||  (init_tree   (&bin))
    ||  (decode_input(&bin)))
        return 1;

    if (dk->out.pos != length) {
        dk_set_error("Decompressed size doesn't match header size");
        return 1;
    }

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

static int scale_counts (struct BIN *bin) {
    int i;
    size_t hi = 0;
    double scale;

    for (i = 0; i < 256; i++)
        if (hi < bin->tree[i].count)
            hi = bin->tree[i].count;
    if (!hi) {
        dk_set_error("All bytes occur zero times?");
        return 1;
    }

    scale = hi / 255.0;

    for (i = 0; i < 256; i++) {
        int v = bin->tree[i].count / scale;
        if (bin->tree[i].count && !v)
            bin->tree[i].count = 1;
        else
            bin->tree[i].count = v;
    }
    return 0;
}

static int write_block (struct BIN *bin, int p, int i) {
    if (p != -1) {
        if (write_byte(bin->dk, p)
        ||  write_byte(bin->dk, i-1))
            return 1;
        for (; p < i; p++)
            if (write_byte(bin->dk, bin->tree[p].count))
                return 1;
        p = -1;
    }
    return 0;
}

static int write_header (struct BIN *bin) {

    int i,p;

    /* write signature byte and 24-bit length */
    if (write_byte(bin->dk, 0x50)
    ||  write_byte(bin->dk, bin->dk->in.length)
    ||  write_byte(bin->dk, bin->dk->in.length >>  8)
    ||  write_byte(bin->dk, bin->dk->in.length >> 16))
        return 1;

    /* scale the leaf counts */
    if (scale_counts(bin))
        return 1;

    /* write the table(s) */
    p = -1;
    for (i = 0; i < 256; i++) {
        if (bin->tree[i].count) {
            if (p == -1)
                p = i;
        }
        else if (write_block(bin, p, i))
            return 1;
    }
    if (write_block(bin, p, i))
        return 1;
    if (write_byte(bin->dk, 0)
    ||  write_byte(bin->dk, 0))
        return 1;

    return 0;
}

static int init_bytes (struct BIN *bin) {
    size_t i;
    int e;

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
            return 1;
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
            return 1;
        if (write_pattern(bin, bin->vlut[c]))
            return 1;
    }

    /* quit signal */
    if (write_pattern(bin, bin->vlut[256]))
        return 1;

    /* write the excess */
    while (bin->dk->out.bitpos)
        if (write_bit(bin->dk, 0))
            return 1;

    return 0;
}


int gbahuff50_compress (struct COMPRESSOR *dk) {
    struct BIN bin;

    memset(&bin, 0, sizeof(struct BIN));
    bin.dk = dk;

    if ((init_bytes   (&bin))
    ||  (init_tree    (&bin))
    ||  (encode_output(&bin)))
        return 1;

    return 0;
}

