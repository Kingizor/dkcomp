/* SPDX-License-Identifier: MIT
 * DK Compression/Decompression Library
 * Copyright (c) 2020-2022 Kingizor */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dkcomp.h"
#include "dk_internal.h"


/* Error reporting functions */
static const char *dk_error_msg = "";

const char *dk_get_error (void) {
    return dk_error_msg;
}
void dk_set_error (const char *error) {
    dk_error_msg = error;
}



/* File/Buffer handling */
static int check_size_comp (size_t input_size) {
    if (input_size > 0x10000) {
        dk_set_error("Input is too large to fit in a 16-bit bank. (n > 65536)");
        return 1;
    }
    if (input_size < 128) {
        dk_set_error("Input is too small. (n < 128)");
        return 1;
    }
    return 0;
}
static int check_size_decomp (size_t input_size) {
    if (input_size > 16777216) {
        dk_set_error("Input file is too large.");
        return 1;
    }
    if (input_size < 0x28) {
        dk_set_error("Input is too small. (n < 0x28)");
        return 1;
    }
    return 0;
}
static int check_input_mem (unsigned char *input, size_t input_size, int mode_comp) {
    if (input == NULL) {
        dk_set_error("Input pointer is NULL.");
        return 1;
    }
    if (mode_comp) {
        if (check_size_comp(input_size))
            return 1;
    }
    else {
        if (check_size_decomp(input_size))
            return 1;
    }
    return 0;
}

static int open_input_file (const char *fn, unsigned char **input, int *input_size, int ofs, int mode_comp) {
    FILE *f;
    long input_length;

    f = fopen(fn, "rb");
    if (f == NULL) {
        dk_set_error("Failed to open input file.");
        return 1;
    }

    if ((fseek(f, 0,   SEEK_END)  == -1)
    || ((input_length = ftell(f)) == -1)
    ||  (fseek(f, ofs, SEEK_SET)  == -1)) {
        dk_set_error("Failed to seek the input file.");
        goto error;
    }

    if (input_length <= ofs) {
        dk_set_error("File ofs is larger than the input file.");
        goto error;
    }

    if (mode_comp) {
        if (check_size_comp(input_length))
            goto error;
        *input_size = input_length;
    }
    else {
        if (check_size_decomp(input_length))
            goto error;
        if (ofs > input_length) {
            dk_set_error("Supplied decompression offset is larger than the input size.");
            goto error;
        }
        if (ofs < 0) {
            dk_set_error("Supplied decompresison offset is negative.");
            goto error;
        }
        *input_size = input_length - ofs;
        if (*input_size > 0x20028) {
            *input_size = 0x20028;
        }
        if (*input_size < 0x28) {
            dk_set_error("File size minus offset is too small.");
        }
    }

    *input = malloc(*input_size);
    if (*input == NULL) {
        dk_set_error("Failed to allocate memory for input buffer.");
        goto error;
    }

    if (fread(*input, 1, *input_size, f) != (size_t)*input_size) {
        dk_set_error("Failed to read from input file.");
        free(*input);
        goto error;
    }
    return 0;
error:
    if (f != NULL)
        fclose(f);
    return 1;
}

static int write_output_file (const char *fn, unsigned char *output, size_t output_size) {

    FILE *f;

    f = fopen(fn, "wb");
    if (f == NULL) {
        dk_set_error("Failed to open file for writing.");
        return 1;
    }

    if (fwrite(output, 1, output_size, f) != output_size) {
        dk_set_error("An error occurred while writing the output file.");
        fclose(f);
        return 1;
    }

    fclose(f);
    return 0;
}
static int open_output_buffer (unsigned char **output, size_t output_size) {
    *output = calloc(output_size, 1);
    if (*output == NULL) {
        dk_set_error("Failed to allocate memory for output buffer.");
        return 1;
    }
    return 0;
}






/* Check whether a compressor is supported */

int (*get_compressor(enum DK_FORMAT comp_type))(struct COMPRESSOR*) {
    switch (comp_type) {
        case     BD_COMP: { return bd_compress; }
        case     SD_COMP: { return sd_compress; }
        default: { break; }
    }
    dk_set_error("Unsupported compression type.\n");
    return NULL;
}



/* Compression handlers */

int dk_compress_mem_to_mem (
    enum DK_FORMAT comp_type,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
) {
    int (*dk_compress)(struct COMPRESSOR*);
    struct COMPRESSOR cmp;
    memset(&cmp, 0, sizeof(struct COMPRESSOR));

    if ((dk_compress = get_compressor(comp_type)) == NULL)
        return 1;
    if (check_input_mem(input, input_size, 1))
        return 1;

    cmp.input     = input;
    cmp.input_len = input_size;

    if (open_output_buffer(&cmp.output, OUTPUT_LIMIT))
        return 1;
    if (dk_compress(&cmp)) {
        free(cmp.output);
        return 1;
    }
    *output      = cmp.output;
    *output_size = cmp.outpos;
    return 0;
}

int dk_compress_file_to_mem (
    enum DK_FORMAT comp_type,
    unsigned char **output,
    size_t *output_size,
    const char *file_in
) {
    int (*dk_compress)(struct COMPRESSOR*);
    struct COMPRESSOR cmp;
    memset(&cmp, 0, sizeof(struct COMPRESSOR));

    if ((dk_compress = get_compressor(comp_type)) == NULL)
        return 1;
    if (open_input_file(file_in, &cmp.input, &cmp.input_len, 0, 1))
        goto error;
    if (open_output_buffer(&cmp.output, OUTPUT_LIMIT))
        goto error;
    if (dk_compress(&cmp))
        goto error;

    free(cmp.input);
    *output      = cmp.output;
    *output_size = cmp.outpos;
    return 0;
error:
    free(cmp.input);
    free(cmp.output);
    return 1;
}


int dk_compress_mem_to_file (
    enum DK_FORMAT comp_type,
    const char *file_out,
    unsigned char *input,
    size_t input_size
) {
    unsigned char *output;
    size_t output_size;

    if (dk_compress_mem_to_mem(comp_type, &output, &output_size, input, input_size))
        return 1;
    if (write_output_file(file_out, output, output_size)) {
        free(output);
        return 1;
    }
    free(output);
    return 0;
}

int dk_compress_file_to_file (
    enum DK_FORMAT comp_type,
    const char *file_out,
    const char *file_in
) {
    unsigned char *output;
    size_t output_size;

    if (dk_compress_file_to_mem(comp_type, &output, &output_size, file_in))
        return 1;
    if (write_output_file(file_out, output, output_size)) {
        free(output);
        return 1;
    }
    free(output);
    return 0;
}





/* Check whether a decompressor is supported */

int (*get_decompressor(enum DK_FORMAT decomp_type))(struct COMPRESSOR*) {
    switch (decomp_type) {
        case     BD_DECOMP: { return     bd_decompress; }
        case     SD_DECOMP: { return     sd_decompress; }
        case DKCCHR_DECOMP: { return dkcchr_decompress; }
        case DKCGBC_DECOMP: { return dkcgbc_decompress; }
        case    DKL_DECOMP: { return    dkl_decompress; }
        default: { break; }
    }
    dk_set_error("Unsupported decompression type.\n");
    return NULL;
}



/* Decompression handlers */

int dk_decompress_mem_to_mem (
    enum DK_FORMAT decomp_type,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
) {
    int (*dk_decompress)(struct COMPRESSOR*);
    struct COMPRESSOR dc;
    memset(&dc, 0, sizeof(struct COMPRESSOR));

    if ((dk_decompress = get_decompressor(decomp_type)) == NULL)
        return 1;
    if (check_input_mem(input, input_size, 0))
        return 1;

    dc.input     = input;
    dc.input_len = input_size;

    if (open_output_buffer(&dc.output, 0x10000))
        return 1;

    if (dk_decompress(&dc)) {
        free(dc.output);
        return 1;
    }
    *output      = dc.output;
    *output_size = dc.outpos;
    return 0;
}
int dk_decompress_file_to_mem (
    enum DK_FORMAT decomp_type,
    unsigned char **output,
    size_t *output_size,
    const char *file_in,
    size_t position
) {
    int (*dk_decompress)(struct COMPRESSOR*);
    struct COMPRESSOR dc;
    memset(&dc, 0, sizeof(struct COMPRESSOR));

    if ((dk_decompress = get_decompressor(decomp_type)) == NULL)
        return 1;
    if (open_input_file(file_in, &dc.input, &dc.input_len, position, 0))
        goto error;
    if (open_output_buffer(&dc.output, 0x10000))
        goto error;
    if (dk_decompress(&dc))
        goto error;

    free(dc.input);
    *output      = dc.output;
    *output_size = dc.outpos;
    return 0;
error:
    free(dc.input);
    free(dc.output);
    return 1;
}

int dk_decompress_mem_to_file (
    enum DK_FORMAT decomp_type,
    const char *file_out,
    unsigned char *input,
    size_t input_size
) {
    unsigned char *output;
    size_t output_size;

    if (dk_decompress_mem_to_mem(decomp_type, &output, &output_size, input, input_size))
        return 1;
    if (write_output_file(file_out, output, output_size)) {
        free(output);
        return 1;
    }
    free(output);
    return 0;
}
int dk_decompress_file_to_file (
    enum DK_FORMAT decomp_type,
    const char *file_out,
    const char *file_in,
    size_t position
) {
    unsigned char *output;
    size_t output_size;

    if (dk_decompress_file_to_mem(decomp_type, &output, &output_size, file_in, position))
        return 1;
    if (write_output_file(file_out, output, output_size)) {
        free(output);
        return 1;
    }
    free(output);
    return 0;
}


