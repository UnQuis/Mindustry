#include "mindustry_map.h"

#include <stdlib.h>
#include <string.h>

static size_t tile_count(const McMapSection *section){
    return (size_t)section->width * section->height;
}

void mc_map_section_destroy(McMapSection *section){
    if(section == NULL) return;
    for(size_t i = 0; i < tile_count(section); i++) free(section->tiles[i].entity_data);
    free(section->tiles);
    *section = (McMapSection){0};
}

McStatus mc_map_section_init(McMapSection *section, uint16_t width, uint16_t height){
    if(section == NULL || width == 0 || height == 0) return MC_INVALID_ARGUMENT;
    *section = (McMapSection){.width = width, .height = height};
    section->tiles = calloc(tile_count(section), sizeof(*section->tiles));
    if(section->tiles == NULL){
        *section = (McMapSection){0};
        return MC_OUT_OF_MEMORY;
    }
    for(size_t i = 0; i < tile_count(section); i++) section->tiles[i].tile.floor = MC_FLOOR_STONE;
    return MC_OK;
}

static McStatus write_floor_runs(const McMapSection *section, McBuffer *output){
    size_t total = tile_count(section);
    for(size_t i = 0; i < total;){
        uint16_t count = 1;
        while(count < 256 && i + count < total &&
              section->tiles[i + count].tile.floor == section->tiles[i].tile.floor &&
              section->tiles[i + count].tile.overlay == section->tiles[i].tile.overlay) count++;
        McStatus status = mc_buffer_write_u16_be(output, section->tiles[i].tile.floor);
        if(status != MC_OK) return status;
        status = mc_buffer_write_u16_be(output, section->tiles[i].tile.overlay);
        if(status != MC_OK) return status;
        status = mc_buffer_write_u8(output, (uint8_t)(count - 1));
        if(status != MC_OK) return status;
        i += count;
    }
    return MC_OK;
}

static McStatus write_block_runs(const McMapSection *section, McBuffer *output){
    size_t total = tile_count(section);
    for(size_t i = 0; i < total;){
        const McMapTileRecord *record = &section->tiles[i];
        if((record->flags & (uint8_t)~5u) != 0){
            return MC_INVALID_ARGUMENT;
        }
        McStatus status = mc_buffer_write_u16_be(output, record->tile.block);
        if(status != MC_OK) return status;
        status = mc_buffer_write_u8(output, record->flags);
        if(status != MC_OK) return status;
        if((record->flags & 4u) != 0){
            status = mc_buffer_write_u8(output, record->data);
            if(status != MC_OK) return status;
            status = mc_buffer_write_u8(output, record->floor_data);
            if(status != MC_OK) return status;
            status = mc_buffer_write_u8(output, record->overlay_data);
            if(status != MC_OK) return status;
            status = mc_buffer_write_u32_be(output, record->extra_data);
            if(status != MC_OK) return status;
        }
        if((record->flags & 1u) != 0){
            status = mc_buffer_write_u8(output, record->entity_center ? 1 : 0);
            if(status != MC_OK) return status;
            if(record->entity_center){
                if(record->entity_size > UINT32_MAX || (record->entity_data == NULL && record->entity_size != 0)) return MC_INVALID_ARGUMENT;
                status = mc_buffer_write_u32_be(output, (uint32_t)record->entity_size);
                if(status != MC_OK) return status;
                status = mc_buffer_write_bytes(output, record->entity_data, record->entity_size);
                if(status != MC_OK) return status;
            }
        }else if((record->flags & 4u) == 0){
            uint16_t count = 1;
            while(count < 256 && i + count < total &&
                  section->tiles[i + count].tile.block == record->tile.block &&
                  section->tiles[i + count].flags == 0) count++;
            status = mc_buffer_write_u8(output, (uint8_t)(count - 1));
            if(status != MC_OK) return status;
            i += count;
            continue;
        }
        i++;
    }
    return MC_OK;
}

McStatus mc_map_section_write(const McMapSection *section, McBuffer *output){
    if(section == NULL || output == NULL || section->tiles == NULL || section->width == 0 || section->height == 0){
        return MC_INVALID_ARGUMENT;
    }
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_u16_be(output, section->width);
    if(status != MC_OK) return status;
    status = mc_buffer_write_u16_be(output, section->height);
    if(status != MC_OK) return status;
    status = write_floor_runs(section, output);
    if(status != MC_OK) return status;
    return write_block_runs(section, output);
}

static McStatus read_floor_runs(McBuffer *input, McMapSection *section){
    size_t total = tile_count(section), position = 0;
    while(position < total){
        uint16_t floor = 0, overlay = 0;
        uint8_t encoded_count = 0;
        if(mc_buffer_read_u16_be(input, &floor) != MC_OK || mc_buffer_read_u16_be(input, &overlay) != MC_OK ||
           mc_buffer_read_u8(input, &encoded_count) != MC_OK) return MC_FORMAT_ERROR;
        size_t count = (size_t)encoded_count + 1;
        if(count > total - position) return MC_FORMAT_ERROR;
        for(size_t i = 0; i < count; i++){
            section->tiles[position + i].tile.floor = floor;
            section->tiles[position + i].tile.overlay = overlay;
        }
        position += count;
    }
    return MC_OK;
}

static McStatus read_block_records(McBuffer *input, McMapSection *section){
    size_t total = tile_count(section), position = 0;
    while(position < total){
        McMapTileRecord *record = &section->tiles[position];
        uint8_t flags = 0;
        if(mc_buffer_read_u16_be(input, &record->tile.block) != MC_OK || mc_buffer_read_u8(input, &flags) != MC_OK ||
           (flags & (uint8_t)~5u) != 0) return MC_FORMAT_ERROR;
        record->flags = flags;
        if((flags & 4u) != 0){
            if(mc_buffer_read_u8(input, &record->data) != MC_OK ||
               mc_buffer_read_u8(input, &record->floor_data) != MC_OK ||
               mc_buffer_read_u8(input, &record->overlay_data) != MC_OK ||
               mc_buffer_read_u32_be(input, &record->extra_data) != MC_OK) return MC_FORMAT_ERROR;
        }
        if((flags & 1u) != 0){
            uint8_t center = 0;
            if(mc_buffer_read_u8(input, &center) != MC_OK) return MC_FORMAT_ERROR;
            record->entity_center = center != 0;
            if(record->entity_center){
                uint32_t entity_size = 0;
                if(mc_buffer_read_u32_be(input, &entity_size) != MC_OK || entity_size > input->size - input->position){
                    return MC_FORMAT_ERROR;
                }
                record->entity_size = entity_size;
                if(entity_size != 0){
                    record->entity_data = malloc(entity_size);
                    if(record->entity_data == NULL) return MC_OUT_OF_MEMORY;
                    if(mc_buffer_read_bytes(input, record->entity_data, entity_size) != MC_OK) return MC_FORMAT_ERROR;
                }
            }
            position++;
        }else if((flags & 4u) != 0){
            position++;
        }else{
            uint8_t encoded_count = 0;
            if(mc_buffer_read_u8(input, &encoded_count) != MC_OK) return MC_FORMAT_ERROR;
            size_t count = (size_t)encoded_count + 1;
            if(count > total - position) return MC_FORMAT_ERROR;
            for(size_t i = 1; i < count; i++){
                section->tiles[position + i].tile.block = record->tile.block;
                section->tiles[position + i].flags = 0;
            }
            position += count;
        }
    }
    return MC_OK;
}

McStatus mc_map_section_read(const uint8_t *data, size_t size, McMapSection *section){
    if(data == NULL || section == NULL || size == 0) return MC_INVALID_ARGUMENT;
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint16_t width = 0, height = 0;
    if(mc_buffer_read_u16_be(&input, &width) != MC_OK || mc_buffer_read_u16_be(&input, &height) != MC_OK){
        return MC_FORMAT_ERROR;
    }
    McMapSection decoded = {0};
    McStatus status = mc_map_section_init(&decoded, width, height);
    if(status != MC_OK) return status;
    status = read_floor_runs(&input, &decoded);
    if(status == MC_OK) status = read_block_records(&input, &decoded);
    if(status != MC_OK || input.position != input.size){
        mc_map_section_destroy(&decoded);
        return status == MC_OK ? MC_FORMAT_ERROR : status;
    }
    mc_map_section_destroy(section);
    *section = decoded;
    return MC_OK;
}
