/* SPDX-License-Identifier: MIT
 * Big Data Compression Library
 * Copyright (c) 2020-2022 Kingizor */

#ifndef DK_COMP
#define DK_COMP

#include <stddef.h>

/* All compression and decompression functions listed here return 0 if they
   complete successfully, or 1 if an error is encountered.
   If an error is encountered, calling dk_get_error(); will return a
   character string explaining the error. (not thread-safe)

   For compression and decompression to memory, the functions will allocate
   data necessary for the provided output pointer, which must be freed
   manually by the user.
*/

enum DK_FORMAT {
    BD_DECOMP,
    BD_COMP,
    SD_DECOMP,
    SD_COMP,
DKCCHR_DECOMP,
DKCGBC_DECOMP,
   DKL_DECOMP
};


/* Error reporting */
const char *dk_get_error (void);


/* Compression functions */
int dk_compress_mem_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
);
int dk_compress_mem_to_file (
    enum DK_FORMAT,
    const char *file_out,
    unsigned char *input,
    size_t input_size
);
int dk_compress_file_to_file (
    enum DK_FORMAT,
    const char *file_out,
    const char *file_in
);
int dk_compress_file_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    const char *file_in
);


/* Decompression functions */
int dk_decompress_mem_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
);
int dk_decompress_mem_to_file (
    enum DK_FORMAT,
    const char *file_out,
    unsigned char *input,
    size_t input_size
);
int dk_decompress_file_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    const char *file_in,
    size_t file_position
);
int dk_decompress_file_to_file (
    enum DK_FORMAT,
    const char *file_out,
    const char *file_in,
    size_t position
);

#endif
