#include "mindustry_save.h"
#include "mindustry_deflate.h"

#include <string.h>

static McStatus read_complete_string_map(const uint8_t *data, size_t size, McSaveTags *tags){
    McBuffer region = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status = mc_save_read_string_map(&region, tags);
    if(status != MC_OK) return status;
    return region.position == region.size ? MC_OK : MC_FORMAT_ERROR;
}

static McStatus read_complete_content_header(const uint8_t *data, size_t size, McContentHeader *header){
    McBuffer region = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status = mc_save_read_content_header(&region, header);
    if(status != MC_OK) return status;
    return region.position == region.size ? MC_OK : MC_FORMAT_ERROR;
}

static McStatus write_region_from_buffer(McBuffer *output, const McBuffer *region){
    return mc_save_write_region(output, region->data, region->size);
}

McStatus mc_save_write_plain_map(const McPlainMapSaveOptions *options, McBuffer *compressed){
    if(options == NULL || compressed == NULL || options->world == NULL || options->version != 13){
        return MC_INVALID_ARGUMENT;
    }
    /* This writer deliberately has no building/entity serializer yet. */
    for(size_t i = 0; i < (size_t)options->world->width * options->world->height; i++){
        if(options->world->tiles[i].block != MC_BLOCK_AIR) return MC_FORMAT_ERROR;
    }

    McBuffer stream, region;
    mc_buffer_init(&stream);
    mc_buffer_init(&region);
    McStatus status = mc_save_write_header(&stream, options->version);
    if(status != MC_OK) goto cleanup;

    status = mc_save_write_string_map(&region, options->meta, options->meta_count);
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    mc_buffer_clear(&region);
    status = mc_buffer_write_u32_be(&region, 2); /* DataPatcher.patchFormatVersion. */
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u32_be(&region, 0); /* no external data assets */
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    mc_buffer_clear(&region);
    status = mc_save_write_content_header(&region, options->content, options->content_count);
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    mc_buffer_clear(&region);
    status = mc_map_write_section(options->world, &region);
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    /* Empty entity mapping, one empty sharded team plan list, no entities. */
    mc_buffer_clear(&region);
    status = mc_buffer_write_u16_be(&region, 0);
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u32_be(&region, 1);
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u32_be(&region, 1);
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u32_be(&region, 0);
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u32_be(&region, 0);
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    /* Empty MapMarkers is an empty UBJSON object. */
    mc_buffer_clear(&region);
    status = mc_buffer_write_u8(&region, '{');
    if(status != MC_OK) goto cleanup;
    status = mc_buffer_write_u8(&region, '}');
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    /* No registered custom chunks. */
    mc_buffer_clear(&region);
    status = mc_buffer_write_u32_be(&region, 0);
    if(status != MC_OK) goto cleanup;
    status = write_region_from_buffer(&stream, &region);
    if(status != MC_OK) goto cleanup;

    status = mc_zlib_compress_stored(stream.data, stream.size, compressed);

cleanup:
    mc_buffer_destroy(&region);
    mc_buffer_destroy(&stream);
    return status;
}

void mc_plain_map_save_destroy(McPlainMapSave *save){
    if(save == NULL) return;
    mc_save_tags_destroy(&save->meta);
    mc_content_header_destroy(&save->content);
    mc_world_destroy(&save->world);
    *save = (McPlainMapSave){0};
}

McStatus mc_save_read_plain_map(const uint8_t *compressed, size_t size, McPlainMapSave *save){
    if(compressed == NULL || save == NULL || size == 0) return MC_INVALID_ARGUMENT;
    *save = (McPlainMapSave){0};

    McBuffer inflated;
    mc_buffer_init(&inflated);
    McStatus status = mc_zlib_decompress(compressed, size, &inflated);
    if(status != MC_OK) goto failure;

    McBuffer input = {.data = inflated.data, .size = inflated.size, .capacity = inflated.size, .position = 0};
    status = mc_save_read_header(&input, &save->version);
    if(status != MC_OK || save->version < 8 || save->version > 13){
        status = MC_FORMAT_ERROR;
        goto failure;
    }

    const uint8_t *region_data = NULL;
    size_t region_size = 0;
    status = mc_save_read_region(&input, &region_data, &region_size);
    if(status != MC_OK || read_complete_string_map(region_data, region_size, &save->meta) != MC_OK){
        status = MC_FORMAT_ERROR;
        goto failure;
    }

    /* Version 12+ writes the data-patches region before the content header. */
    if(save->version >= 12){
        status = mc_save_read_region(&input, &region_data, &region_size);
        if(status != MC_OK || region_size < 8){
            status = MC_FORMAT_ERROR;
            goto failure;
        }
    }

    status = mc_save_read_region(&input, &region_data, &region_size);
    if(status != MC_OK || read_complete_content_header(region_data, region_size, &save->content) != MC_OK){
        status = MC_FORMAT_ERROR;
        goto failure;
    }

    status = mc_save_read_region(&input, &region_data, &region_size);
    if(status != MC_OK || mc_map_read_section(&save->world, region_data, region_size) != MC_OK){
        status = MC_FORMAT_ERROR;
        goto failure;
    }

    /* Entities, markers and custom chunks are not plain-map data yet. Validate
       their framing and skip their payloads rather than pretending they loaded. */
    status = mc_save_read_region(&input, &region_data, &region_size);
    if(status != MC_OK) goto failure;
    status = mc_save_read_region(&input, &region_data, &region_size);
    if(status != MC_OK) goto failure;
    status = mc_save_read_region(&input, &region_data, &region_size);
    if(status != MC_OK || input.position != input.size) goto failure;

    mc_buffer_destroy(&inflated);
    return MC_OK;

failure:
    mc_buffer_destroy(&inflated);
    mc_plain_map_save_destroy(save);
    return status == MC_OK ? MC_FORMAT_ERROR : status;
}
