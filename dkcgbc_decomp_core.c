#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include "dk_internal.h"

static int rb (struct COMPRESSOR *gbc) { /* Read from input */
    if (gbc->inpos >= gbc->input_len) {
        dk_set_error("Input: Tried to read out of bounds.\n");
        return -1;
    }
    return gbc->input[gbc->inpos++];
}
static int ro (struct COMPRESSOR *gbc, int addr) { /* Read from output */
    addr = gbc->outpos - addr;
    if (addr < 0 || addr >= gbc->outpos) {
        dk_set_error("Output: Tried to read out of bounds.\n");
        return -1;
    }
    return gbc->output[addr];
}
static int wb (struct COMPRESSOR *gbc, int val) { /* Write to output */
    if (gbc->outpos >= 0x10000) {
        dk_set_error("Output: Tried writing out of bounds.\n");
        return -1;
    }
    gbc->output[gbc->outpos++] = val;
    return 0;
}

static int dkcc_decomp (struct COMPRESSOR *gbc) {

    int c = rb(gbc);
    if (!c)    return 1; /* Done */
    if (c < 0) return 2;

    switch (c >> 6) {
        default: { /* Single byte, c times */
            int v = rb(gbc);
            if (v < 0)
                return 2;
            while (c--)
                if (wb(gbc, v) < 0)
                    return 2;
            break;
        }
        case 2: { /* c bytes from input */
            c &= 0x3F;
            while (c--)
                if (wb(gbc, rb(gbc)) < 0)
                    return 2;
            break;
        }
        case 3: { /* c bytes from output */
            int pos = rb(gbc);
            if (pos < 0)
                return 2;
            c &= 0x3F;
            while (c--)
                if (wb(gbc, ro(gbc, pos)) < 0)
                    return 2;
            break;
        }
    }
    return 0;
}

int dkcgbc_decompress (struct COMPRESSOR *gbc) {
    for (;;) {
        switch (dkcc_decomp(gbc)) {
            case 0: { continue; }
            case 1: { return 0; }
            case 2: { return 1; }
        }
    }
    return 0;
}

