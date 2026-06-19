/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Kingizor
 * dkcomp library - dkcomp API for WebAssembly */

#include "dkcomp.h"
#include "dk_internal.h"

static unsigned char *input_data, *output_data;
static size_t input_pos, input_size, output_size;

static int exec (enum DK_FORMAT format, typeof(int (enum DK_FORMAT, unsigned char**, size_t*, unsigned char*, size_t))*func) {
    if (input_size < input_pos)
        return DK_ERROR_OFFSET_BIG;

    return func(
        format,
        &output_data,
        &output_size,
        input_data + input_pos,
        input_size - input_pos
    );
}

__attribute__((export_name("decompress")))
int decompress (enum DK_FORMAT format) { return exec(format, dk_decompress_mem_to_mem); }

__attribute__((export_name("compress")))
int   compress (enum DK_FORMAT format) { return exec(format,   dk_compress_mem_to_mem); }

__attribute__((export_name("check_size")))
int check_size (enum DK_FORMAT format) {
    if (input_size < input_pos)
        return DK_ERROR_OFFSET_BIG;

    return dk_compressed_size_mem(
        format,
        input_data + input_pos,
        input_size - input_pos,
        &output_size
    );
}

__attribute__((export_name("load_input_data")))
unsigned char *load_input_data (size_t size) {
    return input_data = malloc(input_size = size);
}

__attribute__((export_name("free_input_data")))
void free_input_data (void) { free(input_data); input_data = NULL; }

__attribute__((export_name("get_input_size")))
size_t get_input_size (void) { return input_size; }

__attribute__((export_name("set_input_size")))
void set_input_size (size_t size) { input_size = size; }

__attribute__((export_name("get_input_pos")))
size_t get_input_pos (void) { return input_pos; }

__attribute__((export_name("set_input_pos")))
void set_input_pos (size_t pos) { input_pos = pos; }


__attribute__((export_name("get_output_data")))
unsigned char *get_output_data (void) { return output_data; }

__attribute__((export_name("free_output_data")))
void free_output_data (void) { free(output_data); output_data = NULL; }

__attribute__((export_name("get_output_size")))
size_t get_output_size (void) { return output_size; }

