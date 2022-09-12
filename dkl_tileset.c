/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - DKL Huffman Tileset Functions */

#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

/* We provide three functions for dealing with the DKL Huffman format:
 * dkl_huffman_decode - decompress data using a existing tree
 * dkl_huffman_encode -   compress data using a existing tree
 * dkl_huffman_tree   - create a tree from data
 */


/* decompressor */

static int read_byte (unsigned char *data, size_t size, size_t pos) {
    if (pos >= size)
        return -1;
    return data[pos];
}

/* Tileset data for all DKL games */
/* A single Huffman table for all data is stored at 3D00-3FFF */
/*  left nodes in 3Exx with types at 3Fxx.7 */
/* right nodes in 3Dxx with types at 3Fxx.3 */
SHARED int dkl_huffman_decode (
    unsigned char   *input, size_t   insize,
    unsigned char **output, size_t *outsize,
    unsigned char    *tree, /* &rom[0x3D00] */
    size_t count /* how many tiles to output until we stop */
) {
    size_t rpos = 0;
    size_t wpos = 0;
    unsigned char node = 0xFE;

    *output = malloc(0x10 * count);
    if (*output == NULL)
        return DK_ERROR_ALLOC;

    for (;;) {
        int c;
        if ((c = read_byte(input, insize, rpos++)) < 0)
            return DK_ERROR_OOB_INPUT;

        for (int i = 0; i < 8; i++) {
            int a = tree[0x200|node]; /* a.73 = LR.format */
            int b;

            if (c & (0x80 >> i)) {
                a &= 0x80;
                b = tree[0x100|node]; /* L */
            }
            else {
                a &= 8;
                b = tree[      node]; /* R */
            }

            if (a) { /* traverse to next node */
                node = b;
            }
            else { /* write value and return to root */
                if (wpos > count) {
                    free(*output); *output = NULL;
                    return DK_ERROR_OOB_OUTPUT_W;
                }
                (*output)[wpos++] = b;
                node = 0xFE;
                if (!(wpos & 15) && !(--count))
                    goto quit;
            }
        }
    }
    *outsize = 0x10 * count;
quit:
    return 0;
}




/* compressor */

struct LUTITEM {
    unsigned path;
    unsigned size;
};

/* Generate a LUT so we can encode data quickly */
static void generate_LUT (
    unsigned char *tree,
    unsigned char node,
    struct LUTITEM *nodelist,
    struct LUTITEM *current
) {
    int flag = tree[node|0x200];
    int L    = tree[node|0x100];
    int R    = tree[node];
    struct LUTITEM nextL = { 1|(current->path << 1), current->size+1 };
    struct LUTITEM nextR = {   (current->path << 1), current->size+1 };

    if (flag & 0x80)
        generate_LUT(tree, L, nodelist, &nextL);
    else {
        nodelist[L] = nextL;
    }

    if (flag & 8)
        generate_LUT(tree, R, nodelist, &nextR);
    else {
        nodelist[R] = nextR;
    }
}

static int write_bit (
    unsigned char *output,
    size_t outsize,
    size_t *wpos,
    size_t *bitpos,
    unsigned path
) {
    if (*wpos >= outsize)
        return 1;
    output[*wpos] |= (path & 1) << (7 ^ *bitpos);
    *bitpos += 1;
    if (*bitpos == 8) {
        *bitpos  = 0;
        *wpos   += 1;
    }
    return 0;
}

/* encode tile data using an existing tree */
SHARED int dkl_huffman_encode (
    unsigned char  *input,  size_t   insize,
    unsigned char **output, size_t *outsize,
    unsigned char  *tree
) {
    size_t wpos = 0, bitpos = 0, i;
    struct LUTITEM base = { 0, 0 };
    struct LUTITEM nodelist[256];
    memset(nodelist, 0, sizeof(nodelist));

    /* tilesets are always decompressed into 8800-97FF */
    if (insize > 0x1000)
        return DK_ERROR_INPUT_LARGE;

    *output = calloc(2*insize, 1);
    if (*output == NULL)
        return DK_ERROR_ALLOC;

    generate_LUT(tree, 0xFE, nodelist, &base);

    for (i = 0; i < insize; i++) {
        struct LUTITEM path = nodelist[input[i]];
        while (path.size--) {
            int bit = !!(path.path & (1 << path.size));
            if (write_bit(*output, insize, &wpos, &bitpos, bit)) {
                free(*output); *output = NULL;
                return DK_ERROR_OOB_OUTPUT_W;
            }
        }
    }
    if (bitpos)
        wpos += 1;
    *outsize = wpos;
    return 0;
}





/* tree generator */

struct VALC {
    size_t count;
    unsigned char index;
};
int valc_sort (const void *aa, const void *bb) {
    const struct VALC *a = aa, *b = bb;
    return (a->count > b->count) ? -1
         : (a->count < b->count);
}

enum NODE_TYPE { CNODE, CLEAF };
struct NODE {
    enum NODE_TYPE type;
    size_t count;
    union { struct { struct NODE *left, *right; }; int value; };
};

struct BIN {
    unsigned char *input;
    size_t insize;
    unsigned char *output;
    struct NODE tree[511];
    struct NODE *root;
    int node_count;
};


static void generate_leaves (struct BIN *bin) {
    size_t i;
    struct VALC count[256];
    bin->node_count = 0;

    for (i = 0; i < 256; i++) {
        count[i].count = 0;
        count[i].index = i;
    }

    /* count bytes and sort */
    for (i = 0; i < bin->insize; i++)
        count[bin->input[i]].count++;
    qsort(count, 256, sizeof(struct VALC), valc_sort);

    /* discard unused values */
    for (i = 0; i < 256; i++)
        if (!count[i].count)
            break;

    /* create a leaf for every value */
    while (i--) {
        struct NODE *leaf = &bin->tree[bin->node_count++];
        leaf->type  = CLEAF;
        leaf->count = count[i].count;
        leaf->value = count[i].index;
    }
}

/* copy/paste from GBA_HUFF20  */
static void generate_tree (struct BIN *bin) {
    struct NODE *tree = bin->tree;
    struct NODE leaf_tree[256];
    struct NODE node_tree[255];
    int lqc = bin->node_count;
    int nqc = 0;
    int lqp = 0;
    int nqp = 0;
    int  ts = 0;

    memcpy(leaf_tree, tree, lqc * sizeof(struct NODE));

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
    bin->node_count = ts;
}

static void generate_dkltree (struct BIN *bin, struct NODE *node, unsigned char *pos) {
    unsigned char cpos = *pos;
    *pos -= 1;

    if (node->left->type == CLEAF) {
        bin->output[cpos|0x100]  = node->left->value;
    }
    else {
        bin->output[cpos|0x200] |= 0x80;
        bin->output[cpos|0x100]  = *pos;
        generate_dkltree(bin, node->left, pos);
    }

    if (node->right->type == CLEAF) {
        bin->output[cpos]  = node->right->value;
    }
    else {
        bin->output[cpos|0x200] |= 0x08;
        bin->output[cpos]  = *pos;
        generate_dkltree(bin, node->right, pos);
    }

}

/* this function generates a 0x300 byte tree from input data.
 * all Huffman data in each DKL game uses the same tree, so all data
 * would have to be examined when doing this. Just:
 * 1) decompress all the huffman data and concatenate it
 * 2) pass the concatenated data to this function to generate a tree
 * 3) compress the individual data segments using the new tree */
SHARED int dkl_huffman_tree (
    unsigned char *input,
    size_t insize,
    unsigned char **tree
) {
    struct BIN bin;
    unsigned char pos = 0xFE;
    memset(&bin, 0, sizeof(struct BIN));
    bin.input  = input;
    bin.insize = insize;
    bin.output = calloc(0x300, 1);
    if (bin.output == NULL)
        return DK_ERROR_ALLOC;
    *tree = bin.output;
    generate_leaves (&bin);
    generate_tree   (&bin);
    generate_dkltree(&bin, bin.root, &pos);
    return 0;
}

