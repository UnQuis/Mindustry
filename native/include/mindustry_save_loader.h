#ifndef MINDUSTRY_NATIVE_SAVE_LOADER_H
#define MINDUSTRY_NATIVE_SAVE_LOADER_H

#include "mindustry_save.h"
#include "mindustry_map.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McSaveBlob{
    uint8_t *data;
    size_t size;
} McSaveBlob;

typedef struct McSaveFile{
    uint32_t version;
    McSaveTags meta;
    McSaveBlob patches;
    McContentHeader content;
    McMapSection map;
    McSaveBlob entities;
    McSaveBlob markers;
    McSaveBlob custom;
} McSaveFile;

/* Loads the current MSAV region sequence and preserves unsupported regions as
   opaque bytes. Map tiles and map entity chunks are decoded losslessly. */
MC_API McStatus mc_save_file_load(const uint8_t *compressed, size_t size, McSaveFile *save);
MC_API McStatus mc_save_file_write(const McSaveFile *save, McBuffer *compressed);
MC_API void mc_save_file_destroy(McSaveFile *save);

#endif
