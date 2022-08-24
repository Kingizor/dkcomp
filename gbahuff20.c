/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA BIOS Huffman (20) compressor and decompressor */

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

/* compression and decompression functions for GBA/DS Huffman */
/* currently only supports 8-bit data size */

/* bios routines read a 32-bit big-endian word */
static int read_bit (struct COMPRESSOR *gba) {
    int v;
    if ((gba->in.pos + (3 ^ gba->in.bytepos)) >= gba->in.length)
        return -1;
    v = (gba->in.data[gba->in.pos
      + (3 ^ gba->in.bytepos)]
     >> (7 ^ gba->in.bitpos++)) & 1;
    if (gba->in.bitpos == 8) {
        gba->in.bitpos  = 0;
        gba->in.bytepos++;
        if (gba->in.bytepos == 4) {
            gba->in.bytepos  = 0;
            gba->in.pos += 4;
        }
    }
    return v;
}
static int read_tree (struct COMPRESSOR *gba, size_t inpos) {
    if (inpos >= gba->in.length)
        return -1;
    return gba->in.data[inpos];
}
static int write_out (struct COMPRESSOR *gba, int bit) {
    if (gba->out.pos >= gba->out.limit)
        return DK_ERROR_OOB_OUTPUT_W;
    gba->out.data[gba->out.pos] |= bit << gba->out.bitpos++;
    if (gba->out.bitpos == 8) {
        gba->out.bitpos  = 0;
        gba->out.pos++;
        if (gba->out.pos < gba->out.limit)
            gba->out.data[gba->out.pos] = 0;
    }
    return 0;
}

int gbahuff20_decompress (struct COMPRESSOR *gba) {

    size_t output_size;
    int data_size; /* how many bits per leaf */
    int n    = 0;  /* current node position */
    int node = 0;  /* previous node value */

    if (gba->in.length < 6)
        return DK_ERROR_EARLY_EOF;

    if ((gba->in.data[0] & 0xF0) != 0x20)
        return DK_ERROR_SIG_WRONG;

    /* data sizes other than 8 are untested */
    data_size = gba->in.data[0] & 15;
    if (!data_size || data_size > 8)
        return DK_ERROR_HUFF_WRONG;
    if (data_size != 8)
        return DK_ERROR_HUFF_LEAF;

    /* size of the output data */
    output_size =  gba->in.data[1]
                | (gba->in.data[2] <<  8)
                | (gba->in.data[3] << 16);

    /* data offset */
    gba->in.pos = 4+2*(gba->in.data[4]+1);

    while (gba->out.pos < output_size) {
        int i, dir = read_bit(gba);
        if (dir < 0)
            return DK_ERROR_OOB_INPUT;
        if ((!dir && (node & 0x80))
        || (  dir && (node & 0x40))) { /* next is a leaf */
            if ((node = read_tree(gba, 6+2*n+dir)) < 0)
                return DK_ERROR_OOB_INPUT;
            for (i = 0; i < data_size; i++)
                if (write_out(gba, !!(node & (1 << i))) < 0)
                    return DK_ERROR_OOB_OUTPUT_W;
            node = n = 0;
        }
        else { /* next is a node */
            if ((node = read_tree(gba, 6+2*n+dir)) < 0)
                return DK_ERROR_OOB_INPUT;
            n += (node & 0x3F)+1;
        }
    }
    return DK_SUCCESS;
}







/* compressor */

enum NODE_TYPE { CNODE, CLEAF };
struct NODE {
    enum NODE_TYPE type;
    int count;
    struct NODE *parent;
    union { struct { struct NODE *left, *right; }; int value; };
};

struct VLUT {
    unsigned sequence;
    int bits;
};

struct BIN {
    struct COMPRESSOR *gba;
    struct NODE tree[513];
    struct VLUT vlut[257];
    struct NODE *root;
    int node_count;
};

/* count frequency of input bytes */
struct VALC {
    unsigned char index;
    unsigned count;
};
static int valc_sort (const void *aa, const void *bb) {
    const struct VALC *a = aa, *b = bb;
    return (a->count > b->count) ? -1
         : (a->count < b->count);
}

static void generate_leaves (struct BIN *bin) {
    struct COMPRESSOR *gba = bin->gba;
    struct VALC count[256];
    int i;
    bin->node_count = 0;

    /* initialise the array */
    for (i = 0; i < 256; i++) {
        count[i].index = i;
        count[i].count = 0;
    }

    /* count how often nodes appear and sort by most frequent */
    while (gba->in.pos < gba->in.length)
        count[gba->in.data[gba->in.pos++]].count++;
    qsort(count, 256, sizeof(struct VALC), valc_sort);
    gba->in.pos = 0;

    /* determine dictionary size */
    for (i = 0; i < 256; i++)
        if (!count[i].count)
            break;

    /* enqueue every leaf */
    while (i--) {
        struct NODE *leaf = &bin->tree[bin->node_count++];
        leaf->type  = CLEAF;
        leaf->count = count[i].count;
        leaf->value = count[i].index;
    }
}


/* set the parent value for each node */
static void init_parent (struct NODE *n) {
    n-> left->parent = n;
    n->right->parent = n;
    if (n-> left->type == CNODE) init_parent(n-> left);
    if (n->right->type == CNODE) init_parent(n->right);
}

static void generate_tree (struct BIN *bin) {
    struct NODE *tree = bin->tree;
    struct NODE leaf_tree[257];
    struct NODE node_tree[256];
    int lqc = bin->node_count;
    int nqc = 0; /* leaf/node queue count */
    int lqp = 0; /* leaf/node queue position */
    int nqp = 0;
    int ts  = 0;

    memcpy(leaf_tree, tree, lqc * sizeof(struct NODE));

    /* create the tree */
    while (lqp < lqc || nqp < nqc) {
        int i;

        /* dequeue the two smallest nodes */
        for (i = 0; i < 2; i++) {
            if (lqp < lqc && nqp < nqc) {
                if (leaf_tree[lqp].count < node_tree[nqp].count)
                    tree[ts++] = leaf_tree[lqp++];
                else
                    tree[ts++] = node_tree[nqp++];
            }
            else if (lqp < lqc) { tree[ts++] = leaf_tree[lqp++]; }
            else if (nqp < nqc) { tree[ts++] = node_tree[nqp++]; }
        }

        /* final node is the top node */
        if (ts & 1) {
            bin->root = &tree[ts-1];
            bin->root->parent = NULL;
            nqp--;
            break;
        }
        else {
            /* enqueue a new node */
            struct NODE *nn = &node_tree[nqc++];
            nn->type  = CNODE;
            nn->left  = &tree[ts-2];
            nn->right = &tree[ts-1];
            nn->count = nn-> left->count
                      + nn->right->count;
        }
    }
    init_parent(bin->root);
    bin->node_count = ts;
}




/* we've gone back and forth between recursive and stack based methods. */
/* we're currently using a multi-stack method so that we can descend */
/* through the tree, but briefly switch out so we can place old nodes. */
/* if we don't switch, we overrun and become unable to place certain nodes. */
/* if we use a tier-based approach the same problem occurs. */

#define STACK_LIMIT   8
#define  NODE_LIMIT 128

struct NODEV {
    struct NODE *node;
    int index; /* where should this node be placed */
};

struct NODE_STACK {
    struct NODEV node[NODE_LIMIT];
    int count; /* how many elements in this stack */
};

struct NODE_BLOCK {
    unsigned char *buf;
    struct NODE_STACK stack[STACK_LIMIT];
    int sc;    /* how many stacks are in use */
    int addr;  /* output position counter */
};

static int gba_node (
    struct NODE_BLOCK *ns,
    struct NODE_STACK *s,
    struct NODEV *nv
) {
    if (nv->node->type == CLEAF) {
        ns->buf[nv->index] = nv->node->value;
    }
    else {
        /* calculate offset and children */
        unsigned offset  = ((ns->addr & ~1) - (nv->index & ~1) - 1) / 2;
        struct NODEV nvl = { nv->node-> left, ns->addr++ };
        struct NODEV nvr = { nv->node->right, ns->addr++ };

        /* we've made a mistake if the calculated offset exceeds 0x3F */
        if (offset >= 0x40)
            return DK_ERROR_HUFF_DIST;

        /* determine node end flags and write the node value */
        offset |= ((nv->node->right->type == CLEAF) << 6);
        offset |= ((nv->node-> left->type == CLEAF) << 7);
        ns->buf[nv->index] = offset;

        /* push the children, last in, first out */
        if ((s->count + 2) >= NODE_LIMIT)
            return DK_ERROR_HUFF_NODES;
        s->node[s->count++] = nvr;
        s->node[s->count++] = nvl;
    }
    return 0;
}

static void remove_stack (struct NODE_BLOCK *ns, int i) {
    /* if the current stack is now empty, remove it */
    if (!ns->stack[i].count) {
        ns->sc--;
        for (; i < ns->sc; i++)
            ns->stack[i] = ns->stack[i+1];
        ns->stack[i].count = 0;
    }
}

/* create the node table in GBA format */
/* The distance (n-1)/2 must fit into 6 bits. */
static int gba_tree (struct BIN *bin) {

    struct NODE_BLOCK ns = {
        &bin->gba->out.data[4],
        {{{ { bin->root, 1 } }, 1}}, /* root node at position #1 */
        1, 2
    };
    enum DK_ERROR e;

    /* create the tree */
    while (ns.sc) {
        struct NODE_STACK *s;
        struct NODEV nv;
        int i;

        /* check the ages of the oldest nodes in each stack */
        for (i = 0; i < ns.sc; i++)
            if ((ns.addr - ns.stack[i].node[0].index) >= 125) /* careful */
                break;

        /* if oldest node cannot be delayed any longer, place it now */
        /* otherwise place the youngest node from the oldest stack */
        if (i == ns.sc) {
            i = 0;
            s = &ns.stack[i];
            nv = s->node[--s->count];

        }
        else {
            /* dequeue the old node */
            s = &ns.stack[i];
            nv = s->node[0];
            memmove(&s->node[0],
                    &s->node[1],
                   --s->count * sizeof(struct NODEV));
            remove_stack(&ns, i);

            /* init a new stack */
            /* (if current node is a leaf it won't be used) */
            i = ns.sc++;
            if (ns.sc > STACK_LIMIT)
                return DK_ERROR_HUFF_STACKS;
        }

        /* place the node */
        if ((e = gba_node(&ns, &ns.stack[i], &nv)))
            return e;
        remove_stack(&ns, i);
    }

    return 0;
}

/* tree entries can be n (?) bits (we only do 8-bits at the moment) */
static int gba_header (struct BIN *bin) {
    struct COMPRESSOR *gba = bin->gba;
    size_t header_size = 5+bin->node_count;
    enum DK_ERROR e;

    if (gba->out.limit < header_size)
        return DK_ERROR_HUFF_OUTSIZE;

    /* compression type = 2, data size = 8 */
    gba->out.data[0] = 0x28;

    /* decompressed size */
    gba->out.data[1] = gba->in.length;
    gba->out.data[2] = gba->in.length >>  8;
    gba->out.data[3] = gba->in.length >> 16;

    /* generate and write the GBA tree */
    if ((e = gba_tree(bin)))
        return e;

    /* record the number of nodes */
    if (header_size  & 3)
        header_size += 4 - (header_size & 3);
    gba->out.pos = header_size;
    gba->out.data[4] = (header_size-5)/2;

    return 0;
}




/* encoding data is faster with a lookup table */

static void create_lut (struct BIN *bin) {
    int i;

    /* create LUT for data encoding */
    memset(bin->vlut, 0, 256*sizeof(struct VLUT));
    for (i = 0; i < bin->node_count; i++) {
        if (bin->tree[i].type == CLEAF) {
            struct NODE *n = &bin->tree[i];
            struct VLUT *v = &bin->vlut[n->value];
            int bits = 0;
            unsigned sequence = 0;

            /* traverse to form a sequence */
            while (n != bin->root) {
                sequence <<= 1;
                bits++;
                if (n == n->parent->right)
                    sequence |= 1;
                n = n->parent;
            }
            v->bits     = bits;
            v->sequence = sequence;
        }
    }
}


static int write_bit (struct COMPRESSOR *gba, unsigned char val) {
    size_t addr = gba->out.pos + (3 ^ gba->out.bytepos);
    unsigned bit = 7 ^ gba->out.bitpos++;
    if (addr >= gba->out.limit)
        return DK_ERROR_OOB_OUTPUT_W;
    gba->out.data[addr] &= ~(1 << bit);
    gba->out.data[addr] |= val << bit;
    if (gba->out.bitpos == 8) {
        gba->out.bitpos  = 0;
        gba->out.bytepos++;
        if (gba->out.bytepos == 4) {
            gba->out.bytepos  = 0;
            gba->out.pos += 4;
        }
    }
    return 0;
}
static int encode_data (struct BIN *bin) {
    struct COMPRESSOR *gba = bin->gba;
    size_t i;
    for (i = 0; i < gba->in.length; i++) {
        struct VLUT v = bin->vlut[gba->in.data[i]];
        while (v.bits--) {
            if (write_bit(gba, v.sequence & 1))
                return DK_ERROR_OOB_OUTPUT_W;
            v.sequence >>= 1;
        }
    }
    if (gba->out.bitpos || gba->out.bytepos) { /* excess */
        if ((gba->out.pos+4) > gba->out.limit)
            return DK_ERROR_OOB_OUTPUT_W;
        gba->out.pos += 4;
    }
    return 0;
}

int gbahuff20_compress (struct COMPRESSOR *gba) {

    struct BIN bin = { gba, {{0}}, {{0}}, NULL, 0 };
    enum DK_ERROR e;

    if (gba->out.limit <= 520)
        return DK_ERROR_OUTPUT_SMALL;

    generate_leaves(&bin);
    generate_tree  (&bin);
    create_lut     (&bin);

    if ((e = gba_header (&bin))
    ||  (e = encode_data(&bin)))
        return e;

    return 0;
}

