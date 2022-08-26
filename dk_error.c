/* SPDX-License-Identifier: MIT
 * Copyright (c) 2022 Kingizor
 * dkcomp library - error messages! */

#include "dk_internal.h"

static const char *dk_error_messages[] = {
    [DK_SUCCESS]            = "No error",

    [DK_ERROR_OOB_INPUT]    = "Tried to read out of bounds when reading input",
    [DK_ERROR_OOB_OUTPUT_R] = "Tried to read out of bounds when reading output",
    [DK_ERROR_OOB_OUTPUT_W] = "Tried to write out of bounds when writing output",

    [DK_ERROR_ALLOC]        = "Failed to allocate memory for something",

    [DK_ERROR_NULL_INPUT]   = "The pointer provided for input is NULL",
    [DK_ERROR_FILE_INPUT]   = "Failed to open input file",
    [DK_ERROR_FILE_OUTPUT]  = "Failed to open output file",
    [DK_ERROR_SEEK_INPUT]   = "Failed to seek the input file",
    [DK_ERROR_FREAD]        = "Failed to read from the input file",
    [DK_ERROR_FWRITE]       = "Failed to write to the output file",

    [DK_ERROR_OFFSET_BIG]   = "The supplied offset is too large",
    [DK_ERROR_OFFSET_NEG]   = "The supplied offset is negative",
    [DK_ERROR_OFFSET_DIFF]  = "The difference between the file size and the supplied offset is too small",

    [DK_ERROR_INPUT_SMALL]  = "Input data is too small",
    [DK_ERROR_INPUT_LARGE]  = "Input data is too large",
    [DK_ERROR_OUTPUT_SMALL] = "Output data is too small",

    [DK_ERROR_SIZE_WRONG]   = "Decompressed size doesn't match the predicted size",
    [DK_ERROR_EARLY_EOF]    = "Unexpected end of input",

    [DK_ERROR_GBA_DETECT]   = "Failed to detect compression type",
    [DK_ERROR_SIG_WRONG]    = "Data has an incorrect signature byte",

    [DK_ERROR_COMP_NOT]     = "Unsupported compression type",
    [DK_ERROR_DECOMP_NOT]   = "Unsupposted decompression type",

    [DK_ERROR_SD_BAD_EXIT]  = "Encountered an exit condition in a case without an exit check",
    [DK_ERROR_LZ77_HIST]    = "Encountered an invalid history offset",
    [DK_ERROR_HUFF_WRONG]   = "The input reports an invalid leaf size",
    [DK_ERROR_HUFF_LEAF]    = "The data requires an unsupported leaf size, we currently only support a size of 8",
    [DK_ERROR_HUFF_DIST]    = "A node exceeded the allowed distance for a node",
    [DK_ERROR_HUFF_NO_LEAF] = "Couldn't produce any leaf nodes",
    [DK_ERROR_HUFF_OUTSIZE] = "Output isn't large enough for the tree table",
    [DK_ERROR_HUFF_STACKS]  = "Tried to create too many stacks when generating GBA tree",
    [DK_ERROR_HUFF_NODES]   = "Tried to place too many nodes in a single stack when generating GBA tree",
    [DK_ERROR_HUFF_NODELIM] = "Data seems to contain too many leaf nodes",
    [DK_ERROR_HUFF_LEAFVAL] = "Tried to add a leaf that already exists",

    [DK_ERROR_TABLE_RANGE]  = "The frequency table contains an invalid range (a > b)",
    [DK_ERROR_TABLE_VALUE]  = "The frequency table contains too many values",
    [DK_ERROR_TABLE_ZERO]   = "All bytes occur zero times",

    [DK_ERROR_VERIFY_DEC]   = "Failed to decompress the newly compressed data",
    [DK_ERROR_VERIFY_SIZE]  = "The size of the decompressed data doesn't match the original",
    [DK_ERROR_VERIFY_DATA]  = "The decompressed data doesn't match the original data",

    [DK_ERROR_INVALID]      = "An invalid error code was passed to this function"
};

SHARED const char *dk_get_error (int error_code) {
    if (error_code < 0 || error_code >= DK_ERROR_LIMIT)
        error_code = DK_ERROR_INVALID;
    return dk_error_messages[error_code];
}

