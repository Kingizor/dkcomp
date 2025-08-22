/* SPDX-License-Identifier: MIT
 * Copyright (c) 2020-2025 Kingizor
 * Big Data Compression Library */

#ifndef DK_COMP
#define DK_COMP

#include <stddef.h>

/* All compression and decompression functions listed here return 0 if they
   complete successfully, or 1 if an error is encountered.
   If an error is encountered, calling dk_get_error(error_code); will
   return a pointer to a character string explaining the error.

   For compression and decompression to memory, the functions will allocate
   data necessary for the provided output pointer, which must be freed
   manually by the user.
*/

#if defined(__GNUC__) || defined(__clang__)
  #define SHARED __attribute__ ((visibility ("default")))
#elif defined(_MSC_VER)
  #ifdef BUILD_DKCOMP
    #define SHARED __declspec(dllexport)
  #else
    #define SHARED __declspec(dllimport)
  #endif
#else
  #define SHARED
#endif

enum DK_FORMAT {
        BD_COMP,
        SD_COMP,
    DKCCHR_COMP,
    DKCGBC_COMP,
       DKL_COMP,
  GBA_LZ77_COMP,
GBA_HUFF20_COMP,
   GBA_RLE_COMP,
GBA_HUFF50_COMP,
GBA_HUFF60_COMP,
       GBA_COMP, /* auto-detect GBA */
GB_PRINTER_COMP,
      COMP_LIMIT
};


/* Error reporting */
SHARED const char *dk_get_error (int);


/* Compression functions */
SHARED int dk_compress_mem_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
);
SHARED int dk_compress_mem_to_file (
    enum DK_FORMAT,
    const char *file_out,
    unsigned char *input,
    size_t input_size
);
SHARED int dk_compress_file_to_file (
    enum DK_FORMAT,
    const char *file_out,
    const char *file_in
);
SHARED int dk_compress_file_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    const char *file_in
);


/* Decompression functions */
SHARED int dk_decompress_mem_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    unsigned char *input,
    size_t input_size
);
SHARED int dk_decompress_mem_to_file (
    enum DK_FORMAT,
    const char *file_out,
    unsigned char *input,
    size_t input_size
);
SHARED int dk_decompress_file_to_mem (
    enum DK_FORMAT,
    unsigned char **output,
    size_t *output_size,
    const char *file_in,
    size_t file_position
);
SHARED int dk_decompress_file_to_file (
    enum DK_FORMAT,
    const char *file_out,
    const char *file_in,
    size_t position
);


/* size functions */
/* these report the size of the compressed data */
SHARED int dk_compressed_size_mem (
    enum DK_FORMAT comp_type,
    unsigned char *input,
    size_t input_size,
    size_t *compressed_size
);
SHARED int dk_compressed_size_file (
    enum DK_FORMAT decomp_type,
    const char *file_in,
    size_t position,
    size_t *compressed_size
);




/* DKL Huffman functions */
int dkl_huffman_decode (
    unsigned char   *input, size_t    input_size,
    unsigned char **output, size_t  *output_size,
    unsigned char    *tree, size_t    tile_count 
);
int dkl_huffman_encode (
    unsigned char  *input,  size_t   input_size,
    unsigned char **output, size_t *output_size,
    unsigned char  *tree
);
int dkl_huffman_tree (
    unsigned char *input, size_t input_size,
    unsigned char **tree
);

#endif
