#ifndef MINDUSTRY_NATIVE_PATCHES_H
#define MINDUSTRY_NATIVE_PATCHES_H

#include "mindustry_save.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef enum McPatchAssetKind{
    MC_PATCH_TEXT = 0,
    MC_PATCH_IMAGE = 1
} McPatchAssetKind;

typedef struct McPatchAsset{
    McPatchAssetKind kind;
    /* Image assets have a Java UTF name; text assets have no serialized name. */
    char *name;
    uint16_t width;
    uint16_t height;
    uint8_t *data;
    size_t size;
} McPatchAsset;

typedef struct McDataPatches{
    /* Save 11 uses the legacy byte-count text-only representation. */
    bool legacy;
    uint32_t format_version;
    McPatchAsset *assets;
    size_t count;
} McDataPatches;

MC_API McStatus mc_data_patches_read(uint32_t save_version, const uint8_t *data, size_t size, McDataPatches *patches);
MC_API McStatus mc_data_patches_write(uint32_t save_version, const McDataPatches *patches, McBuffer *output);
MC_API void mc_data_patches_destroy(McDataPatches *patches);

#endif
