#include "mindustry_save_loader.h"
#include "mindustry_deflate.h"

#include <stdlib.h>
#include <string.h>

static McStatus copy_region(McBuffer *input, McSaveBlob *blob){
    const uint8_t *data = NULL;
    size_t size = 0;
    McStatus status = mc_save_read_region(input, &data, &size);
    if(status != MC_OK) return status;
    if(size == 0){
        blob->data = NULL;
        blob->size = 0;
        return MC_OK;
    }
    blob->data = malloc(size);
    if(blob->data == NULL) return MC_OUT_OF_MEMORY;
    memcpy(blob->data, data, size);
    blob->size = size;
    return MC_OK;
}

static McStatus read_complete_string_map(const uint8_t *data, size_t size, McSaveTags *tags){
    McBuffer region = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status = mc_save_read_string_map(&region, tags);
    return status == MC_OK && region.position == region.size ? MC_OK : MC_FORMAT_ERROR;
}

static McStatus read_complete_content_header(const uint8_t *data, size_t size, McContentHeader *header){
    McBuffer region = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status = mc_save_read_content_header(&region, header);
    return status == MC_OK && region.position == region.size ? MC_OK : MC_FORMAT_ERROR;
}

void mc_save_file_destroy(McSaveFile *save){
    if(save == NULL) return;
    mc_save_tags_destroy(&save->meta);
    mc_content_header_destroy(&save->content);
    mc_map_section_destroy(&save->map);
    free(save->patches.data);
    free(save->entities.data);
    free(save->markers.data);
    free(save->custom.data);
    *save = (McSaveFile){0};
}

McStatus mc_save_file_load(const uint8_t *compressed, size_t size, McSaveFile *save){
    if(compressed == NULL || save == NULL || size == 0) return MC_INVALID_ARGUMENT;
    *save = (McSaveFile){0};
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
    if(save->version >= 12){
        status = copy_region(&input, &save->patches);
        if(status != MC_OK || save->patches.size < 8){
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
    if(status != MC_OK || mc_map_section_read(region_data, region_size, &save->map) != MC_OK){
        status = MC_FORMAT_ERROR;
        goto failure;
    }
    status = copy_region(&input, &save->entities);
    if(status != MC_OK) goto failure;
    if(save->version >= 8){
        status = copy_region(&input, &save->markers);
        if(status != MC_OK) goto failure;
    }
    status = copy_region(&input, &save->custom);
    if(status != MC_OK || input.position != input.size){
        status = MC_FORMAT_ERROR;
        goto failure;
    }

    mc_buffer_destroy(&inflated);
    return MC_OK;

failure:
    mc_buffer_destroy(&inflated);
    mc_save_file_destroy(save);
    return status == MC_OK ? MC_FORMAT_ERROR : status;
}

static McStatus write_blob_region(McBuffer *stream, const McSaveBlob *blob){
    return mc_save_write_region(stream, blob->data, blob->size);
}

McStatus mc_save_file_write(const McSaveFile *save, McBuffer *compressed){
    if(save == NULL || compressed == NULL || save->version < 8 || save->version > 13 ||
       save->map.tiles == NULL || save->map.width == 0 || save->map.height == 0 ||
       save->meta.count > UINT16_MAX || save->content.count > UINT8_MAX) return MC_INVALID_ARGUMENT;

    McBuffer stream, region, default_patches;
    mc_buffer_init(&stream);
    mc_buffer_init(&region);
    mc_buffer_init(&default_patches);
    McSaveTag *meta_tags = NULL;
    McContentGroupView *content_views = NULL;
    McStatus status = mc_save_write_header(&stream, save->version);
    if(status != MC_OK) goto cleanup;

    if(save->meta.count != 0){
        meta_tags = calloc(save->meta.count, sizeof(*meta_tags));
        if(meta_tags == NULL){ status = MC_OUT_OF_MEMORY; goto cleanup; }
        for(size_t i = 0; i < save->meta.count; i++){
            meta_tags[i] = (McSaveTag){save->meta.keys[i], save->meta.values[i]};
        }
    }
    status = mc_save_write_string_map(&region, meta_tags, save->meta.count);
    if(status != MC_OK) goto cleanup;
    status = mc_save_write_region(&stream, region.data, region.size);
    if(status != MC_OK) goto cleanup;

    if(save->version >= 12){
        if(save->patches.size == 0){
            status = mc_buffer_write_u32_be(&default_patches, 2);
            if(status == MC_OK) status = mc_buffer_write_u32_be(&default_patches, 0);
            if(status != MC_OK) goto cleanup;
            status = mc_save_write_region(&stream, default_patches.data, default_patches.size);
        }else{
            status = write_blob_region(&stream, &save->patches);
        }
        if(status != MC_OK) goto cleanup;
    }

    if(save->content.count != 0){
        content_views = calloc(save->content.count, sizeof(*content_views));
        if(content_views == NULL){ status = MC_OUT_OF_MEMORY; goto cleanup; }
        for(size_t i = 0; i < save->content.count; i++){
            content_views[i] = (McContentGroupView){
                save->content.groups[i].type,
                (const char *const *)save->content.groups[i].names,
                save->content.groups[i].count
            };
        }
    }
    mc_buffer_clear(&region);
    status = mc_save_write_content_header(&region, content_views, save->content.count);
    if(status != MC_OK) goto cleanup;
    status = mc_save_write_region(&stream, region.data, region.size);
    if(status != MC_OK) goto cleanup;

    mc_buffer_clear(&region);
    status = mc_map_section_write(&save->map, &region);
    if(status != MC_OK) goto cleanup;
    status = mc_save_write_region(&stream, region.data, region.size);
    if(status != MC_OK) goto cleanup;
    status = write_blob_region(&stream, &save->entities);
    if(status != MC_OK) goto cleanup;
    status = write_blob_region(&stream, &save->markers);
    if(status != MC_OK) goto cleanup;
    status = write_blob_region(&stream, &save->custom);
    if(status != MC_OK) goto cleanup;

    status = mc_zlib_compress_stored(stream.data, stream.size, compressed);

cleanup:
    free(meta_tags);
    free(content_views);
    mc_buffer_destroy(&default_patches);
    mc_buffer_destroy(&region);
    mc_buffer_destroy(&stream);
    return status;
}
