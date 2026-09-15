#ifndef MINDUSTRY_NATIVE_SCHEMATIC_H
#define MINDUSTRY_NATIVE_SCHEMATIC_H

#include "mindustry_save.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McSchematicTile{
    uint8_t block;
    int16_t x;
    int16_t y;
    uint8_t rotation;
    /* Raw TypeIO.writeObject bytes. Size zero means a null config. */
    uint8_t *config;
    size_t config_size;
} McSchematicTile;

typedef struct McSchematic{
    uint16_t width;
    uint16_t height;
    McSaveTags tags;
    char **blocks;
    size_t block_count;
    McSchematicTile *tiles;
    size_t tile_count;
} McSchematic;

MC_API McStatus mc_schematic_write(const McSchematic *schematic, McBuffer *output);
MC_API McStatus mc_schematic_read(const uint8_t *data, size_t size, McSchematic *schematic);
MC_API void mc_schematic_destroy(McSchematic *schematic);

#endif
