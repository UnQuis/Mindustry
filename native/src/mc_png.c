#include "mindustry_png.h"
#include "mindustry_deflate.h"

#include <stdlib.h>
#include <string.h>

static uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t size){
    for(size_t i = 0; i < size; i++){
        crc ^= data[i];
        for(unsigned bit = 0; bit < 8; bit++) crc = (crc >> 1) ^ (UINT32_C(0xedb88320) & (0u - (crc & 1u)));
    }
    return crc;
}

static uint32_t crc32_two(const uint8_t *first, size_t first_size, const uint8_t *second, size_t second_size){
    uint32_t crc = UINT32_C(0xffffffff);
    crc = crc32_update(crc, first, first_size);
    crc = crc32_update(crc, second, second_size);
    return ~crc;
}

static uint32_t read_u32_be(const uint8_t *data){
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
        ((uint32_t)data[2] << 8) | data[3];
}

static uint8_t paeth(uint8_t left, uint8_t up, uint8_t upper_left){
    int p = (int)left + (int)up - (int)upper_left;
    int pa = abs(p - (int)left);
    int pb = abs(p - (int)up);
    int pc = abs(p - (int)upper_left);
    return pa <= pb && pa <= pc ? left : pb <= pc ? up : upper_left;
}

void mc_image_destroy(McImage *image){
    if(image == NULL) return;
    free(image->rgba);
    *image = (McImage){0};
}

McStatus mc_png_read(const uint8_t *data, size_t size, McImage *image){
    static const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if(data == NULL || image == NULL || size < sizeof(signature) || memcmp(data, signature, sizeof(signature)) != 0){
        return MC_INVALID_ARGUMENT;
    }

    bool got_header = false, got_end = false;
    uint32_t width = 0, height = 0;
    uint8_t color_type = 0;
    McBuffer compressed;
    mc_buffer_init(&compressed);
    size_t position = sizeof(signature);
    McStatus status = MC_OK;
    while(position < size){
        if(size - position < 12) { status = MC_FORMAT_ERROR; break; }
        uint32_t length = read_u32_be(data + position);
        if((size_t)length > size - position - 12) { status = MC_FORMAT_ERROR; break; }
        const uint8_t *type = data + position + 4;
        const uint8_t *payload = data + position + 8;
        const uint8_t *stored_crc = payload + length;
        if(crc32_two(type, 4, payload, length) != read_u32_be(stored_crc)){
            status = MC_FORMAT_ERROR;
            break;
        }

        if(memcmp(type, "IHDR", 4) == 0){
            if(got_header || length != 13) { status = MC_FORMAT_ERROR; break; }
            width = read_u32_be(payload);
            height = read_u32_be(payload + 4);
            uint8_t bit_depth = payload[8];
            color_type = payload[9];
            if(width == 0 || height == 0 || bit_depth != 8 || (color_type != 2 && color_type != 6) ||
               payload[10] != 0 || payload[11] != 0 || payload[12] != 0){
                status = MC_FORMAT_ERROR;
                break;
            }
            got_header = true;
        }else if(memcmp(type, "IDAT", 4) == 0){
            if(!got_header || mc_buffer_write_bytes(&compressed, payload, length) != MC_OK){
                status = MC_FORMAT_ERROR;
                break;
            }
        }else if(memcmp(type, "IEND", 4) == 0){
            if(length != 0) { status = MC_FORMAT_ERROR; break; }
            got_end = true;
            position += 12;
            break;
        }
        position += 12 + length;
    }
    if(status != MC_OK || !got_header || !got_end || position != size || compressed.size == 0){
        mc_buffer_destroy(&compressed);
        return status == MC_OK ? MC_FORMAT_ERROR : status;
    }

    McBuffer scanlines;
    mc_buffer_init(&scanlines);
    status = mc_zlib_decompress(compressed.data, compressed.size, &scanlines);
    mc_buffer_destroy(&compressed);
    if(status != MC_OK) goto failure;

    size_t bytes_per_pixel = color_type == 6 ? 4 : 3;
    if((size_t)width > SIZE_MAX / bytes_per_pixel ||
       (size_t)width * bytes_per_pixel + 1 > SIZE_MAX / height){
        status = MC_CAPACITY_EXCEEDED;
        goto failure;
    }
    size_t row_bytes = (size_t)width * bytes_per_pixel;
    size_t raw_size = (row_bytes + 1) * height;
    if(scanlines.size != raw_size || row_bytes > SIZE_MAX / height ||
       (size_t)width > SIZE_MAX / (size_t)height || (size_t)width * height > SIZE_MAX / 4){
        status = MC_FORMAT_ERROR;
        goto failure;
    }

    uint8_t *rows = calloc(row_bytes * height, 1);
    if(rows == NULL){
        status = MC_OUT_OF_MEMORY;
        goto failure;
    }
    size_t source = 0;
    for(uint32_t y = 0; y < height; y++){
        uint8_t filter = scanlines.data[source++];
        uint8_t *row = rows + (size_t)y * row_bytes;
        const uint8_t *previous = y == 0 ? NULL : row - row_bytes;
        if(filter > 4){
            free(rows);
            status = MC_FORMAT_ERROR;
            goto failure;
        }
        for(size_t x = 0; x < row_bytes; x++){
            uint8_t raw = scanlines.data[source++];
            uint8_t left = x < bytes_per_pixel ? 0 : row[x - bytes_per_pixel];
            uint8_t up = previous == NULL ? 0 : previous[x];
            uint8_t upper_left = previous == NULL || x < bytes_per_pixel ? 0 : previous[x - bytes_per_pixel];
            switch(filter){
                case 0: row[x] = raw; break;
                case 1: row[x] = (uint8_t)(raw + left); break;
                case 2: row[x] = (uint8_t)(raw + up); break;
                case 3: row[x] = (uint8_t)(raw + (uint8_t)(((unsigned)left + up) / 2)); break;
                case 4: row[x] = (uint8_t)(raw + paeth(left, up, upper_left)); break;
                default: break;
            }
        }
    }

    McImage decoded = {.width = width, .height = height};
    decoded.rgba = malloc((size_t)width * height * 4);
    if(decoded.rgba == NULL){
        free(rows);
        status = MC_OUT_OF_MEMORY;
        goto failure;
    }
    for(uint32_t y = 0; y < height; y++){
        const uint8_t *row = rows + (size_t)y * row_bytes;
        uint8_t *destination = decoded.rgba + (size_t)y * width * 4;
        for(uint32_t x = 0; x < width; x++){
            destination[x * 4] = row[x * bytes_per_pixel];
            destination[x * 4 + 1] = row[x * bytes_per_pixel + 1];
            destination[x * 4 + 2] = row[x * bytes_per_pixel + 2];
            destination[x * 4 + 3] = color_type == 6 ? row[x * bytes_per_pixel + 3] : 255;
        }
    }
    free(rows);
    mc_buffer_destroy(&scanlines);
    mc_image_destroy(image);
    *image = decoded;
    return MC_OK;

failure:
    mc_buffer_destroy(&scanlines);
    return status;
}

static McStatus write_chunk(McBuffer *output, const char type[4], const uint8_t *data, size_t size){
    if(size > UINT32_MAX) return MC_CAPACITY_EXCEEDED;
    McStatus status = mc_buffer_write_u32_be(output, (uint32_t)size);
    if(status != MC_OK) return status;
    status = mc_buffer_write_bytes(output, type, 4);
    if(status != MC_OK) return status;
    status = mc_buffer_write_bytes(output, data, size);
    if(status != MC_OK) return status;
    return mc_buffer_write_u32_be(output, crc32_two((const uint8_t *)type, 4, data, size));
}

McStatus mc_png_write(const McImage *image, McBuffer *output){
    static const uint8_t signature[] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    if(image == NULL || output == NULL || image->width == 0 || image->height == 0 || image->rgba == NULL ||
       image->width > UINT32_MAX / 4u || (size_t)image->width * image->height > SIZE_MAX / 4){
        return MC_INVALID_ARGUMENT;
    }
    size_t row_bytes = (size_t)image->width * 4;
    if(row_bytes > SIZE_MAX - 1 || row_bytes + 1 > SIZE_MAX / image->height) return MC_CAPACITY_EXCEEDED;
    McBuffer scanlines, compressed;
    mc_buffer_init(&scanlines);
    mc_buffer_init(&compressed);
    McStatus status = MC_OK;
    for(uint32_t y = 0; y < image->height; y++){
        status = mc_buffer_write_u8(&scanlines, 0);
        if(status != MC_OK) goto cleanup;
        status = mc_buffer_write_bytes(&scanlines, image->rgba + (size_t)y * row_bytes, row_bytes);
        if(status != MC_OK) goto cleanup;
    }
    status = mc_zlib_compress_stored(scanlines.data, scanlines.size, &compressed);
    if(status != MC_OK) goto cleanup;

    mc_buffer_clear(output);
    status = mc_buffer_write_bytes(output, signature, sizeof(signature));
    uint8_t header[13] = {
        (uint8_t)(image->width >> 24), (uint8_t)(image->width >> 16), (uint8_t)(image->width >> 8), (uint8_t)image->width,
        (uint8_t)(image->height >> 24), (uint8_t)(image->height >> 16), (uint8_t)(image->height >> 8), (uint8_t)image->height,
        8, 6, 0, 0, 0
    };
    if(status == MC_OK) status = write_chunk(output, "IHDR", header, sizeof(header));
    if(status == MC_OK) status = write_chunk(output, "IDAT", compressed.data, compressed.size);
    if(status == MC_OK) status = write_chunk(output, "IEND", NULL, 0);

cleanup:
    mc_buffer_destroy(&compressed);
    mc_buffer_destroy(&scanlines);
    return status;
}
