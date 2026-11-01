// lz4.h - LZ4 block format (https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md).
#pragma once

#include "common.h"

// Worst case output size for a given input size.
size_t lz4_bound(size_t input);

// Returns bytes written, or 0 when the buffer is too small / input is empty.
size_t lz4_compress_block(const u8 *src, size_t srcLen, u8 *dst, size_t dstCap);

// Returns bytes written into dst, or 0 on malformed input.
size_t lz4_decompress_block(const u8 *src, size_t srcLen, u8 *dst, size_t dstCap);
