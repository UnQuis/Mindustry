#include "mindustry_typeio.h"
#include "mindustry_save.h"

#include <stdlib.h>

static McStatus skip_bytes(McBuffer *input, size_t amount){
    if(input == NULL || input->position > input->size || amount > input->size - input->position){
        return MC_FORMAT_ERROR;
    }
    input->position += amount;
    return MC_OK;
}

static McStatus read_u32(McBuffer *input, uint32_t *value){
    return mc_buffer_read_u32_be(input, value);
}

static McStatus read_count_u16(McBuffer *input, size_t *count){
    uint16_t value = 0;
    if(mc_buffer_read_u16_be(input, &value) != MC_OK || value > 1000) return MC_FORMAT_ERROR;
    *count = value;
    return MC_OK;
}

static McStatus skip_object(McBuffer *input, unsigned depth){
    if(depth > 32) return MC_FORMAT_ERROR;
    uint8_t type = 0;
    if(mc_buffer_read_u8(input, &type) != MC_OK) return MC_FORMAT_ERROR;
    uint32_t length = 0;
    size_t count = 0;
    switch(type){
        case 0: /* null */
            return MC_OK;
        case 1: /* integer */
        case 3: /* float */
            return skip_bytes(input, 4);
        case 2: /* long */
            return skip_bytes(input, 8);
        case 4: /* nullable Java modified-UTF string */
            if(mc_buffer_read_u8(input, &type) != MC_OK) return MC_FORMAT_ERROR;
            if(type == 0) return MC_OK;
            char *string = NULL;
            if(mc_save_read_utf(input, &string) != MC_OK) return MC_FORMAT_ERROR;
            free(string);
            return MC_OK;
        case 5: /* content type + content ID */
            return skip_bytes(input, 3);
        case 6: /* IntSeq */
            if(read_count_u16(input, &count) != MC_OK) return MC_FORMAT_ERROR;
            return skip_bytes(input, count * 4);
        case 7: /* Point2 */
            return skip_bytes(input, 8);
        case 8: /* Point2[] */
            if(mc_buffer_read_u8(input, &type) != MC_OK) return MC_FORMAT_ERROR;
            return skip_bytes(input, (size_t)type * 4);
        case 9: /* TechNode */
            return skip_bytes(input, 3);
        case 10: /* boolean */
            return skip_bytes(input, 1);
        case 11: /* double */
        case 12: /* Building position */
            return skip_bytes(input, type == 11 ? 8 : 4);
        case 13: /* LAccess ordinal */
            return skip_bytes(input, 2);
        case 14: /* byte[] */
        case 16: /* boolean[] */
        case 22: /* Object[] */
            if(read_u32(input, &length) != MC_OK || length > 1000) return MC_FORMAT_ERROR;
            if(type == 14) return skip_bytes(input, length);
            if(type == 16) return skip_bytes(input, length);
            for(uint32_t i = 0; i < length; i++){
                if(skip_object(input, depth + 1) != MC_OK) return MC_FORMAT_ERROR;
            }
            return MC_OK;
        case 15: /* legacy */
            return skip_bytes(input, 1);
        case 17: /* unit ID */
            return skip_bytes(input, 4);
        case 18: /* Vec2[] */
            if(read_count_u16(input, &count) != MC_OK) return MC_FORMAT_ERROR;
            return skip_bytes(input, count * 8);
        case 19: /* Vec2 */
            return skip_bytes(input, 8);
        case 20: /* Team */
            return skip_bytes(input, 1);
        case 21: /* int[] */
            if(read_count_u16(input, &count) != MC_OK) return MC_FORMAT_ERROR;
            return skip_bytes(input, count * 4);
        case 23: /* UnitCommand */
            return skip_bytes(input, 2);
        default:
            return MC_FORMAT_ERROR;
    }
}

McStatus mc_typeio_skip(McBuffer *input, size_t *bytes){
    if(input == NULL || bytes == NULL) return MC_INVALID_ARGUMENT;
    size_t start = input->position;
    McStatus status = skip_object(input, 0);
    if(status != MC_OK){
        input->position = start;
        return status;
    }
    *bytes = input->position - start;
    return MC_OK;
}
