/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2022 Kingizor
 * Big Data Compression Library */

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
        BD_COMP,
        SD_COMP,
    DKCCHR_COMP,
    DKCGBC_COMP,
       DKL_COMP,
GBA_HUFF60_COMP,
GBA_HUFF50_COMP,
GBA_HUFF20_COMP,
  GBA_LZ77_COMP,
   GBA_RLE_COMP,
       GBA_COMP, /* auto-detect GBA */
      COMP_LIMIT
};


/* Error reporting */
const char *dk_get_error (int);


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
