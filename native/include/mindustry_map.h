#ifndef MINDUSTRY_NATIVE_MAP_H
#define MINDUSTRY_NATIVE_MAP_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McMapTileRecord{
    McTile tile;
    /* Java SaveVersion.writeMap packed flags: bit 0 entity, bit 2 tile data. */
    uint8_t flags;
    uint8_t data;
    uint8_t floor_data;
    uint8_t overlay_data;
    uint32_t extra_data;
    bool entity_center;
    uint8_t *entity_data;
    size_t entity_size;
} McMapTileRecord;

typedef struct McMapSection{
    uint16_t width;
    uint16_t height;
    McMapTileRecord *tiles;
} McMapSection;

MC_API McStatus mc_map_section_init(McMapSection *section, uint16_t width, uint16_t height);
MC_API void mc_map_section_destroy(McMapSection *section);
MC_API McStatus mc_map_section_write(const McMapSection *section, McBuffer *output);
MC_API McStatus mc_map_section_read(const uint8_t *data, size_t size, McMapSection *section);

#endif
