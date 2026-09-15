#include "mindustry_schematic.h"
#include "mindustry_deflate.h"
#include "mindustry_typeio.h"

#include <stdlib.h>
#include <string.h>

static McStatus write_schematic_tags(McBuffer *output, const McSaveTags *tags){
    if(tags == NULL || tags->count > UINT8_MAX) return MC_INVALID_ARGUMENT;
    McStatus status = mc_buffer_write_u8(output, (uint8_t)tags->count);
    if(status != MC_OK) return status;
    for(size_t i = 0; i < tags->count; i++){
        status = mc_save_write_utf(output, tags->keys[i]);
        if(status != MC_OK) return status;
        status = mc_save_write_utf(output, tags->values[i]);
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

static McStatus read_schematic_tags(McBuffer *input, McSaveTags *tags){
    uint8_t count = 0;
    if(mc_buffer_read_u8(input, &count) != MC_OK) return MC_FORMAT_ERROR;
    tags->keys = count == 0 ? NULL : calloc(count, sizeof(*tags->keys));
    tags->values = count == 0 ? NULL : calloc(count, sizeof(*tags->values));
    if(count != 0 && (tags->keys == NULL || tags->values == NULL)){
        mc_save_tags_destroy(tags);
        return MC_OUT_OF_MEMORY;
    }
    tags->count = count;
    for(size_t i = 0; i < count; i++){
        McStatus status = mc_save_read_utf(input, &tags->keys[i]);
        if(status != MC_OK){
            mc_save_tags_destroy(tags);
            return status;
        }
        status = mc_save_read_utf(input, &tags->values[i]);
        if(status != MC_OK){
            mc_save_tags_destroy(tags);
            return status;
        }
    }
    return MC_OK;
}

void mc_schematic_destroy(McSchematic *schematic){
    if(schematic == NULL) return;
    mc_save_tags_destroy(&schematic->tags);
    for(size_t i = 0; i < schematic->block_count; i++) free(schematic->blocks[i]);
    free(schematic->blocks);
    for(size_t i = 0; i < schematic->tile_count; i++) free(schematic->tiles[i].config);
    free(schematic->tiles);
    *schematic = (McSchematic){0};
}

McStatus mc_schematic_write(const McSchematic *schematic, McBuffer *output){
    if(schematic == NULL || output == NULL || schematic->width == 0 || schematic->height == 0 ||
       schematic->width > 128 || schematic->height > 128 || schematic->block_count > UINT8_MAX ||
       schematic->tile_count > UINT32_MAX ||
       (schematic->tags.count != 0 && (schematic->tags.keys == NULL || schematic->tags.values == NULL)) ||
       (schematic->block_count != 0 && schematic->blocks == NULL) ||
       (schematic->tile_count != 0 && schematic->tiles == NULL)) return MC_INVALID_ARGUMENT;

    McBuffer plain;
    mc_buffer_init(&plain);
    McStatus status = mc_buffer_write_u16_be(&plain, schematic->width);
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u16_be(&plain, schematic->height);
    if(status != MC_OK) goto cleanup;
    status = write_schematic_tags(&plain, &schematic->tags);
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u8(&plain, (uint8_t)schematic->block_count);
    if(status != MC_OK) goto cleanup;
    for(size_t i = 0; i < schematic->block_count; i++){
        status = mc_save_write_utf(&plain, schematic->blocks[i]);
        if(status != MC_OK) goto cleanup;
    }
    status = mc_buffer_write_u32_be(&plain, (uint32_t)schematic->tile_count);
    if(status != MC_OK) goto cleanup;
    for(size_t i = 0; i < schematic->tile_count; i++){
        const McSchematicTile *tile = &schematic->tiles[i];
        if(tile->block >= schematic->block_count || (tile->config_size != 0 && tile->config == NULL)) {
            status = MC_INVALID_ARGUMENT;
            goto cleanup;
        }
        uint32_t packed = ((uint32_t)(uint16_t)tile->x << 16) | (uint16_t)tile->y;
        status = mc_buffer_write_u8(&plain, tile->block);
        if(status != MC_OK) goto cleanup;
        status = mc_buffer_write_u32_be(&plain, packed);
        if(status != MC_OK) goto cleanup;
        /* TypeIO.writeObject(null): nullType = 0. */
        if(tile->config_size == 0) status = mc_buffer_write_u8(&plain, 0);
        else status = mc_buffer_write_bytes(&plain, tile->config, tile->config_size);
        if(status != MC_OK) goto cleanup;
        status = mc_buffer_write_u8(&plain, tile->rotation);
        if(status != MC_OK) goto cleanup;
    }

    McBuffer compressed;
    mc_buffer_init(&compressed);
    status = mc_zlib_compress_stored(plain.data, plain.size, &compressed);
    if(status != MC_OK) {
        mc_buffer_destroy(&compressed);
        goto cleanup;
    }
    mc_buffer_clear(output);
    status = mc_buffer_write_bytes(output, "msch", 4);
    if(status == MC_OK) status = mc_buffer_write_u8(output, 1);
    if(status == MC_OK) status = mc_buffer_write_bytes(output, compressed.data, compressed.size);
    mc_buffer_destroy(&compressed);

cleanup:
    mc_buffer_destroy(&plain);
    return status;
}

static McStatus read_schematic_payload(const uint8_t *data, size_t size, McSchematic *schematic){
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    if(mc_buffer_read_u16_be(&input, &schematic->width) != MC_OK ||
       mc_buffer_read_u16_be(&input, &schematic->height) != MC_OK ||
       schematic->width == 0 || schematic->height == 0 || schematic->width > 128 || schematic->height > 128){
        return MC_FORMAT_ERROR;
    }
    McStatus status = read_schematic_tags(&input, &schematic->tags);
    if(status != MC_OK) return status;

    uint8_t block_count = 0;
    if(mc_buffer_read_u8(&input, &block_count) != MC_OK) return MC_FORMAT_ERROR;
    schematic->block_count = block_count;
    if(block_count != 0){
        schematic->blocks = calloc(block_count, sizeof(*schematic->blocks));
        if(schematic->blocks == NULL) return MC_OUT_OF_MEMORY;
        for(size_t i = 0; i < block_count; i++){
            status = mc_save_read_utf(&input, &schematic->blocks[i]);
            if(status != MC_OK) return status;
        }
    }

    uint32_t tile_count = 0;
    if(mc_buffer_read_u32_be(&input, &tile_count) != MC_OK || tile_count > 128u * 128u){
        return MC_FORMAT_ERROR;
    }
    schematic->tile_count = tile_count;
    if(tile_count != 0){
        schematic->tiles = calloc(tile_count, sizeof(*schematic->tiles));
        if(schematic->tiles == NULL) return MC_OUT_OF_MEMORY;
    }
    for(size_t i = 0; i < tile_count; i++){
        uint8_t block = 0, rotation = 0;
        uint32_t packed = 0;
        if(mc_buffer_read_u8(&input, &block) != MC_OK || block >= block_count ||
           mc_buffer_read_u32_be(&input, &packed) != MC_OK){
            return MC_FORMAT_ERROR;
        }
        size_t config_start = input.position;
        size_t config_size = 0;
        if(mc_typeio_skip(&input, &config_size) != MC_OK ||
           mc_buffer_read_u8(&input, &rotation) != MC_OK){
            return MC_FORMAT_ERROR;
        }
        uint8_t *config = malloc(config_size);
        if(config_size != 0 && config == NULL) return MC_OUT_OF_MEMORY;
        if(config_size != 0) memcpy(config, input.data + config_start, config_size);
        schematic->tiles[i] = (McSchematicTile){
            .block = block,
            .x = (int16_t)(packed >> 16),
            .y = (int16_t)packed,
            .rotation = rotation,
            .config = config,
            .config_size = config_size
        };
    }
    return input.position == input.size ? MC_OK : MC_FORMAT_ERROR;
}

McStatus mc_schematic_read(const uint8_t *data, size_t size, McSchematic *schematic){
    if(data == NULL || schematic == NULL || size < 6) return MC_INVALID_ARGUMENT;
    if(memcmp(data, "msch", 4) != 0 || data[4] != 1) return MC_FORMAT_ERROR;

    McBuffer plain;
    mc_buffer_init(&plain);
    McStatus status = mc_zlib_decompress(data + 5, size - 5, &plain);
    if(status != MC_OK) goto cleanup;

    McSchematic decoded = {0};
    status = read_schematic_payload(plain.data, plain.size, &decoded);
    if(status != MC_OK){
        mc_schematic_destroy(&decoded);
        goto cleanup;
    }
    mc_schematic_destroy(schematic);
    *schematic = decoded;

cleanup:
    mc_buffer_destroy(&plain);
    return status;
}
