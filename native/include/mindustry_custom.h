#ifndef MINDUSTRY_NATIVE_CUSTOM_H
#define MINDUSTRY_NATIVE_CUSTOM_H

#include "mindustry_save.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McCustomChunk{
    char *name;
    uint8_t *data;
    size_t size;
} McCustomChunk;

typedef struct McCustomChunks{
    McCustomChunk *chunks;
    size_t count;
} McCustomChunks;

MC_API McStatus mc_custom_chunks_read(const uint8_t *data, size_t size, McCustomChunks *chunks);
MC_API McStatus mc_custom_chunks_write(const McCustomChunks *chunks, McBuffer *output);
MC_API void mc_custom_chunks_destroy(McCustomChunks *chunks);

#endif
