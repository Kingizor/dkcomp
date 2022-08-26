/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA Huffman (60) compressor and decompressor */

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

#define NODE_LIMIT 515

/* (de)compression routine used in GBA versions of DKC2 and DKC3 */
/* most functions are shared between compressor and decompressor */

struct NODE {
    enum CTYPE { CNODE, CLEAF } type;
    int weight;
    int parent;
    union {
        struct { int L; int R; } dir; /* node */
        unsigned short val;           /* leaf */
    };
};

struct BIN {
    struct COMPRESSOR *gba;
    struct NODE *tree;
};

static int read_bit (struct COMPRESSOR *gba) {
    int v;
    if (gba->in.pos >= gba->in.length)
        return -1;
    v = (gba->in.data[gba->in.pos] >> gba->in.bitpos++) & 1;
    if (gba->in.bitpos == 8) {
        gba->in.bitpos  = 0;
        gba->in.pos++;
    }
    return v;
}

static int write_byte (struct COMPRESSOR *gba, unsigned char out) {
    if (gba->out.pos >= gba->out.limit)
        return -1;
    gba->out.data[gba->out.pos++] = out;
    return 0;
}


static void rebuild_tree (struct BIN *bin, int node_count) {
    struct NODE *tree = bin->tree;
    int node, pnode;

    /* send all leaves to the back and cut the weight by half */
    node  = node_count;
    pnode = node_count-1;
    while (node--)
        if (tree[node].type == CLEAF) {
            tree[pnode] = tree[node];
            tree[pnode].weight = (tree[pnode].weight + 1) / 2;
            pnode--;
        }

    /* generate new nodes */
    node = node_count-2;
    while (node > 0) {
        int weight = tree[node].weight + tree[node+1].weight;

        /* find an appropriate position for the new node */
        int rnode = pnode+1;
        while (weight < tree[rnode].weight)
            rnode++;
        rnode--;

        /* shift everything up one to make space */
        memmove(&tree[pnode], &tree[pnode+1],
               (rnode-pnode)*sizeof(struct NODE));

        /* add the new node */
        tree[rnode].type   = CNODE;
        tree[rnode].weight = weight;
        tree[rnode].dir.L  = node;
        tree[rnode].dir.R  = node+1;

        node -= 2;
        pnode--;
    }

    /* apply the new parent nodes */
    node = node_count-1;
    while (node--)
        if (tree[node].type == CNODE) {
            tree[tree[node].dir.L].parent = node;
            tree[tree[node].dir.R].parent = node;
        }
}

static int add_leaf (struct BIN *bin, int *node, int nc, unsigned char val) {
    struct NODE *tree = bin->tree;
    struct NODE new_leaf =
        { CLEAF, 0, nc-1, .val=val };
    struct NODE new_node =
        { CNODE, 1, tree[nc-1].parent, .dir.L = nc, .dir.R = nc+1 };
    int i;

    /* upper limit for nodes */
    if (nc+1 >= NODE_LIMIT)
        return DK_ERROR_HUFF_NODELIM;

    /* it's possible for malformed data to have duplicate leaves */
    /* does the leaf already exist in our tree? */
    for (i = 0; i < nc; i++)
        if (bin->tree[i].type == CLEAF
        &&  bin->tree[i].val  == val)
            return DK_ERROR_HUFF_LEAFVAL;

    /* add the new leaf */
    tree[nc+1] = new_leaf;

    /* move the old node */
    tree[nc]   = tree[nc-1];
    tree[nc].parent = nc-1;

    /* add the new node */
    tree[nc-1] = new_node;

    *node = nc+1;
    return 0;
}

static void swap_nodes (struct BIN *bin, int aa, int bb) {
    struct NODE *tree = bin->tree;
    struct NODE *a = &tree[aa];
    struct NODE *b = &tree[bb];
    struct NODE  c = *a;
    int parent;

    /* tell the children parent's new position */
    if (a->type == CNODE)
        tree[a->dir.L].parent = tree[a->dir.R].parent = bb;
    if (b->type == CNODE)
        tree[b->dir.L].parent = tree[b->dir.R].parent = aa;

    /* swap a/b */
    *a = *b;
    *b =  c;

    /* parents are dependent on position, so those values don't change */
       parent = a->parent;
    a->parent = b->parent;
    b->parent = parent;
}

static void update_weights (struct BIN *bin, int node) {
    struct NODE *tree = bin->tree;
    while (node >= 0) {
        int pnode = node;
        tree[node].weight++;
        while (pnode-- && tree[pnode].weight < tree[node].weight);
        pnode++;
        if (pnode != node)
            swap_nodes(bin, pnode, node);
        node = tree[pnode].parent;
    }
}

int gbahuff60_decompress (struct COMPRESSOR *gba) {

    /* tree consists of:
         - two special leafs     (the "new value" and "quit" commands)
         - up to 256 value leafs (each unique byte encountered so far)
         - nodes to connect every leaf
    */
    struct NODE tree[NODE_LIMIT] = {
        {CNODE, 2, -1, .dir.L = 1, .dir.R = 2 },
        {CLEAF, 1,  0, .val = 0x100 }, /* quit */
        {CLEAF, 1,  0, .val = 0x101 }  /* new leaf */
    };
    int node_count = 3;
    struct BIN bin = { gba, tree };
    size_t data_length;
    enum DK_ERROR e;

    /* check the header */
    if (gba->in.length < 4)
        return DK_ERROR_INPUT_SMALL;
    if (gba->in.data[0] != 0x60)
        return DK_ERROR_SIG_WRONG;
    data_length =  gba->in.data[1]
                | (gba->in.data[2] <<  8)
                | (gba->in.data[3] << 16);
    gba->in.pos = 4;


    /* process the data */
    for (;;) {
        int out  = 0;
        int quit = 0;
        int node = 0;
        int i;

        /* traverse the tree for a value */
        while (tree[node].type == CNODE) {
            switch (read_bit(gba)) {
                case 0: { node = tree[node].dir.L; break; }
                case 1: { node = tree[node].dir.R; break; }
               default: { return DK_ERROR_OOB_INPUT; }
            }
        }

        /* process the value */
        switch (tree[node].val) {
               default: { out = tree[node].val; break; }
            case 0x100: { quit = 1; break; }
            case 0x101: {
                for (i = 0; i < 8; i++) {
                    int bit;
                    if ((bit = read_bit(gba)) < 0)
                        return DK_ERROR_OOB_INPUT;
                    out <<= 1;
                    out |= bit;
                }
                if ((e = add_leaf(&bin, &node, node_count, out)))
                    return e;
                node_count += 2;
                break;
            }
        }
        if (quit)
            break;
        if (write_byte(gba, out))
            return DK_ERROR_OOB_OUTPUT_W;
        if (gba->out.pos > data_length)
            return DK_ERROR_SIZE_WRONG;

        /* rebuild the tree if root weight exceeds 0x8000 */
        if (tree->weight >= 0x8000) {
            rebuild_tree(&bin, node_count);

            /* the old node pointer is invalid after rebuilding the tree */
            for (i = 0; i < node_count; i++)
                if (bin.tree[i].type == CLEAF
                &&  bin.tree[i].val  == out)
                    break;
            node = i;
        }

        update_weights(&bin, node);
    }

    if (gba->out.pos != data_length)
        return DK_ERROR_SIZE_WRONG;

    return 0;
}






/* compressor */

static int write_bit (struct COMPRESSOR *gba, unsigned char val) {
    size_t addr = gba->out.pos + gba->out.bytepos;
    unsigned bit = gba->out.bitpos++;
    if (addr >= gba->out.limit)
        return DK_ERROR_OOB_OUTPUT_W;
    gba->out.data[addr] &= ~(1 << bit);
    gba->out.data[addr] |= val << bit;
    if (gba->out.bitpos == 8) {
        gba->out.bitpos  = 0;
        gba->out.pos++;
    }
    return 0;
}

/* linear search (very bad!) */
static int nsearch (struct NODE *tree, int node_count, int val) {
    struct NODE *n = &tree[node_count];
    while (--n > tree)
        if (n->type == CLEAF && n->val == val)
            break;
    return n - tree;
}

static int encode_leaf (struct BIN *bin, struct NODE *n) {
    unsigned sequence  = 0;
    unsigned char bits = 0;

    /* construct the path */
    while (n->parent != -1) {
        struct NODE *parent = &bin->tree[n->parent];
        sequence <<= 1;
        sequence |= (n == &bin->tree[parent->dir.R]);
        n = parent;
        bits++;
    }

    /* write the sequence */
    while (bits--) {
        int bit = !!(sequence & 1);
        sequence >>= 1;
        if (write_bit(bin->gba, bit))
            return DK_ERROR_OOB_OUTPUT_W;
    }
    return 0;
}

int gbahuff60_compress (struct COMPRESSOR *gba) {

    struct NODE tree[NODE_LIMIT] = {
        { CNODE, 2, -1, .dir.L = 1, .dir.R = 2 },
        { CLEAF, 1,  0, .val = 0x100 },
        { CLEAF, 1,  0, .val = 0x101 }
    };
    int node_count = 3;
    struct BIN bin = { gba, tree };
    enum DK_ERROR e;

    /* write header */
    if (write_byte(gba, 0x60)
    ||  write_byte(gba, gba->in.length)
    ||  write_byte(gba, gba->in.length >>  8)
    ||  write_byte(gba, gba->in.length >> 16))
        return DK_ERROR_OOB_OUTPUT_W;

    /* process data */
    while (gba->in.pos < gba->in.length) {
        int  val = gba->in.data[gba->in.pos++];
        int node = nsearch(tree, node_count, val);
        int i;
        if (!node) { /* leaf not present in tree, so add a new leaf */

            /* send the new leaf command */
            if ((e = encode_leaf(&bin, &tree[nsearch(tree, node_count, 0x101)])))
                return e;

            /* write the value */
            for (i = 0; i < 8; i++)
                if (write_bit(gba, (val >> (7^i)) & 1))
                    return DK_ERROR_OOB_OUTPUT_W;

            /* add the leaf to the tree */
            if ((e = add_leaf(&bin, &node, node_count, val)))
                return e;
            node_count += 2;
        }
        else { /* use the existing leaf */
            if ((e = encode_leaf(&bin, &tree[node])))
                return e;
        }

        /* rebuild the tree is the root node becomes too heavy */
        if (tree->weight >= 0x8000) {
            rebuild_tree(&bin, node_count);

            /* the old node pointer is invalid after rebuilding the tree */
            for (i = 0; i < node_count; i++)
                if (tree[i].type == CLEAF
                &&  tree[i].val  == val)
                    break;
            node = i;
        }

        update_weights(&bin, node);
    }

    /* quit */
    if ((e = encode_leaf(&bin, &tree[nsearch(tree, node_count, 0x100)])))
        return e;

    /* excess */
    if (gba->out.bitpos || gba->out.bytepos) {
        if ((gba->out.pos+1) > gba->out.limit)
            return DK_ERROR_OOB_OUTPUT_W;
        gba->out.pos++;
    }
    return 0;
}

