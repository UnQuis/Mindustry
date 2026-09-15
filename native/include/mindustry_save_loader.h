#ifndef MINDUSTRY_NATIVE_SAVE_LOADER_H
#define MINDUSTRY_NATIVE_SAVE_LOADER_H

#include "mindustry_save.h"
#include "mindustry_map.h"
#include "mindustry_patches.h"
#include "mindustry_entities.h"
#include "mindustry_markers.h"
#include "mindustry_custom.h"

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
    McDataPatches patch_data;
    McContentHeader content;
    McMapSection map;
    McSaveBlob entities;
    McEntitiesRegion entity_data;
    McSaveBlob markers;
    McMarkers marker_data;
    McSaveBlob custom;
    McCustomChunks custom_data;
} McSaveFile;

/* Loads the current MSAV region sequence and preserves unsupported regions as
   opaque bytes. Map tiles and map entity chunks are decoded losslessly. */
MC_API McStatus mc_save_file_load(const uint8_t *compressed, size_t size, McSaveFile *save);
MC_API McStatus mc_save_file_write(const McSaveFile *save, McBuffer *compressed);
MC_API McStatus mc_save_file_copy_world(const McSaveFile *save, McWorld *world);
MC_API void mc_save_file_destroy(McSaveFile *save);

#endif
