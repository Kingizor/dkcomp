/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2022 Kingizor
 * dkcomp library - application programming interface */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dkcomp.h"
#include "dk_internal.h"


/* verify compressed data by decompressing and comparing */
/* some formats pad input data which can make this fail */
#define VERIFY_DATA 0


/* Error reporting functions */
static const char *dk_error_msg = "";

const char *dk_get_error (void) {
    return dk_error_msg;
}
void dk_set_error (const char *error) {
    dk_error_msg = error;
}



/* File/Buffer handling */

static int check_input_mem (unsigned char *input) {
    if (input == NULL) {
        dk_set_error("Input pointer is NULL.");
        return 1;
    }
    return 0;
}

static int open_input_file (
    const char *fn,
    unsigned char **input,
    size_t *input_size,
    int ofs,
    int mode_comp
) {
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
        *input_size = input_length;
    }
    else {
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


#if VERIFY_DATA
/* verify compressed data by decompressing it and comparing */
static int verify_data (enum DK_FORMAT comp_type, struct COMPRESSOR *cmp) {
    unsigned char *data = NULL;
    size_t size = 0;
    if (dk_decompress_mem_to_mem(comp_type, &data, &size, cmp->out.data, cmp->out.pos)) {
        dk_set_error("Failed to decompress compressed data (corruption)");
        goto error;
    }
    if (size != cmp->in.length) {
        dk_set_error("Decompressed size doesn't match the original data");
        goto error;
    }
    if (memcmp(data, cmp->in.data, size)) {
        dk_set_error("Decompressed data doesn't match the original data");
        goto error;
    }
    free(data);
    return 0;
error:
    free(data);
    return 1;
}
#endif






/* Check whether a compressor is supported */

struct COMP_TYPE {
    unsigned size_limit; /* 1 << n */
    int (  *comp)(struct COMPRESSOR*);
    int (*decomp)(struct COMPRESSOR*);
};

static const struct COMP_TYPE comp_table[] = {
    [        BD_COMP] = { 16,        bd_compress,        bd_decompress },
    [        SD_COMP] = { 16,        sd_compress,        sd_decompress },
    [    DKCCHR_COMP] = { 16,    dkcchr_compress,    dkcchr_decompress },
    [    DKCGBC_COMP] = { 12,    dkcgbc_compress,    dkcgbc_decompress },
    [       DKL_COMP] = { 16,               NULL,       dkl_decompress },
    [GBA_HUFF60_COMP] = { 24, gbahuff60_compress, gbahuff60_decompress },
    [GBA_HUFF50_COMP] = { 24, gbahuff50_compress, gbahuff50_decompress },
    [GBA_HUFF20_COMP] = { 24, gbahuff20_compress, gbahuff20_decompress },
    [  GBA_LZ77_COMP] = { 24,   gbalz77_compress,   gbalz77_decompress },
    [   GBA_RLE_COMP] = { 24,    gbarle_compress,    gbarle_decompress },
    [       GBA_COMP] = { 24,               NULL,       gba_decompress }
};


/* Check whether a (de)compressor is supported */
const struct COMP_TYPE *get_compressor (int index, int type) {
    if (index < 0
    ||  index >= COMP_LIMIT
    ||  ( type && comp_table[index].  comp == NULL)
    ||  (         comp_table[index].decomp == NULL)) {
        if (type)
            dk_set_error("Unsupported compression type.\n");
        else
            dk_set_error("Unsupported decompression type.\n");
        return NULL;
    }
    return &comp_table[index];
}




/* Compression handlers */

int dk_compress_mem_to_mem (
    enum DK_FORMAT comp_type,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
) {
    const struct COMP_TYPE *dk_compress = get_compressor(comp_type, 1);
    struct COMPRESSOR cmp;
    memset(&cmp, 0, sizeof(struct COMPRESSOR));

    if (dk_compress == NULL)
        return 1;
    if (check_input_mem(input))
        return 1;

    cmp.in.data   = input;
    cmp.in.length = input_size;
    cmp.out.limit = 1 << dk_compress->size_limit;

    if (open_output_buffer(&cmp.out.data, cmp.out.limit))
        return 1;
    if (dk_compress->comp(&cmp)) {
        free(cmp.out.data);
        return 1;
    }
#if VERIFY_DATA
    if (verify_data(comp_type, &cmp)) {
        free(cmp.out.data);
        return 1;
    }
#endif

    *output      = cmp.out.data;
    *output_size = cmp.out.pos;
    return 0;
}

int dk_compress_file_to_mem (
    enum DK_FORMAT comp_type,
    unsigned char **output,
    size_t *output_size,
    const char *file_in
) {
    const struct COMP_TYPE *dk_compress = get_compressor(comp_type, 1);
    struct COMPRESSOR cmp;
    memset(&cmp, 0, sizeof(struct COMPRESSOR));

    if (dk_compress == NULL)
        return 1;
    if (open_input_file(file_in, &cmp.in.data, &cmp.in.length, 0, 1))
        goto error;
    cmp.out.limit = 1 << dk_compress->size_limit;
    if (open_output_buffer(&cmp.out.data, cmp.out.limit))
        goto error;
    if (dk_compress->comp(&cmp))
        goto error;
#if VERIFY_DATA
    if (verify_data(comp_type, &cmp))
        goto error;
#endif

    free(cmp.in.data);
    *output      = cmp.out.data;
    *output_size = cmp.out.pos;
    return 0;
error:
    free(cmp.in.data);
    free(cmp.out.data);
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





/* Decompression handlers */

int dk_decompress_mem_to_mem (
    enum DK_FORMAT decomp_type,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
) {
    const struct COMP_TYPE *dk_decompress = get_compressor(decomp_type, 0);
    struct COMPRESSOR dc;
    memset(&dc, 0, sizeof(struct COMPRESSOR));

    if (dk_decompress == NULL)
        return 1;
    if (check_input_mem(input))
        return 1;

    dc.in.data   = input;
    dc.in.length = input_size;
    dc.out.limit = 1 << dk_decompress->size_limit;

    if (open_output_buffer(&dc.out.data, dc.out.limit))
        return 1;

    if (dk_decompress->decomp(&dc)) {
        free(dc.out.data);
        return 1;
    }
    *output      = dc.out.data;
    *output_size = dc.out.pos;
    return 0;
}
int dk_decompress_file_to_mem (
    enum DK_FORMAT decomp_type,
    unsigned char **output,
    size_t *output_size,
    const char *file_in,
    size_t position
) {
    const struct COMP_TYPE *dk_decompress = get_compressor(decomp_type, 0);
    struct COMPRESSOR dc;
    memset(&dc, 0, sizeof(struct COMPRESSOR));

    if (dk_decompress == NULL)
        return 1;
    if (open_input_file(file_in, &dc.in.data, &dc.in.length, position, 0))
        goto error;
    dc.out.limit = 1 << dk_decompress->size_limit;
    if (open_output_buffer(&dc.out.data, dc.out.limit))
        goto error;
    if (dk_decompress->decomp(&dc))
        goto error;

    free(dc.in.data);
    *output      = dc.out.data;
    *output_size = dc.out.pos;
    return 0;
error:
    free(dc.in.data);
    free(dc.out.data);
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


