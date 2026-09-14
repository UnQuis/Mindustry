#ifndef MINDUSTRY_NATIVE_DEFLATE_H
#define MINDUSTRY_NATIVE_DEFLATE_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/*
 * The Java save stream is a zlib-wrapped DEFLATE stream. The writer uses
 * standards-compliant stored DEFLATE blocks so it has no third-party runtime
 * dependency. The reader accepts stored, fixed-Huffman and dynamic-Huffman
 * blocks, including streams produced by Java's DeflaterOutputStream.
 */
MC_API McStatus mc_zlib_compress_stored(const uint8_t *data, size_t size, McBuffer *output);
MC_API McStatus mc_zlib_decompress(const uint8_t *data, size_t size, McBuffer *output);

#endif
