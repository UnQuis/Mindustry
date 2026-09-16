#ifndef MINDUSTRY_NATIVE_FORMAT_H
#define MINDUSTRY_NATIVE_FORMAT_H

#include "mindustry.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McBuffer{
    uint8_t *data;
    size_t size;
    size_t capacity;
    size_t position;
} McBuffer;

MC_API void mc_buffer_init(McBuffer *buffer);
MC_API void mc_buffer_destroy(McBuffer *buffer);
MC_API void mc_buffer_clear(McBuffer *buffer);
MC_API McStatus mc_buffer_write_u8(McBuffer *buffer, uint8_t value);
MC_API McStatus mc_buffer_write_u16_be(McBuffer *buffer, uint16_t value);
MC_API McStatus mc_buffer_write_u32_be(McBuffer *buffer, uint32_t value);
MC_API McStatus mc_buffer_write_bytes(McBuffer *buffer, const void *data, size_t size);
MC_API McStatus mc_buffer_read_u8(McBuffer *buffer, uint8_t *value);
MC_API McStatus mc_buffer_read_u16_be(McBuffer *buffer, uint16_t *value);
MC_API McStatus mc_buffer_read_u32_be(McBuffer *buffer, uint32_t *value);
MC_API McStatus mc_buffer_read_bytes(McBuffer *buffer, void *destination, size_t size);

/*
 * Encodes/decodes the uncompressed map section written by SaveVersion.writeMap
 * for tiles without custom data or building entities. The integer encoding and
 * RLE counters match Java DataOutput and the current Mindustry map format:
 * width, height, floor/overlay runs, then block runs.
 *
 * This is intentionally a section codec, not a complete .msav reader. The
 * outer MSAV header, deflate stream, content header and building/entity chunks
 * are separate compatibility gates.
 */
MC_API McStatus mc_map_write_section(const McWorld *world, McBuffer *output);
MC_API McStatus mc_map_read_section(McWorld *world, const uint8_t *data, size_t size);

/* The registry uses McBuffer for its deterministic manifest codec. */
#include "mindustry_content_registry.h"

#endif
