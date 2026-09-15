#include "mindustry_custom.h"

#include <stdlib.h>
#include <string.h>

static McStatus read_count(McBuffer *input, size_t *count){
    uint32_t encoded = 0;
    if(mc_buffer_read_u32_be(input, &encoded) != MC_OK || encoded > INT32_MAX ||
       (size_t)encoded > input->size - input->position) return MC_FORMAT_ERROR;
    *count = (size_t)encoded;
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

void mc_custom_chunks_destroy(McCustomChunks *chunks){
    if(chunks == NULL) return;
    for(size_t i = 0; i < chunks->count; i++){
        free(chunks->chunks[i].name);
        free(chunks->chunks[i].data);
    }
    free(chunks->chunks);
    *chunks = (McCustomChunks){0};
}

McStatus mc_custom_chunks_read(const uint8_t *data, size_t size, McCustomChunks *chunks){
    if(data == NULL || chunks == NULL || size == 0) return MC_INVALID_ARGUMENT;
    *chunks = (McCustomChunks){0};
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status = read_count(&input, &chunks->count);
    if(status != MC_OK) return status;
    if(chunks->count != 0){
        chunks->chunks = calloc(chunks->count, sizeof(*chunks->chunks));
        if(chunks->chunks == NULL) return MC_OUT_OF_MEMORY;
    }
    for(size_t i = 0; i < chunks->count; i++){
        if(mc_save_read_utf(&input, &chunks->chunks[i].name) != MC_OK){ status = MC_FORMAT_ERROR; goto failure; }
        uint32_t length = 0;
        if(mc_buffer_read_u32_be(&input, &length) != MC_OK || length > input.size - input.position){
            status = MC_FORMAT_ERROR;
            goto failure;
        }
        chunks->chunks[i].size = length;
        status = copy_bytes(&input, length, &chunks->chunks[i].data);
        if(status != MC_OK) goto failure;
    }
    if(input.position != input.size){ status = MC_FORMAT_ERROR; goto failure; }
    return MC_OK;

failure:
    mc_custom_chunks_destroy(chunks);
    return status;
}

McStatus mc_custom_chunks_write(const McCustomChunks *chunks, McBuffer *output){
    if(chunks == NULL || output == NULL || chunks->count > INT32_MAX) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_u32_be(output, (uint32_t)chunks->count);
    for(size_t i = 0; status == MC_OK && i < chunks->count; i++){
        const McCustomChunk *chunk = &chunks->chunks[i];
        if(chunk->name == NULL || chunk->size > UINT32_MAX || (chunk->data == NULL && chunk->size != 0)) return MC_INVALID_ARGUMENT;
        status = mc_save_write_utf(output, chunk->name);
        if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)chunk->size);
        if(status == MC_OK) status = mc_buffer_write_bytes(output, chunk->data, chunk->size);
    }
    return status;
}
