#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dkcomp.h"

static void check_size (const char *name) {
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

    printf("Output size is %zd bytes.\n", len);
}

int main (int argc, char *argv[]) {

    static const struct DK_ID {
        enum DK_FORMAT id;
        char *name;
    } formats[] = {
        {        BD_COMP, "SNES DKC2/DKC3 Big Data"},
        {        SD_COMP, "SNES DKC3 Small Data"   },
        {    DKCCHR_COMP, "SNES DKC Tile Data"     },
        {    DKCGBC_COMP, " GBC DKC Layout"        },
        {GBA_HUFF60_COMP, " GBA Huffman (60)"      },
        {GBA_HUFF50_COMP, " GBA Huffman (50)"      },
        {GBA_HUFF20_COMP, " GBA BIOS Huffman"      },
        {  GBA_LZ77_COMP, " GBA BIOS LZ77"         },
        {   GBA_RLE_COMP, " GBA BIOS RLE"          }
    };
    static const int size = sizeof(formats) / sizeof(struct DK_ID);
    int i, format = 0;

    if (argc != 4) {
        puts("Usage: ./comp FORMAT OUTPUT INPUT\n\n"
             "Supported compression formats:");
        for (i = 0; i < size; i++)
            printf("  %2d - %s\n", i, formats[i].name);
        return 1;
    }

    format = strtol(argv[1], NULL, 0);
    if (format < 0 || format >= size) {
        fprintf(stderr, "Unsupported compression format.\n");
        return 1;
    }

    if (dk_compress_file_to_file(formats[format].id, argv[2], argv[3])) {
        fprintf(stderr, "Error: %s.\n", dk_get_error());
        return 1;
    }

    check_size(argv[2]);
    printf("Done.\n");

    return 0;
}

