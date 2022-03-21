#include <stdio.h>
#include <string.h>
#include "dkcomp.h"

int main (int argc, char *argv[]) {

    if (argc != 4) {
        puts("Usage: ./comp FORMAT OUTPUT INPUT\n\n"
             "Supported formats:\n"
             "bd - SNES DKC2/DKC3 Big Data\n"
             "sd - SNES      DKC3 Small Data"
        );
        return 1;
    }

    static const struct DK_ID {
        enum DK_FORMAT id;
        char *abbrev;
    } formats[] = {
        {    BD_COMP, "bd" },
        {    SD_COMP, "sd" }
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
        fprintf(stderr, "Unsupported compression format.\n");
        return 1;
    }

    if (dk_compress_file_to_file(format, argv[2], argv[3])) {
        fprintf(stderr, "Error: %s.\n", dk_get_error());
        return 1;
    }
    if (dk_get_error() != NULL)
        printf("Error says: %s.\n", dk_get_error());
    printf("Done.\n");

    return 0;
}
