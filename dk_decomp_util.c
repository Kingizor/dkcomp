/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2022 Kingizor
 * dkcomp library - decompression utility */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dkcomp.h"

static void check_size (const char *name, size_t compressed_size) {
    FILE *f = fopen(name, "rb");
    size_t len;
    if (f == NULL) {
        fprintf(stderr, "Failed to open output file. (size check)\n");
        return;
    }

    if (fseek(f, 0, SEEK_END) == -1
    || (len = ftell(f)) == -1u) {
        fprintf(stderr, "Failed to seek output file. (size check)\n");
        fclose(f);
        return;
    }
    fclose(f);

    printf("  Compressed size was %zd bytes.\n", compressed_size);
    printf("Decompressed size  is %zd bytes.\n", len);
}

int main (int argc, char *argv[]) {

    static const struct DK_ID {
        enum DK_FORMAT id;
        char *name;
    } formats[] = {
        {        BD_COMP, "SNES DKC2/DKC3 Big Data"    },
        {        SD_COMP, "SNES DKC3 Small Data"       },
        {    DKCCHR_COMP, "SNES DKC Tilesets"          },
        {    DKCGBC_COMP, " GBC DKC Tilemaps"          },
        {       DKL_COMP, " GB  DKL/DKL2/DKL3 Tilemaps"},
        {  GBA_LZ77_COMP, " GBA BIOS LZ77 (10)"        },
        {GBA_HUFF20_COMP, " GBA BIOS Huffman (20)"     },
        {   GBA_RLE_COMP, " GBA BIOS RLE (30)"         },
        {GBA_HUFF50_COMP, " GBA Huffman (50)"          },
        {GBA_HUFF60_COMP, " GBA Huffman (60)"          },
        {       GBA_COMP, " GBA BIOS Auto-Detect"      }
    };
    static const int size = sizeof(formats) / sizeof(struct DK_ID);
    int e, i, format = 0;
    size_t offset;
    size_t compressed_size = 0;

    if (argc != 5) {
        puts("Usage: ./decomp FORMAT OUTPUT INPUT POSITION\n\n"
             "Supported decompression formats:");
        for (i = 0; i < size; i++)
            printf("  %2d - %s\n", i, formats[i].name);
        return 1;
    }

    format = strtol(argv[1], NULL, 0);
    if (format < 0 || format >= size) {
        fprintf(stderr, "Unsupported decompression format.\n");
        return 1;
    }

    offset = strtol(argv[4], NULL, 0);

    if ((e = dk_compressed_size_file(formats[format].id, argv[3], offset, &compressed_size))) {
        fprintf(stderr, "Error: %s.\n", dk_get_error(e));
        return 1;
    }

    if ((e = dk_decompress_file_to_file(formats[format].id, argv[2], argv[3], offset))) {
        fprintf(stderr, "Error: %s.\n", dk_get_error(e));
        return 1;
    }

    check_size(argv[2], compressed_size);
    printf("Done.\n");

    return 0;
}

