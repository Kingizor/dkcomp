/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - GBA auto-detect decompressor */

#include <stdlib.h>
#include "dk_internal.h"

int gba_decompress (struct COMPRESSOR *gba) {
    if (gba->in.length < 5)
        return DK_ERROR_EARLY_EOF;

    switch (*gba->in.data >> 4) {
        case 1: { return   gbalz77_decompress(gba); }
        case 2: { return gbahuff20_decompress(gba); }
        case 3: { return    gbarle_decompress(gba); }
        case 5: { return gbahuff50_decompress(gba); }
        case 6: { return gbahuff60_decompress(gba); }
    }
    return DK_ERROR_GBA_DETECT;
}

