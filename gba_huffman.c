#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "dk_internal.h"

/* compression and decompression functions for GBA/DS Huffman */
/* currently only supports 8-bit data size */

/* bios routines read a 32-bit big-endian word */
static int read_bit (struct COMPRESSOR *gba) {
    int v;
    if ((gba->in.pos + (3 ^ gba->in.bytepos)) >= gba->in.length) {
        dk_set_error("Tried to read out of bounds");
        return -1;
    }
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
    if (inpos >= gba->in.length) {
        dk_set_error("Tried to read out of bounds");
        return -1;
    }
    return gba->in.data[inpos];
}
static int write_out (struct COMPRESSOR *gba, int bit) {
    if (gba->out.pos >= gba->out.limit) {
        dk_set_error("Tried to write out of bounds");
        return 1;
    }
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

    if (gba->in.length < 6) {
        dk_set_error("Unexpected end of input");
        return 1;
    }
    if ((gba->in.data[0] & 0xF0) != 0x20) {
        dk_set_error("Incorrect identifier for Huffman");
        return 1;
    }

    /* data sizes other than 8 are untested */
    data_size = gba->in.data[0] & 15;
    if (!data_size) {
        dk_set_error("Invalid data size? (0)");
        return 1;
    }
    if (data_size != 8) {
        dk_set_error("Unsupported data size");
        return 1;
    }

    /* size of the output data */
    output_size =  gba->in.data[1]
                | (gba->in.data[2] <<  8)
                | (gba->in.data[3] << 16);

    /* data offset */
    gba->in.pos = 4+2*(gba->in.data[4]+1);

    while (gba->out.pos < output_size) {
        int i, dir = read_bit(gba);
        if (dir < 0) return 1;
        if ((!dir && (node & 0x80))
        || (  dir && (node & 0x40))) { /* next is a leaf */
            node = read_tree(gba, 6+2*n+dir);
            for (i = 0; i < data_size; i++)
                if (write_out(gba, !!(node & (1 << i))) < 0)
                    return 1;
            node = n = 0;
        }
        else { /* next is a node */
            node = read_tree(gba, 6+2*n+dir);
            n += (node & 0x3F)+1;
        }
    }
    return 0;
}







/* compressor */

enum NODE_TYPE { CNODE, CLEAF };
struct NODE {
    enum NODE_TYPE type;
    int count;
    struct NODE *parent;
    union { struct { struct NODE *left, *right; }; int value; };
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

static int generate_leaves (struct COMPRESSOR *gba, struct NODE *leaf_tree) {
    struct VALC count[256];
    int i, leaf_count = 0;

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
        struct NODE *leaf = &leaf_tree[leaf_count++];
        leaf->type  = CLEAF;
        leaf->count = count[i].count;
        leaf->value = count[i].index;
    }
    return leaf_count;
}

static int generate_tree (
    struct NODE *node_tree,
    struct NODE *leaf_tree,
    int lqc
) {
    int nqc = 0; /* leaf/node queue count */
    int lqp = 0; /* leaf/node queue position */
    int nqp = 0;

    /* create the tree */
    while (lqp < lqc || nqp < nqc) {
        struct NODE *n[2] = { NULL, NULL };
        int i;

        /* dequeue the two smallest nodes */
        for (i = 0; i < 2; i++) {
            if (lqp < lqc && nqp < nqc) {
                if (leaf_tree[lqp].count < node_tree[nqp].count)
                    n[i] = &leaf_tree[lqp++];
                else
                    n[i] = &node_tree[nqp++];
            }
            else if (lqp < lqc) { n[i] = &leaf_tree[lqp++]; }
            else if (nqp < nqc) { n[i] = &node_tree[nqp++]; }
        }

        /* final node is the top node */
        if (n[1] == NULL) {
            n[0]->parent = NULL;
            nqp--;
            break;
        }
        else {
            /* enqueue a new node */
            struct NODE *nn = &node_tree[nqc++];
            nn->type = CNODE;
            nn->left  = n[0]; n[0]->parent = nn;
            nn->right = n[1]; n[1]->parent = nn;
            nn->count = n[0]->count + n[1]->count;
        }
    }
    return nqc;
}



/* create the node table in GBA format */
/* we use queues here instead of recursion to minimize the distance */
/* between nodes. i.e. The distance (n-1)/2 must fit into 6 bits. */
static int gba_tree (struct COMPRESSOR *gba, struct NODE *root) {

#define STACK_SIZE 256
    struct NODE_SET {
        struct NODE *node;
        int index;
    };
    struct NODE_SET stack[STACK_SIZE];
    struct NODE_SET *cstack = stack; /* current tier */
    struct NODE_SET *nstack = stack + STACK_SIZE / 2; /* next tier */
    int cqc = 1; /* how many nodes left to process */
    int nqc = 0; /* how many nodes in the next tier */
    int next_pair = 6;

    cstack[0].node  = root;
    cstack[0].index = 5;

    while (cqc) {
        struct NODE_SET *tstack;
        int i;
        for (i = 0; i < cqc; i++) {
            struct NODE_SET *ns = &cstack[i];
            struct NODE *n = ns->node;
            if (n->type == CLEAF) {
                gba->out.data[ns->index] = n->value;
            }
            else {
                /* enqueue children */
                struct NODE_SET  left = { n->left,  next_pair   };
                struct NODE_SET right = { n->right, next_pair+1 };
                nstack[nqc++] = left;
                nstack[nqc++] = right;

                int v = (((next_pair - ns->index) - 1) / 2)
                    | ((n->right->type == CLEAF) << 6)
                    | ((n->left ->type == CLEAF) << 7);

                /* write data */
                gba->out.data[ns->index] = v;
                next_pair += 2;
            }
        }
        tstack = cstack;
        cstack = nstack;
        nstack = tstack;
        cqc = nqc;
        if (cqc > STACK_SIZE/2) {
            dk_set_error("Huffman: exceeded allowed node distance");
            return 1;
        }
        nqc = 0;
    }
    return 0;
}

/* tree entries can be n (?) bits (we only do 8-bits at the moment) */
static int gba_header (
    struct COMPRESSOR *gba,
    struct NODE *root,
    int tree_size
) {
    size_t header_size = 5+tree_size;

    if (gba->out.limit < header_size) {
        dk_set_error("Output not large enough for tree table");
        return 1;
    }

    /* compression type = 2, data size = 8 */
    gba->out.data[0] = 0x28;

    /* decompressed size */
    gba->out.data[1] = gba->in.length;
    gba->out.data[2] = gba->in.length >>  8;
    gba->out.data[3] = gba->in.length >> 16;

    /* generate and write the GBA tree */
    if (gba_tree(gba, root))
        return 1;

    /* word align */
    if (header_size  & 3)
        header_size += 4 - (header_size & 3);
    gba->out.pos = header_size;
    gba->out.data[4] = (header_size-5)/2;
    return 0;
}




/* encoding data is faster with a lookup table */
struct VLUT {
    unsigned sequence;
    int bits;
};

static void create_lut (
    struct VLUT *vlut,
    struct NODE *root,
    struct NODE *leaf_tree, int leaf_count
) {
    int i;

    /* create LUT for data encoding */
    memset(vlut, 0, 256*sizeof(vlut));
    for (i = 0; i < leaf_count; i++) {
        struct NODE *n = &leaf_tree[i];
        struct VLUT *v = &vlut[n->value];
        int bits = 0;
        unsigned sequence = 0;

        /* traverse to form a sequence */
        while (n != root) {
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


static int write_bit (struct COMPRESSOR *gba, unsigned char val) {
    size_t addr = gba->out.pos + (3 ^ gba->out.bytepos);
    unsigned bit = 7 ^ gba->out.bitpos++;
    if (addr >= gba->out.limit) {
        dk_set_error("Tried to write out of bounds (output)");
        return 1;
    }
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
static int encode_data (
    struct COMPRESSOR *gba,
    struct VLUT *vlut
) {
    size_t i;
    for (i = 0; i < gba->in.length; i++) {
        struct VLUT v = vlut[gba->in.data[i]];
        while (v.bits--) {
            if (write_bit(gba, v.sequence & 1))
                return 1;
            v.sequence >>= 1;
        }
    }
    if (gba->out.bitpos || gba->out.bytepos) { /* excess */
        if ((gba->out.pos+4) > gba->out.limit) {
            dk_set_error("Tried to write out of bounds");
            return 1;
        }
        gba->out.pos += 4;
    }
    return 0;
}

int gbahuff20_compress (struct COMPRESSOR *gba) {
    struct NODE leaf_tree[256];
    struct NODE node_tree[256];
    struct VLUT vlut[256];
    struct NODE *root = NULL;
    int leaf_count, node_count, tree_size;

    leaf_count = generate_leaves(gba, leaf_tree);
    if (!leaf_count) {
        dk_set_error("Huffman: Couldn't produce any leaves.\n");
        return 1;
    }

    node_count = generate_tree(node_tree, leaf_tree, leaf_count);
    tree_size  = leaf_count + node_count;
    root       = &node_tree[node_count-1];
    create_lut(vlut, root, leaf_tree, leaf_count);

    if (gba_header (gba, root, tree_size)
    ||  encode_data(gba, vlut))
        return 1;
    return 0;
}

