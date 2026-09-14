#include "mindustry_format.h"

#include <stdlib.h>
#include <string.h>

static McStatus mc_buffer_reserve(McBuffer *buffer, size_t additional){
    if(buffer == NULL || additional > SIZE_MAX - buffer->size) return MC_CAPACITY_EXCEEDED;
    size_t needed = buffer->size + additional;
    if(needed <= buffer->capacity) return MC_OK;

    size_t capacity = buffer->capacity == 0 ? 64 : buffer->capacity;
    while(capacity < needed){
        if(capacity > SIZE_MAX / 2) {
            capacity = needed;
            break;
        }
        capacity *= 2;
    }
    uint8_t *data = realloc(buffer->data, capacity);
    if(data == NULL) return MC_OUT_OF_MEMORY;
    buffer->data = data;
    buffer->capacity = capacity;
    return MC_OK;
}

void mc_buffer_init(McBuffer *buffer){
    if(buffer == NULL) return;
    *buffer = (McBuffer){0};
}

void mc_buffer_destroy(McBuffer *buffer){
    if(buffer == NULL) return;
    free(buffer->data);
    *buffer = (McBuffer){0};
}

void mc_buffer_clear(McBuffer *buffer){
    if(buffer == NULL) return;
    buffer->size = 0;
    buffer->position = 0;
}

McStatus mc_buffer_write_u8(McBuffer *buffer, uint8_t value){
    McStatus status = mc_buffer_reserve(buffer, 1);
    if(status != MC_OK) return status;
    buffer->data[buffer->size++] = value;
    return MC_OK;
}

McStatus mc_buffer_write_u16_be(McBuffer *buffer, uint16_t value){
    McStatus status = mc_buffer_reserve(buffer, 2);
    if(status != MC_OK) return status;
    buffer->data[buffer->size++] = (uint8_t)(value >> 8);
    buffer->data[buffer->size++] = (uint8_t)(value & 0xffu);
    return MC_OK;
}

McStatus mc_buffer_read_u8(McBuffer *buffer, uint8_t *value){
    if(buffer == NULL || value == NULL || buffer->position >= buffer->size) return MC_FORMAT_ERROR;
    *value = buffer->data[buffer->position++];
    return MC_OK;
}

McStatus mc_buffer_read_u16_be(McBuffer *buffer, uint16_t *value){
    if(buffer == NULL || value == NULL || buffer->position > buffer->size || buffer->size - buffer->position < 2){
        return MC_FORMAT_ERROR;
    }
    uint16_t high = buffer->data[buffer->position++];
    uint16_t low = buffer->data[buffer->position++];
    *value = (uint16_t)((high << 8) | low);
    return MC_OK;
}

McStatus mc_buffer_read_bytes(McBuffer *buffer, void *destination, size_t size){
    if(buffer == NULL || destination == NULL || buffer->position > buffer->size ||
       size > buffer->size - buffer->position) return MC_FORMAT_ERROR;
    memcpy(destination, buffer->data + buffer->position, size);
    buffer->position += size;
    return MC_OK;
}

static McStatus write_tile_run(McBuffer *output, const McTile *tile, uint16_t count){
    McStatus status = mc_buffer_write_u16_be(output, tile->floor);
    if(status != MC_OK) return status;
    status = mc_buffer_write_u16_be(output, tile->overlay);
    if(status != MC_OK) return status;
    /* Java writes the number of following tiles, not the run length. */
    return mc_buffer_write_u8(output, (uint8_t)(count - 1));
}

static McStatus write_block_run(McBuffer *output, const McTile *tile, uint16_t count){
    McStatus status = mc_buffer_write_u16_be(output, tile->block);
    if(status != MC_OK) return status;
    /* packed flags: no building entity and no custom tile data */
    status = mc_buffer_write_u8(output, 0);
    if(status != MC_OK) return status;
    return mc_buffer_write_u8(output, (uint8_t)(count - 1));
}

McStatus mc_map_write_section(const McWorld *world, McBuffer *output){
    if(world == NULL || output == NULL || world->tiles == NULL || world->width == 0 || world->height == 0){
        return MC_INVALID_ARGUMENT;
    }
    const size_t tile_count = (size_t)world->width * world->height;
    if(tile_count > SIZE_MAX / sizeof(McTile)) return MC_CAPACITY_EXCEEDED;

    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_u16_be(output, world->width);
    if(status != MC_OK) return status;
    status = mc_buffer_write_u16_be(output, world->height);
    if(status != MC_OK) return status;

    for(size_t i = 0; i < tile_count;){
        uint16_t count = 1;
        while(count < 256 && i + count < tile_count &&
              world->tiles[i + count].floor == world->tiles[i].floor &&
              world->tiles[i + count].overlay == world->tiles[i].overlay){
            count++;
        }
        status = write_tile_run(output, &world->tiles[i], count);
        if(status != MC_OK) return status;
        i += count;
    }

    for(size_t i = 0; i < tile_count;){
        uint16_t count = 1;
        while(count < 256 && i + count < tile_count &&
              world->tiles[i + count].block == world->tiles[i].block){
            count++;
        }
        status = write_block_run(output, &world->tiles[i], count);
        if(status != MC_OK) return status;
        i += count;
    }
    return MC_OK;
}

static McStatus read_run(McBuffer *input, uint16_t *first, uint16_t *second, uint8_t *count){
    McStatus status = mc_buffer_read_u16_be(input, first);
    if(status != MC_OK) return status;
    status = mc_buffer_read_u16_be(input, second);
    if(status != MC_OK) return status;
    return mc_buffer_read_u8(input, count);
}

McStatus mc_map_read_section(McWorld *world, const uint8_t *data, size_t size){
    if(world == NULL || data == NULL || size == 0) return MC_INVALID_ARGUMENT;

    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint16_t width = 0, height = 0;
    McStatus status = mc_buffer_read_u16_be(&input, &width);
    if(status != MC_OK) return status;
    status = mc_buffer_read_u16_be(&input, &height);
    if(status != MC_OK || width == 0 || height == 0) return MC_FORMAT_ERROR;

    McWorld decoded = {0};
    status = mc_world_init(&decoded, width, height);
    if(status != MC_OK) return status;
    const size_t tile_count = (size_t)width * height;

    size_t position = 0;
    while(position < tile_count){
        uint16_t floor = 0, overlay = 0;
        uint8_t encoded_count = 0;
        status = read_run(&input, &floor, &overlay, &encoded_count);
        size_t count = (size_t)encoded_count + 1;
        if(status != MC_OK || count > tile_count - position){
            mc_world_destroy(&decoded);
            return MC_FORMAT_ERROR;
        }
        for(size_t i = 0; i < count; i++){
            decoded.tiles[position + i].floor = floor;
            decoded.tiles[position + i].overlay = overlay;
        }
        position += count;
    }

    position = 0;
    while(position < tile_count){
        uint16_t block = 0, packed = 0;
        uint8_t encoded_count = 0;
        /* packed is encoded as a byte in the Java format; read it separately. */
        status = mc_buffer_read_u16_be(&input, &block);
        if(status != MC_OK) break;
        uint8_t packed_byte = 0;
        status = mc_buffer_read_u8(&input, &packed_byte);
        if(status != MC_OK) break;
        packed = packed_byte;
        if(packed != 0){
            /* Building/data records need their own codec and must not be silently lost. */
            status = MC_FORMAT_ERROR;
            break;
        }
        status = mc_buffer_read_u8(&input, &encoded_count);
        size_t count = (size_t)encoded_count + 1;
        if(status != MC_OK || count > tile_count - position){
            status = MC_FORMAT_ERROR;
            break;
        }
        for(size_t i = 0; i < count; i++) decoded.tiles[position + i].block = block;
        position += count;
    }

    if(status == MC_OK && input.position != input.size) status = MC_FORMAT_ERROR;
    if(status != MC_OK){
        mc_world_destroy(&decoded);
        return status;
    }

    mc_world_destroy(world);
    *world = decoded;
    return MC_OK;
}
