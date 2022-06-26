#include <stdlib.h>
#include "dk_internal.h"

/* auto-detect GBA BIOS Huffman/LZ77/RLE formats */

int gba_decompress (struct COMPRESSOR *gba) {
    if (gba->in.length < 1) {
        dk_set_error("Input data too short!");
        return 1;
    }

    switch (*gba->in.data >> 4) {
        case 1: { return   gbalz77_decompress(gba); }
        case 2: { return gbahuff20_decompress(gba); }
        case 3: { return    gbarle_decompress(gba); }
        case 5: { return gbahuff50_decompress(gba); }
        case 6: { return gbahuff60_decompress(gba); }
    }
    dk_set_error("Unable to detect compression type.");
    return 1;
}

