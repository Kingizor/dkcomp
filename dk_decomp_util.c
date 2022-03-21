#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dkcomp.h"

int main (int argc, char *argv[]) {

    if (argc != 5) {
        puts("Usage: ./decomp FORMAT OUTPUT INPUT POSITION\n\n"
             "Supported formats:\n"
             "    bd - SNES DKC2/DKC3 Big Data\n"
             "    sd - SNES      DKC3 Small Data\n"
             "dkcchr - SNES DKC Tileset\n"
             "dkcgbc -  GBC DKC Layout\n"
             "dkl    -  GB  DKL/DKL2/DKL3 tilemaps"
        );
        return 1;
    }

    static const struct DK_ID {
        enum DK_FORMAT id;
        char *abbrev;
    } formats[] = {
        {    BD_DECOMP, "bd" },
        {    SD_DECOMP, "sd" },
        {DKCCHR_DECOMP, "dkcchr" },
        {DKCGBC_DECOMP, "dkcgbc" },
        {   DKL_DECOMP, "dkl" }
    };
    static const int size = sizeof(formats) / sizeof(struct DK_ID);

    int i, format = 0;
    for (i = 0; i < size; i++) {
        const struct DK_ID *f = &formats[i];
        if (!strcmp(argv[1], f->abbrev)) {
            format = f->id;
            break;
        }
    }
    if (i == size) {
        fprintf(stderr, "Unsupported decompression format.\n");
        return 1;
    }

    if (dk_decompress_file_to_file(format, argv[2], argv[3], strtol(argv[4], NULL, 0))) {
        fprintf(stderr, "Error: %s.\n", dk_get_error());
        return 1;
    }

    FILE *f = fopen(argv[2], "rb");
    if (fseek(f, 0, SEEK_END)) {
        fprintf(stderr, "seek_error\n");
        fclose(f);
        return 1;
    }
    size_t len = ftell(f);

    printf("Output size is %zd bytes.\n", len);

    printf("Done.\n");

    return 0;
}
