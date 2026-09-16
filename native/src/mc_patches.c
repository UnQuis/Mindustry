#include "mindustry_patches.h"

#include <stdlib.h>
#include <string.h>

static McStatus read_i32_length(McBuffer *input, size_t *length){
    uint32_t encoded = 0;
    if(input->position > input->size || mc_buffer_read_u32_be(input, &encoded) != MC_OK){
        return MC_FORMAT_ERROR;
    }
#if SIZE_MAX < UINT32_MAX
    if(encoded > (uint32_t)SIZE_MAX) return MC_FORMAT_ERROR;
#endif
    *length = (size_t)encoded;
    if(*length > input->size - input->position) return MC_FORMAT_ERROR;
    return MC_OK;
}

static McStatus copy_bytes(McBuffer *input, size_t size, uint8_t **destination){
    *destination = NULL;
    if(size == 0) return MC_OK;
    *destination = malloc(size);
    if(*destination == NULL) return MC_OUT_OF_MEMORY;
    if(mc_buffer_read_bytes(input, *destination, size) != MC_OK){
        free(*destination);
        *destination = NULL;
        return MC_FORMAT_ERROR;
    }
    return MC_OK;
}

void mc_data_patches_destroy(McDataPatches *patches){
    if(patches == NULL) return;
    for(size_t i = 0; i < patches->count; i++){
        free(patches->assets[i].name);
        free(patches->assets[i].data);
    }
    free(patches->assets);
    *patches = (McDataPatches){0};
}

static McStatus reserve_assets(McDataPatches *patches, size_t count){
    if(count == 0) return MC_OK;
    if(count > SIZE_MAX / sizeof(*patches->assets)) return MC_CAPACITY_EXCEEDED;
    patches->assets = calloc(count, sizeof(*patches->assets));
    if(patches->assets == NULL) return MC_OUT_OF_MEMORY;
    patches->count = count;
    return MC_OK;
}

static McStatus read_text_assets(McBuffer *input, uint32_t amount, McDataPatches *patches){
    if(amount > input->size - input->position) return MC_FORMAT_ERROR;
    McStatus status = reserve_assets(patches, amount);
    if(status != MC_OK) return status;
    for(size_t i = 0; i < amount; i++){
        size_t length = 0;
        status = read_i32_length(input, &length);
        if(status != MC_OK) return status;
        patches->assets[i].kind = MC_PATCH_TEXT;
        status = copy_bytes(input, length, &patches->assets[i].data);
        if(status != MC_OK) return status;
        patches->assets[i].size = length;
    }
    return MC_OK;
}

McStatus mc_data_patches_read(uint32_t save_version, const uint8_t *data, size_t size, McDataPatches *patches){
    if(data == NULL || patches == NULL || size == 0 || save_version < 11 || save_version > 13){
        return MC_INVALID_ARGUMENT;
    }
    *patches = (McDataPatches){0};
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint32_t amount = 0;
    McStatus status;

    if(save_version == 11){
        uint8_t legacy_amount = 0;
        status = mc_buffer_read_u8(&input, &legacy_amount);
        if(status != MC_OK) return MC_FORMAT_ERROR;
        patches->legacy = true;
        status = read_text_assets(&input, legacy_amount, patches);
    }else{
        status = mc_buffer_read_u32_be(&input, &patches->format_version);
        if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &amount);
        if(status == MC_OK) status = read_text_assets(&input, amount, patches);
        if(status == MC_OK){
            uint32_t image_amount = 0;
            status = mc_buffer_read_u32_be(&input, &image_amount);
            if(status == MC_OK){
                if(image_amount > (uint32_t)(input.size - input.position)) status = MC_FORMAT_ERROR;
                if(status == MC_OK && image_amount != 0){
                    if(image_amount > SIZE_MAX - patches->count) status = MC_CAPACITY_EXCEEDED;
                    else{
                        size_t old_count = patches->count;
                        size_t new_count = old_count + image_amount;
                        McPatchAsset *assets = realloc(patches->assets, new_count * sizeof(*assets));
                        if(assets == NULL) status = MC_OUT_OF_MEMORY;
                        else{
                            patches->assets = assets;
                            memset(patches->assets + old_count, 0, image_amount * sizeof(*assets));
                            patches->count = new_count;
                            for(size_t i = old_count; i < new_count && status == MC_OK; i++){
                                status = mc_save_read_utf(&input, &patches->assets[i].name);
                                if(status != MC_OK) break;
                                patches->assets[i].kind = MC_PATCH_IMAGE;
                                if(mc_buffer_read_u16_be(&input, &patches->assets[i].width) != MC_OK ||
                                   mc_buffer_read_u16_be(&input, &patches->assets[i].height) != MC_OK){
                                    status = MC_FORMAT_ERROR;
                                    break;
                                }
                                size_t length = 0;
                                status = read_i32_length(&input, &length);
                                if(status != MC_OK) break;
                                status = copy_bytes(&input, length, &patches->assets[i].data);
                                patches->assets[i].size = length;
                            }
                        }
                    }
                }
            }
        }
    }
    if(status != MC_OK || input.position != input.size){
        mc_data_patches_destroy(patches);
        return status == MC_OK ? MC_FORMAT_ERROR : status;
    }
    return MC_OK;
}

static McStatus write_i32_length(McBuffer *output, size_t size){
    if(size > UINT32_MAX) return MC_CAPACITY_EXCEEDED;
    return mc_buffer_write_u32_be(output, (uint32_t)size);
}

static McStatus write_text_asset(McBuffer *output, const McPatchAsset *asset){
    if(asset->data == NULL && asset->size != 0) return MC_INVALID_ARGUMENT;
    McStatus status = write_i32_length(output, asset->size);
    if(status == MC_OK) status = mc_buffer_write_bytes(output, asset->data, asset->size);
    return status;
}

McStatus mc_data_patches_write(uint32_t save_version, const McDataPatches *patches, McBuffer *output){
    if(patches == NULL || output == NULL || save_version < 11 || save_version > 13 ||
       patches->count > UINT32_MAX) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    if(save_version == 11){
        if(patches->count > UINT8_MAX) return MC_CAPACITY_EXCEEDED;
        McStatus status = mc_buffer_write_u8(output, (uint8_t)patches->count);
        for(size_t i = 0; status == MC_OK && i < patches->count; i++){
            if(patches->assets[i].kind != MC_PATCH_TEXT) return MC_INVALID_ARGUMENT;
            status = write_text_asset(output, &patches->assets[i]);
        }
        return status;
    }

    size_t text_count = 0, image_count = 0;
    for(size_t i = 0; i < patches->count; i++){
        if(patches->assets[i].kind == MC_PATCH_TEXT) text_count++;
        else if(patches->assets[i].kind == MC_PATCH_IMAGE) image_count++;
        else return MC_INVALID_ARGUMENT;
    }
    if(text_count > UINT32_MAX || image_count > UINT32_MAX) return MC_CAPACITY_EXCEEDED;
    McStatus status = mc_buffer_write_u32_be(output, patches->format_version);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)text_count);
    for(size_t i = 0; status == MC_OK && i < patches->count; i++){
        if(patches->assets[i].kind == MC_PATCH_TEXT) status = write_text_asset(output, &patches->assets[i]);
    }
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)image_count);
    for(size_t i = 0; status == MC_OK && i < patches->count; i++){
        const McPatchAsset *asset = &patches->assets[i];
        if(asset->kind != MC_PATCH_IMAGE) continue;
        if(asset->name == NULL) return MC_INVALID_ARGUMENT;
        status = mc_save_write_utf(output, asset->name);
        if(status == MC_OK) status = mc_buffer_write_u16_be(output, asset->width);
        if(status == MC_OK) status = mc_buffer_write_u16_be(output, asset->height);
        if(status == MC_OK) status = write_text_asset(output, asset);
    }
    return status;
}
