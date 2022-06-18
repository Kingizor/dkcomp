#include <stdlib.h>
#include <string.h>
#include "dk_internal.h"

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
    struct COMPRESSOR *dk;
    struct NODE *tree;
};

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
        return -1;
    }
    dk->out.data[dk->out.pos++] = out;
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

static int add_leaf (struct BIN *bin, int nc, unsigned char val) {
    struct NODE *tree = bin->tree;
    struct NODE new_leaf =
        { CLEAF, 0, nc-1, .val=val };
    struct NODE new_node =
        { CNODE, 1, tree[nc-1].parent, .dir.L = nc, .dir.R = nc+1 };

    /* add the new leaf */
    tree[nc+1] = new_leaf;

    /* move the old node */
    tree[nc]   = tree[nc-1];
    tree[nc].parent = nc-1;

    /* add the new node */
    tree[nc-1] = new_node;

    return nc+1;
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

int rarehuf_decompress (struct COMPRESSOR *dk) {

    /* tree consists of:
         - two special leafs     (the "new value" and "quit" commands)
         - up to 256 value leafs (each unique byte encountered so far)
         - nodes to connect every leaf
    */
    struct NODE tree[515] = {
        {CNODE, 2, -1, .dir.L = 1, .dir.R = 2 },
        {CLEAF, 1,  0, .val = 0x100 }, /* quit */
        {CLEAF, 1,  0, .val = 0x101 }  /* new leaf */
    };
    int node_count = 3;
    struct BIN bin = { dk, tree };

    for (;;) {
        int out  = 0;
        int quit = 0;
        int node = 0;

        /* traverse the tree for a value */
        while (tree[node].type == CNODE) {
            switch (read_bit(dk)) {
                case 0: { node = tree[node].dir.L; break; }
                case 1: { node = tree[node].dir.R; break; }
               default: { return 1; }
            }
        }

        /* process the value */
        switch (tree[node].val) {
               default: { out = tree[node].val; break; }
            case 0x100: { quit = 1; break; }
            case 0x101: {
                int i;
                for (i = 0; i < 8; i++) {
                    int bit = read_bit(dk);
                    if (bit < 0)
                        return 1;
                    out <<= 1;
                    out |= bit;
                }
                node = add_leaf(&bin, node_count, out);
                node_count += 2;
                break;
            }
        }
        if (quit)
            break;
        if (write_byte(dk, out))
            return 1;
        if (tree->weight >= 0x8000)
            rebuild_tree(&bin, node_count);

        update_weights(&bin, node);
    }
    return 0;
}






/* compressor */

static int write_bit (struct COMPRESSOR *dk, unsigned char val) {
    size_t addr = dk->out.pos + dk->out.bytepos;
    unsigned bit = dk->out.bitpos++;
    if (addr >= dk->out.limit) {
        dk_set_error("Tried to write out of bounds (output)");
        return 1;
    }
    dk->out.data[addr] &= ~(1 << bit);
    dk->out.data[addr] |= val << bit;
    if (dk->out.bitpos == 8) {
        dk->out.bitpos  = 0;
        dk->out.pos++;
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
        if (write_bit(bin->dk, bit))
            return 1;
    }
    return 0;
}

int rarehuf_compress (struct COMPRESSOR *dk) {

    struct NODE tree[515] = {
        { CNODE, 2, -1, .dir.L = 1, .dir.R = 2 },
        { CLEAF, 1,  0, .val = 0x100 },
        { CLEAF, 1,  0, .val = 0x101 }
    };
    int node_count = 3;
    struct BIN bin = { dk, tree };

    while (dk->in.pos < dk->in.length) {
        int  val = dk->in.data[dk->in.pos++];
        int node = nsearch(tree, node_count, val);
        if (!node) { /* leaf not present in tree, so add a new leaf */
            int i;

            /* send the new leaf command */
            if (encode_leaf(&bin, &tree[nsearch(tree, node_count, 0x101)]))
                return 1;

            /* write the value */
            for (i = 0; i < 8; i++)
                if (write_bit(dk, (val >> (7^i)) & 1))
                    return 1;

            /* add the leaf to the tree */
            node = add_leaf(&bin, node_count, val);
            node_count += 2;
        }
        else { /* use the existing leaf */
            if (encode_leaf(&bin, &tree[node]))
                return 1;
        }
        if (tree->weight >= 0x8000)
            rebuild_tree(&bin, node_count);
        update_weights(&bin, node);
    }

    /* quit */
    if (encode_leaf(&bin, &tree[nsearch(tree, node_count, 0x100)]))
        return 1;

    /* excess */
    if (dk->out.bitpos || dk->out.bytepos) {
        if ((dk->out.pos+1) > dk->out.limit) {
            dk_set_error("Tried to write out of bounds");
            return 1;
        }
        dk->out.pos++;
    }
    return 0;
}

