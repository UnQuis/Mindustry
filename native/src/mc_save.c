#include "mindustry_save.h"

#include <stdlib.h>
#include <string.h>

static McStatus utf8_next(const char **cursor, uint32_t *codepoint){
    const unsigned char *text = (const unsigned char *)*cursor;
    if(text == NULL || *text == 0 || codepoint == NULL) return MC_FORMAT_ERROR;

    uint32_t value;
    size_t length;
    if(text[0] < 0x80u){
        value = text[0];
        length = 1;
    }else if((text[0] & 0xe0u) == 0xc0u){
        value = text[0] & 0x1fu;
        length = 2;
    }else if((text[0] & 0xf0u) == 0xe0u){
        value = text[0] & 0x0fu;
        length = 3;
    }else if((text[0] & 0xf8u) == 0xf0u){
        value = text[0] & 0x07u;
        length = 4;
    }else{
        return MC_FORMAT_ERROR;
    }

    for(size_t i = 1; i < length; i++){
        if((text[i] & 0xc0u) != 0x80u) return MC_FORMAT_ERROR;
        value = (value << 6) | (text[i] & 0x3fu);
    }
    if((length == 2 && value < 0x80u) || (length == 3 && value < 0x800u) ||
       (length == 4 && value < 0x10000u) || value > 0x10ffffu ||
       (value >= 0xd800u && value <= 0xdfffu)) return MC_FORMAT_ERROR;

    *cursor += length;
    *codepoint = value;
    return MC_OK;
}

static size_t modified_utf_size(uint32_t codepoint){
    if(codepoint == 0) return 2;
    if(codepoint <= 0x7fu) return 1;
    if(codepoint <= 0x7ffu) return 2;
    if(codepoint <= 0xffffu) return 3;
    return 6;
}

static size_t write_modified_code_unit(uint8_t *destination, uint16_t value){
    if(value >= 1 && value <= 0x7fu){
        destination[0] = (uint8_t)value;
        return 1;
    }
    if(value <= 0x7ffu){
        destination[0] = (uint8_t)(0xc0u | (value >> 6));
        destination[1] = (uint8_t)(0x80u | (value & 0x3fu));
        return 2;
    }
    destination[0] = (uint8_t)(0xe0u | (value >> 12));
    destination[1] = (uint8_t)(0x80u | ((value >> 6) & 0x3fu));
    destination[2] = (uint8_t)(0x80u | (value & 0x3fu));
    return 3;
}

McStatus mc_save_write_utf(McBuffer *output, const char *utf8){
    if(output == NULL || utf8 == NULL) return MC_INVALID_ARGUMENT;

    size_t encoded_size = 0;
    const char *cursor = utf8;
    while(*cursor != '\0'){
        uint32_t codepoint = 0;
        McStatus status = utf8_next(&cursor, &codepoint);
        if(status != MC_OK) return status;
        size_t part = modified_utf_size(codepoint);
        if(part > SIZE_MAX - encoded_size) return MC_CAPACITY_EXCEEDED;
        encoded_size += part;
    }
    if(encoded_size > UINT16_MAX) return MC_CAPACITY_EXCEEDED;

    McStatus status = mc_buffer_write_u16_be(output, (uint16_t)encoded_size);
    if(status != MC_OK) return status;
    cursor = utf8;
    while(*cursor != '\0'){
        uint32_t codepoint = 0;
        status = utf8_next(&cursor, &codepoint);
        if(status != MC_OK) return status;

        uint16_t code_units[2];
        size_t unit_count = 1;
        if(codepoint > 0xffffu){
            uint32_t value = codepoint - 0x10000u;
            code_units[0] = (uint16_t)(0xd800u | (value >> 10));
            code_units[1] = (uint16_t)(0xdc00u | (value & 0x3ffu));
            unit_count = 2;
        }else{
            code_units[0] = (uint16_t)codepoint;
        }
        for(size_t i = 0; i < unit_count; i++){
            uint8_t bytes[3];
            size_t byte_count = write_modified_code_unit(bytes, code_units[i]);
            status = mc_buffer_write_bytes(output, bytes, byte_count);
            if(status != MC_OK) return status;
        }
    }
    return MC_OK;
}

static McStatus read_modified_code_unit(McBuffer *input, uint16_t *unit){
    uint8_t first = 0;
    McStatus status = mc_buffer_read_u8(input, &first);
    if(status != MC_OK || first == 0) return MC_FORMAT_ERROR;

    if((first & 0x80u) == 0){
        *unit = first;
        return MC_OK;
    }
    if((first & 0xe0u) == 0xc0u){
        uint8_t second = 0;
        if(mc_buffer_read_u8(input, &second) != MC_OK || (second & 0xc0u) != 0x80u) return MC_FORMAT_ERROR;
        uint16_t value = (uint16_t)(((first & 0x1fu) << 6) | (second & 0x3fu));
        if(value != 0 && value < 0x80u) return MC_FORMAT_ERROR;
        *unit = value;
        return MC_OK;
    }
    if((first & 0xf0u) == 0xe0u){
        uint8_t second = 0, third = 0;
        if(mc_buffer_read_u8(input, &second) != MC_OK || mc_buffer_read_u8(input, &third) != MC_OK ||
           (second & 0xc0u) != 0x80u || (third & 0xc0u) != 0x80u) return MC_FORMAT_ERROR;
        uint16_t value = (uint16_t)(((first & 0x0fu) << 12) | ((second & 0x3fu) << 6) | (third & 0x3fu));
        if(value < 0x800u) return MC_FORMAT_ERROR;
        *unit = value;
        return MC_OK;
    }
    return MC_FORMAT_ERROR;
}

static size_t utf8_encoded_unit_size(uint16_t unit){
    return unit <= 0x7fu ? 1 : unit <= 0x7ffu ? 2 : 3;
}

static size_t write_utf8_code_unit(char *destination, uint16_t unit){
    if(unit <= 0x7fu){
        destination[0] = (char)unit;
        return 1;
    }
    if(unit <= 0x7ffu){
        destination[0] = (char)(0xc0u | (unit >> 6));
        destination[1] = (char)(0x80u | (unit & 0x3fu));
        return 2;
    }
    destination[0] = (char)(0xe0u | (unit >> 12));
    destination[1] = (char)(0x80u | ((unit >> 6) & 0x3fu));
    destination[2] = (char)(0x80u | (unit & 0x3fu));
    return 3;
}

McStatus mc_save_read_utf(McBuffer *input, char **utf8){
    if(input == NULL || utf8 == NULL) return MC_INVALID_ARGUMENT;
    *utf8 = NULL;
    uint16_t byte_count = 0;
    if(mc_buffer_read_u16_be(input, &byte_count) != MC_OK || input->position > input->size ||
       byte_count > input->size - input->position) return MC_FORMAT_ERROR;

    size_t start = input->position;
    size_t end = start + byte_count;
    size_t unit_count = 0;
    while(input->position < end){
        uint16_t unit = 0;
        if(read_modified_code_unit(input, &unit) != MC_OK || input->position > end){
            input->position = start;
            return MC_FORMAT_ERROR;
        }
        unit_count++;
    }
    if(input->position != end){
        input->position = start;
        return MC_FORMAT_ERROR;
    }

    uint16_t *units = unit_count == 0 ? NULL : malloc(unit_count * sizeof(*units));
    if(unit_count != 0 && units == NULL){
        input->position = start;
        return MC_OUT_OF_MEMORY;
    }
    input->position = start;
    for(size_t i = 0; i < unit_count; i++){
        if(read_modified_code_unit(input, &units[i]) != MC_OK){
            free(units);
            input->position = start;
            return MC_FORMAT_ERROR;
        }
    }

    size_t final_size = 0;
    for(size_t i = 0; i < unit_count; i++){
        size_t part = utf8_encoded_unit_size(units[i]);
        if(units[i] >= 0xd800u && units[i] <= 0xdbffu && i + 1 < unit_count &&
           units[i + 1] >= 0xdc00u && units[i + 1] <= 0xdfffu){
            part = 4;
            i++;
        }
        if(part > SIZE_MAX - final_size){
            free(units);
            return MC_CAPACITY_EXCEEDED;
        }
        final_size += part;
    }
    char *result = malloc(final_size + 1);
    if(result == NULL){
        free(units);
        return MC_OUT_OF_MEMORY;
    }

    size_t output_position = 0;
    for(size_t i = 0; i < unit_count; i++){
        uint16_t unit = units[i];
        if(unit >= 0xd800u && unit <= 0xdbffu && i + 1 < unit_count &&
           units[i + 1] >= 0xdc00u && units[i + 1] <= 0xdfffu){
            uint32_t codepoint = 0x10000u + (((uint32_t)unit - 0xd800u) << 10) + (units[++i] - 0xdc00u);
            result[output_position++] = (char)(0xf0u | (codepoint >> 18));
            result[output_position++] = (char)(0x80u | ((codepoint >> 12) & 0x3fu));
            result[output_position++] = (char)(0x80u | ((codepoint >> 6) & 0x3fu));
            result[output_position++] = (char)(0x80u | (codepoint & 0x3fu));
        }else{
            output_position += write_utf8_code_unit(result + output_position, unit);
        }
    }
    result[output_position] = '\0';
    free(units);
    *utf8 = result;
    return MC_OK;
}

McStatus mc_save_write_header(McBuffer *output, uint32_t version){
    if(output == NULL) return MC_INVALID_ARGUMENT;
    static const uint8_t header[] = {'M', 'S', 'A', 'V'};
    McStatus status = mc_buffer_write_bytes(output, header, sizeof(header));
    if(status != MC_OK) return status;
    return mc_buffer_write_u32_be(output, version);
}

McStatus mc_save_read_header(McBuffer *input, uint32_t *version){
    if(input == NULL || version == NULL) return MC_INVALID_ARGUMENT;
    uint8_t header[4];
    if(mc_buffer_read_bytes(input, header, sizeof(header)) != MC_OK || header[0] != 'M' ||
       header[1] != 'S' || header[2] != 'A' || header[3] != 'V') return MC_FORMAT_ERROR;
    return mc_buffer_read_u32_be(input, version);
}

McStatus mc_save_write_region(McBuffer *output, const uint8_t *data, size_t size){
    if(output == NULL || (data == NULL && size != 0)) return MC_INVALID_ARGUMENT;
    if(size > UINT32_MAX) return MC_CAPACITY_EXCEEDED;
    McStatus status = mc_buffer_write_u32_be(output, (uint32_t)size);
    if(status != MC_OK) return status;
    return mc_buffer_write_bytes(output, data, size);
}

McStatus mc_save_read_region(McBuffer *input, const uint8_t **data, size_t *size){
    if(input == NULL || data == NULL || size == NULL) return MC_INVALID_ARGUMENT;
    uint32_t region_size = 0;
    if(mc_buffer_read_u32_be(input, &region_size) != MC_OK || input->position > input->size ||
       region_size > input->size - input->position) return MC_FORMAT_ERROR;
    *data = input->data + input->position;
    *size = region_size;
    input->position += region_size;
    return MC_OK;
}

McStatus mc_save_write_string_map(McBuffer *output, const McSaveTag *tags, size_t count){
    if(output == NULL || (tags == NULL && count != 0)) return MC_INVALID_ARGUMENT;
    if(count > UINT16_MAX) return MC_CAPACITY_EXCEEDED;
    McStatus status = mc_buffer_write_u16_be(output, (uint16_t)count);
    if(status != MC_OK) return status;
    for(size_t i = 0; i < count; i++){
        if(tags[i].key == NULL || tags[i].value == NULL) return MC_INVALID_ARGUMENT;
        status = mc_save_write_utf(output, tags[i].key);
        if(status != MC_OK) return status;
        status = mc_save_write_utf(output, tags[i].value);
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

void mc_save_tags_destroy(McSaveTags *tags){
    if(tags == NULL) return;
    for(size_t i = 0; i < tags->count; i++){
        free(tags->keys[i]);
        free(tags->values[i]);
    }
    free(tags->keys);
    free(tags->values);
    *tags = (McSaveTags){0};
}

McStatus mc_save_read_string_map(McBuffer *input, McSaveTags *tags){
    if(input == NULL || tags == NULL) return MC_INVALID_ARGUMENT;
    *tags = (McSaveTags){0};
    uint16_t count = 0;
    if(mc_buffer_read_u16_be(input, &count) != MC_OK) return MC_FORMAT_ERROR;
    if(count == 0) return MC_OK;

    tags->keys = calloc(count, sizeof(*tags->keys));
    tags->values = calloc(count, sizeof(*tags->values));
    if(tags->keys == NULL || tags->values == NULL){
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

McStatus mc_save_write_content_header(McBuffer *output, const McContentGroupView *groups, size_t count){
    if(output == NULL || (groups == NULL && count != 0)) return MC_INVALID_ARGUMENT;
    if(count > UINT8_MAX) return MC_CAPACITY_EXCEEDED;
    uint8_t previous_type = 0;
    bool have_previous = false;
    McStatus status = mc_buffer_write_u8(output, (uint8_t)count);
    if(status != MC_OK) return status;
    for(size_t i = 0; i < count; i++){
        const McContentGroupView *group = &groups[i];
        if(group->type >= MC_CONTENT_TYPE_COUNT || group->count > UINT16_MAX ||
           (group->names == NULL && group->count != 0) || group->count == 0 ||
           (have_previous && group->type <= previous_type)) return MC_INVALID_ARGUMENT;
        previous_type = group->type;
        have_previous = true;
        status = mc_buffer_write_u8(output, group->type);
        if(status != MC_OK) return status;
        status = mc_buffer_write_u16_be(output, (uint16_t)group->count);
        if(status != MC_OK) return status;
        for(size_t j = 0; j < group->count; j++){
            if(group->names[j] == NULL) return MC_INVALID_ARGUMENT;
            status = mc_save_write_utf(output, group->names[j]);
            if(status != MC_OK) return status;
        }
    }
    return MC_OK;
}

void mc_content_header_destroy(McContentHeader *header){
    if(header == NULL) return;
    for(size_t i = 0; i < header->count; i++){
        for(size_t j = 0; j < header->groups[i].count; j++) free(header->groups[i].names[j]);
        free(header->groups[i].names);
    }
    free(header->groups);
    *header = (McContentHeader){0};
}

McStatus mc_save_read_content_header(McBuffer *input, McContentHeader *header){
    if(input == NULL || header == NULL) return MC_INVALID_ARGUMENT;
    *header = (McContentHeader){0};
    uint8_t group_count = 0;
    if(mc_buffer_read_u8(input, &group_count) != MC_OK) return MC_FORMAT_ERROR;
    if(group_count == 0) return MC_OK;

    header->groups = calloc(group_count, sizeof(*header->groups));
    if(header->groups == NULL) return MC_OUT_OF_MEMORY;
    header->count = group_count;
    uint8_t previous_type = 0;
    bool have_previous = false;
    for(size_t i = 0; i < group_count; i++){
        McContentGroup *group = &header->groups[i];
        uint16_t name_count = 0;
        if(mc_buffer_read_u8(input, &group->type) != MC_OK ||
           mc_buffer_read_u16_be(input, &name_count) != MC_OK || group->type >= MC_CONTENT_TYPE_COUNT ||
           (have_previous && group->type <= previous_type) || name_count == 0){
            mc_content_header_destroy(header);
            return MC_FORMAT_ERROR;
        }
        previous_type = group->type;
        have_previous = true;
        group->count = name_count;
        group->names = calloc(name_count, sizeof(*group->names));
        if(group->names == NULL){
            mc_content_header_destroy(header);
            return MC_OUT_OF_MEMORY;
        }
        for(size_t j = 0; j < name_count; j++){
            McStatus status = mc_save_read_utf(input, &group->names[j]);
            if(status != MC_OK){
                mc_content_header_destroy(header);
                return status;
            }
        }
    }
    return MC_OK;
}

int32_t mc_content_header_find(const McContentHeader *header, uint8_t type, const char *name){
    if(header == NULL || name == NULL) return -1;
    for(size_t i = 0; i < header->count; i++){
        const McContentGroup *group = &header->groups[i];
        if(group->type != type) continue;
        for(size_t j = 0; j < group->count; j++){
            if(strcmp(group->names[j], name) == 0) return (int32_t)j;
        }
        return -1;
    }
    return -1;
}
