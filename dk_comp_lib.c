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



/* File/Buffer handling */

static int check_input_mem (unsigned char *input) {
    if (input == NULL)
        return DK_ERROR_NULL_INPUT;
    return 0;
}

static int open_input_file (
    const char *fn,
    unsigned char **input,
    size_t *input_size,
    size_t ofs,
    int mode_comp
) {
    FILE *f;
    long input_length;

    f = fopen(fn, "rb");
    if (f == NULL)
        return DK_ERROR_FILE_INPUT;

    if ((fseek(f, 0,   SEEK_END)  == -1)
    || ((input_length = ftell(f)) == -1)
    ||  (fseek(f, ofs, SEEK_SET)  == -1)) {
        fclose(f);
        return DK_ERROR_SEEK_INPUT;
    }

    if ((long)ofs < 0) {
        fclose(f);
        return DK_ERROR_OFFSET_NEG;
    }

    *input_size = input_length;

    if (ofs >= *input_size) {
        fclose(f);
        return DK_ERROR_OFFSET_BIG;
    }

    if (!mode_comp)
        *input_size -= ofs;

    *input = calloc(*input_size, 1);
    if (*input == NULL) {
        fclose(f);
        return DK_ERROR_ALLOC;
    }

    if (fread(*input, 1, *input_size, f) != (size_t)*input_size) {
        free(*input); *input = NULL;
        fclose(f);
        return DK_ERROR_FREAD;
    }
    return 0;
}

static int write_output_file (const char *fn, unsigned char *output, size_t output_size) {
    FILE *f;
    f = fopen(fn, "wb");
    if (f == NULL)
        return DK_ERROR_FILE_OUTPUT;

    if (fwrite(output, 1, output_size, f) != output_size) {
        fclose(f);
        return DK_ERROR_FWRITE;
    }
    fclose(f);
    return 0;
}
static int open_output_buffer (unsigned char **output, size_t output_size) {
    *output = calloc(output_size, 1);
    if (*output == NULL)
        return DK_ERROR_ALLOC;
    return 0;
}


#if VERIFY_DATA
/* verify compressed data by decompressing it and comparing */
static int verify_data (enum DK_FORMAT comp_type, struct COMPRESSOR *cmp) {
    unsigned char *data = NULL;
    size_t size = 0;
    if (dk_decompress_mem_to_mem(comp_type, &data, &size, cmp->out.data, cmp->out.pos))
        return DK_ERROR_VERIFY_DEC;
    if (size != cmp->in.length) {
        free(data);
        return DK_ERROR_VERIFY_SIZE;
    }
    if (memcmp(data, cmp->in.data, size)) {
        free(data);
        return DK_ERROR_VERIFY_DATA;
    }
    free(data);
    return 0;
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
static int get_compressor (int index, int type, const struct COMP_TYPE **comp) {
    if (index < 0
    ||  index >= COMP_LIMIT
    ||  ( type && comp_table[index].  comp == NULL)
    ||  (         comp_table[index].decomp == NULL)) {
        return type ? DK_ERROR_COMP_NOT
                    : DK_ERROR_DECOMP_NOT;
    }
    *comp = &comp_table[index];
    return 0;
}




/* Compression handlers */

SHARED int dk_compress_mem_to_mem (
    enum DK_FORMAT comp_type,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
) {
    enum DK_ERROR e;
    const struct COMP_TYPE *dk_compress;
    struct COMPRESSOR cmp;
    memset(&cmp, 0, sizeof(struct COMPRESSOR));

    if ((e = get_compressor(comp_type, 1, &dk_compress))
    ||  (e = check_input_mem(input)))
        goto error;

    cmp.in.data   = input;
    cmp.in.length = input_size;
    cmp.out.limit = 1 << dk_compress->size_limit;

    if ((e = open_output_buffer(&cmp.out.data, cmp.out.limit))
    ||  (e = dk_compress->comp(&cmp)))
        goto error;
#if VERIFY_DATA
    if ((e = verify_data(comp_type, &cmp)))
        goto error;
#endif

    *output      = cmp.out.data;
    *output_size = cmp.out.pos;
    return 0;
error:
    free(cmp.out.data); cmp.out.data = NULL;
    return e;
}

SHARED int dk_compress_file_to_mem (
    enum DK_FORMAT comp_type,
    unsigned char **output,
    size_t *output_size,
    const char *file_in
) {
    enum DK_ERROR e;
    const struct COMP_TYPE *dk_compress;
    struct COMPRESSOR cmp;
    memset(&cmp, 0, sizeof(struct COMPRESSOR));

    if ((e = get_compressor(comp_type, 1, &dk_compress))
    ||  (e = open_input_file(file_in, &cmp.in.data, &cmp.in.length, 0, 1)))
        goto error;
    cmp.out.limit = 1 << dk_compress->size_limit;
    if ((e = open_output_buffer(&cmp.out.data, cmp.out.limit))
    ||  (e = dk_compress->comp(&cmp)))
        goto error;
#if VERIFY_DATA
    if (e = verify_data(comp_type, &cmp))
        goto error;
#endif

    free(cmp.in.data); cmp.in.data = NULL;
    *output      = cmp.out.data;
    *output_size = cmp.out.pos;
    return 0;
error:
    free(cmp. in.data); cmp. in.data = NULL;
    free(cmp.out.data); cmp.out.data = NULL;
    return e;
}

SHARED int dk_compress_mem_to_file (
    enum DK_FORMAT comp_type,
    const char *file_out,
    unsigned char *input,
    size_t input_size
) {
    unsigned char *output = NULL;
    size_t output_size;
    enum DK_ERROR e;

    if ((e = dk_compress_mem_to_mem(comp_type, &output, &output_size, input, input_size))
    ||  (e = write_output_file(file_out, output, output_size))) {
        free(output);
        return e;
    }
    free(output);
    return 0;
}

SHARED int dk_compress_file_to_file (
    enum DK_FORMAT comp_type,
    const char *file_out,
    const char *file_in
) {
    unsigned char *output = NULL;
    size_t output_size;
    enum DK_ERROR e;

    if ((e = dk_compress_file_to_mem(comp_type, &output, &output_size, file_in))
    ||  (e = write_output_file(file_out, output, output_size))) {
        free(output);
        return e;
    }
    free(output);
    return 0;
}





/* Decompression handlers */

SHARED int dk_decompress_mem_to_mem (
    enum DK_FORMAT decomp_type,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
) {
    enum DK_ERROR e;
    const struct COMP_TYPE *dk_decompress;
    struct COMPRESSOR dc;
    memset(&dc, 0, sizeof(struct COMPRESSOR));

    if ((e = get_compressor(decomp_type, 0, &dk_decompress))
    ||  (e = check_input_mem(input)))
        goto error;

    dc.in.data   = input;
    dc.in.length = input_size;
    dc.out.limit = 1 << dk_decompress->size_limit;

    if ((e = open_output_buffer(&dc.out.data, dc.out.limit))
    ||  (e = dk_decompress->decomp(&dc)))
        goto error;
    *output      = dc.out.data;
    *output_size = dc.out.pos;
    return 0;
error:
    free(dc.out.data); dc.out.data = NULL;
    return e;
}

SHARED int dk_decompress_file_to_mem (
    enum DK_FORMAT decomp_type,
    unsigned char **output,
    size_t *output_size,
    const char *file_in,
    size_t position
) {
    enum DK_ERROR e;
    const struct COMP_TYPE *dk_decompress;
    struct COMPRESSOR dc;
    memset(&dc, 0, sizeof(struct COMPRESSOR));

    if ((e = get_compressor(decomp_type, 0, &dk_decompress))
    ||  (e = open_input_file(file_in, &dc.in.data, &dc.in.length, position, 0)))
        goto error;

    dc.out.limit = 1 << dk_decompress->size_limit;

    if ((e = open_output_buffer(&dc.out.data, dc.out.limit))
    ||  (e = dk_decompress->decomp(&dc)))
        goto error;

    free(dc.in.data); dc.in.data = NULL;
    *output      = dc.out.data;
    *output_size = dc.out.pos;
    return 0;
error:
    free(dc.in.data);  dc. in.data = NULL;
    free(dc.out.data); dc.out.data = NULL;
    return e;
}

SHARED int dk_decompress_mem_to_file (
    enum DK_FORMAT decomp_type,
    const char *file_out,
    unsigned char *input,
    size_t input_size
) {
    unsigned char *output = NULL;
    size_t output_size;
    enum DK_ERROR e;

    if ((e = dk_decompress_mem_to_mem(decomp_type, &output, &output_size, input, input_size))
    ||  (e = write_output_file(file_out, output, output_size))) {
        free(output);
        return e;
    }
    free(output);
    return 0;
}

SHARED int dk_decompress_file_to_file (
    enum DK_FORMAT decomp_type,
    const char *file_out,
    const char *file_in,
    size_t position
) {
    unsigned char *output = NULL;
    size_t output_size;
    enum DK_ERROR e;

    if ((e = dk_decompress_file_to_mem(decomp_type, &output, &output_size, file_in, position))
    ||  (e = write_output_file(file_out, output, output_size))) {
        free(output);
        return e;
    }
    free(output);
    return 0;
}

