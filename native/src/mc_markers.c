#include "mindustry_markers.h"

#include <stdlib.h>
#include <string.h>

#define MC_UBJSON_MAX_VALUES ((size_t)1000000)

static void value_destroy(McUbjsonValue *value){
    if(value == NULL) return;
    for(size_t i = 0; i < value->count; i++){
        free(value->keys == NULL ? NULL : value->keys[i]);
        value_destroy(value->values[i]);
        free(value->values[i]);
    }
    free(value->keys);
    free(value->values);
    free(value->string);
    *value = (McUbjsonValue){0};
}

void mc_markers_destroy(McMarkers *markers){
    if(markers == NULL) return;
    value_destroy(&markers->root);
    *markers = (McMarkers){0};
}

void mc_markers_init_empty(McMarkers *markers){
    if(markers == NULL) return;
    mc_markers_destroy(markers);
    markers->root.kind = MC_UBJSON_OBJECT;
}

static McStatus read_u64(McBuffer *input, uint64_t *value){
    if(input->position > input->size || input->size - input->position < 8) return MC_FORMAT_ERROR;
    uint64_t result = 0;
    for(size_t i = 0; i < 8; i++) result = (result << 8) | input->data[input->position++];
    *value = result;
    return MC_OK;
}

static McStatus write_u64(McBuffer *output, uint64_t value){
    McStatus status = MC_OK;
    for(int i = 7; i >= 0 && status == MC_OK; i--) status = mc_buffer_write_u8(output, (uint8_t)(value >> (i * 8)));
    return status;
}

static McStatus read_length_marker(McBuffer *input, size_t *length){
    uint8_t marker = 0;
    if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
    uint64_t value = 0;
    switch(marker){
        case 'i':
            if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
            value = marker;
            break;
        case 'I': {
            uint16_t short_value = 0;
            if(mc_buffer_read_u16_be(input, &short_value) != MC_OK) return MC_FORMAT_ERROR;
            value = short_value;
            break;
        }
        case 'l': {
            uint32_t int_value = 0;
            if(mc_buffer_read_u32_be(input, &int_value) != MC_OK) return MC_FORMAT_ERROR;
            value = int_value;
            break;
        }
        case 'L':
            if(read_u64(input, &value) != MC_OK || value > SIZE_MAX) return MC_FORMAT_ERROR;
            break;
        default:
            return MC_FORMAT_ERROR;
    }
    if(value > SIZE_MAX || value > MC_UBJSON_MAX_VALUES * 1024u) return MC_FORMAT_ERROR;
    *length = (size_t)value;
    return MC_OK;
}

static McStatus read_key(McBuffer *input, char **key){
    size_t size = 0;
    if(read_length_marker(input, &size) != MC_OK || size > input->size - input->position) return MC_FORMAT_ERROR;
    char *result = malloc(size + 1);
    if(result == NULL) return MC_OUT_OF_MEMORY;
    if(mc_buffer_read_bytes(input, result, size) != MC_OK){
        free(result);
        return MC_FORMAT_ERROR;
    }
    result[size] = '\0';
    *key = result;
    return MC_OK;
}

static McUbjsonValue *value_new(void){
    return calloc(1, sizeof(McUbjsonValue));
}

static McStatus append_child(McUbjsonValue *parent, char *key, McUbjsonValue *child){
    if(parent->count >= MC_UBJSON_MAX_VALUES) return MC_FORMAT_ERROR;
    McUbjsonValue **values = realloc(parent->values, (parent->count + 1) * sizeof(*values));
    if(values == NULL) return MC_OUT_OF_MEMORY;
    parent->values = values;
    char **keys = parent->keys;
    if(parent->kind == MC_UBJSON_OBJECT){
        keys = realloc(parent->keys, (parent->count + 1) * sizeof(*keys));
        if(keys == NULL) return MC_OUT_OF_MEMORY;
        parent->keys = keys;
        parent->keys[parent->count] = key;
    }else{
        free(key);
    }
    parent->values[parent->count++] = child;
    return MC_OK;
}

static McStatus read_value(McBuffer *input, uint8_t marker, McUbjsonValue **result);

static McStatus read_count_value(McBuffer *input, size_t *count){
    uint8_t marker = 0;
    if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
    McUbjsonValue *value = NULL;
    McStatus status = read_value(input, marker, &value);
    if(status != MC_OK) return status;
    bool integer = value->kind >= MC_UBJSON_UINT8 && value->kind <= MC_UBJSON_INT64;
    bool negative = (value->kind == MC_UBJSON_INT8 && (int8_t)value->integer < 0) ||
                    (value->kind == MC_UBJSON_INT16 && (int16_t)value->integer < 0) ||
                    (value->kind == MC_UBJSON_INT32 && (int32_t)value->integer < 0) ||
                    (value->kind == MC_UBJSON_INT64 && (int64_t)value->integer < 0);
    if(!integer || negative || value->integer > SIZE_MAX){
        value_destroy(value);
        free(value);
        return MC_FORMAT_ERROR;
    }
    *count = (size_t)value->integer;
    value_destroy(value);
    free(value);
    return *count > MC_UBJSON_MAX_VALUES ? MC_FORMAT_ERROR : MC_OK;
}

static McStatus read_array(McBuffer *input, McUbjsonValue *array){
    uint8_t marker = 0;
    if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
    uint8_t forced = 0;
    if(marker == '$'){
        if(mc_buffer_read_u8(input, &forced) != MC_OK || mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
    }
    if(marker == '#'){
        size_t count = 0;
        if(read_count_value(input, &count) != MC_OK) return MC_FORMAT_ERROR;
        for(size_t i = 0; i < count; i++){
            uint8_t value_marker = forced;
            if(value_marker == 0 && mc_buffer_read_u8(input, &value_marker) != MC_OK) return MC_FORMAT_ERROR;
            McUbjsonValue *child = NULL;
            McStatus status = read_value(input, value_marker, &child);
            if(status != MC_OK) return status;
            status = append_child(array, NULL, child);
            if(status != MC_OK){ value_destroy(child); free(child); return status; }
        }
        /* Arc's optimized writer intentionally omits the closing ']'. */
        return MC_OK;
    }
    if(marker == ']' ) return MC_OK;
    for(;;){
        uint8_t value_marker = forced == 0 ? marker : forced;
        McUbjsonValue *child = NULL;
        McStatus status = read_value(input, value_marker, &child);
        if(status != MC_OK) return status;
        status = append_child(array, NULL, child);
        if(status != MC_OK){ value_destroy(child); free(child); return status; }
        if(forced == 0){
            if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
            if(marker == ']') return MC_OK;
        }else{
            if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
            if(marker == ']') return MC_OK;
            /* Typed arrays without a count are accepted as a convenience. */
        }
    }
}

static McStatus read_object(McBuffer *input, McUbjsonValue *object){
    uint8_t marker = 0;
    if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
    uint8_t forced = 0;
    size_t expected = SIZE_MAX;
    if(marker == '$'){
        if(mc_buffer_read_u8(input, &forced) != MC_OK || mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
    }
    if(marker == '#'){
        if(read_count_value(input, &expected) != MC_OK) return MC_FORMAT_ERROR;
        if(mc_buffer_read_u8(input, &marker) != MC_OK && expected != 0) return MC_FORMAT_ERROR;
    }
    if(expected == 0) return MC_OK;
    for(size_t i = 0; i < expected; i++){
        if(marker == '}') return expected == SIZE_MAX ? MC_OK : MC_FORMAT_ERROR;
        char *key = NULL;
        /* marker is the key-length marker already consumed above. */
        if(input->position == 0) return MC_FORMAT_ERROR;
        input->position--;
        if(read_key(input, &key) != MC_OK) return MC_FORMAT_ERROR;
        uint8_t value_marker = forced;
        if(value_marker == 0 && mc_buffer_read_u8(input, &value_marker) != MC_OK){ free(key); return MC_FORMAT_ERROR; }
        McUbjsonValue *child = NULL;
        McStatus status = read_value(input, value_marker, &child);
        if(status != MC_OK){ free(key); return status; }
        status = append_child(object, key, child);
        if(status != MC_OK){ free(key); value_destroy(child); free(child); return status; }
        if(expected == SIZE_MAX){
            if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
            if(marker == '}') return MC_OK;
        }else if(i + 1 < expected){
            if(mc_buffer_read_u8(input, &marker) != MC_OK) return MC_FORMAT_ERROR;
        }
    }
    return MC_OK;
}

static McStatus read_string_value(McBuffer *input, McUbjsonValue *value){
    size_t size = 0;
    if(read_length_marker(input, &size) != MC_OK || size > input->size - input->position) return MC_FORMAT_ERROR;
    value->string = malloc(size + 1);
    if(value->string == NULL) return MC_OUT_OF_MEMORY;
    if(mc_buffer_read_bytes(input, value->string, size) != MC_OK) return MC_FORMAT_ERROR;
    value->string[size] = '\0';
    return MC_OK;
}

static McStatus read_value(McBuffer *input, uint8_t marker, McUbjsonValue **result){
    McUbjsonValue *value = value_new();
    if(value == NULL) return MC_OUT_OF_MEMORY;
    McStatus status = MC_OK;
    switch(marker){
        case 'Z': value->kind = MC_UBJSON_NULL; break;
        case 'T': value->kind = MC_UBJSON_TRUE; break;
        case 'F': value->kind = MC_UBJSON_FALSE; break;
        case 'B': { uint8_t v = 0; value->kind = MC_UBJSON_UINT8; status = mc_buffer_read_u8(input, &v); value->integer = v; } break;
        case 'U': value->kind = MC_UBJSON_UINT16; { uint16_t v = 0; status = mc_buffer_read_u16_be(input, &v); value->integer = v; } break;
        case 'i': value->kind = MC_UBJSON_INT8; { uint8_t v = 0; status = mc_buffer_read_u8(input, &v); value->integer = (uint64_t)(int64_t)(int8_t)v; } break;
        case 'I': value->kind = MC_UBJSON_INT16; { uint16_t v = 0; status = mc_buffer_read_u16_be(input, &v); value->integer = (uint64_t)(int64_t)(int16_t)v; } break;
        case 'l': value->kind = MC_UBJSON_INT32; { uint32_t v = 0; status = mc_buffer_read_u32_be(input, &v); value->integer = (uint64_t)(int64_t)(int32_t)v; } break;
        case 'L': value->kind = MC_UBJSON_INT64; { uint64_t v = 0; status = read_u64(input, &v); value->integer = v; } break;
        case 'd': { uint32_t bits = 0; value->kind = MC_UBJSON_FLOAT32; status = mc_buffer_read_u32_be(input, &bits); float f = 0.0f; memcpy(&f, &bits, sizeof(f)); value->real = f; } break;
        case 'D': { uint64_t bits = 0; value->kind = MC_UBJSON_FLOAT64; status = read_u64(input, &bits); memcpy(&value->real, &bits, sizeof(value->real)); } break;
        case 'C': { uint16_t v = 0; value->kind = MC_UBJSON_CHAR; status = mc_buffer_read_u16_be(input, &v); value->integer = v; } break;
        case 's': value->kind = MC_UBJSON_STRING; { uint8_t size = 0; status = mc_buffer_read_u8(input, &size); if(status == MC_OK && size > input->size - input->position) status = MC_FORMAT_ERROR; if(status == MC_OK){ value->string = malloc((size_t)size + 1); if(value->string == NULL) status = MC_OUT_OF_MEMORY; else { status = mc_buffer_read_bytes(input, value->string, size); value->string[size] = '\0'; } } } break;
        case 'S': value->kind = MC_UBJSON_STRING; status = read_string_value(input, value); break;
        case '[': value->kind = MC_UBJSON_ARRAY; status = read_array(input, value); break;
        case '{': value->kind = MC_UBJSON_OBJECT; status = read_object(input, value); break;
        case 'a': case 'A': {
            value->kind = MC_UBJSON_ARRAY;
            uint8_t item = 0;
            if(mc_buffer_read_u8(input, &item) != MC_OK) status = MC_FORMAT_ERROR;
            size_t count = 0;
            if(status == MC_OK){
                if(marker == 'a'){ uint8_t n = 0; status = mc_buffer_read_u8(input, &n); count = n; }
                else { uint32_t n = 0; status = mc_buffer_read_u32_be(input, &n); count = n; }
            }
            for(size_t i = 0; status == MC_OK && i < count; i++){
                McUbjsonValue *child = NULL;
                status = read_value(input, item, &child);
                if(status == MC_OK) status = append_child(value, NULL, child);
                if(status != MC_OK && child != NULL){ value_destroy(child); free(child); }
            }
        } break;
        default: status = MC_FORMAT_ERROR; break;
    }
    if(status != MC_OK){ value_destroy(value); free(value); return status; }
    *result = value;
    return MC_OK;
}

McStatus mc_markers_read(const uint8_t *data, size_t size, McMarkers *markers){
    if(data == NULL || markers == NULL || size == 0) return MC_INVALID_ARGUMENT;
    mc_markers_destroy(markers);
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint8_t marker = 0;
    if(mc_buffer_read_u8(&input, &marker) != MC_OK || marker != '{') return MC_FORMAT_ERROR;
    McUbjsonValue *root = NULL;
    McStatus status = read_value(&input, marker, &root);
    if(status != MC_OK || input.position != input.size){
        if(root != NULL){ value_destroy(root); free(root); }
        return status == MC_OK ? MC_FORMAT_ERROR : status;
    }
    markers->root = *root;
    free(root);
    return MC_OK;
}

static McStatus write_length(McBuffer *output, size_t size){
    if(size <= UINT8_MAX){
        McStatus status = mc_buffer_write_u8(output, 'i');
        if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)size);
        return status;
    }
    if(size <= UINT16_MAX){
        McStatus status = mc_buffer_write_u8(output, 'I');
        if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)size);
        return status;
    }
    if(size <= UINT32_MAX){
        McStatus status = mc_buffer_write_u8(output, 'l');
        if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)size);
        return status;
    }
    return MC_CAPACITY_EXCEEDED;
}

static McStatus write_key(McBuffer *output, const char *key){
    size_t size = strlen(key);
    McStatus status = write_length(output, size);
    if(status == MC_OK) status = mc_buffer_write_bytes(output, key, size);
    return status;
}

static McStatus write_value(McBuffer *output, const McUbjsonValue *value){
    if(value == NULL) return MC_INVALID_ARGUMENT;
    McStatus status = MC_OK;
    switch(value->kind){
        case MC_UBJSON_NULL: return mc_buffer_write_u8(output, 'Z');
        case MC_UBJSON_TRUE: return mc_buffer_write_u8(output, 'T');
        case MC_UBJSON_FALSE: return mc_buffer_write_u8(output, 'F');
        case MC_UBJSON_UINT8: status = mc_buffer_write_u8(output, 'B'); if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)value->integer); return status;
        case MC_UBJSON_UINT16: status = mc_buffer_write_u8(output, 'U'); if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)value->integer); return status;
        case MC_UBJSON_INT8: status = mc_buffer_write_u8(output, 'i'); if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)(int8_t)value->integer); return status;
        case MC_UBJSON_INT16: status = mc_buffer_write_u8(output, 'I'); if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)(int16_t)value->integer); return status;
        case MC_UBJSON_INT32: status = mc_buffer_write_u8(output, 'l'); if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)(int32_t)value->integer); return status;
        case MC_UBJSON_INT64: status = mc_buffer_write_u8(output, 'L'); if(status == MC_OK) status = write_u64(output, value->integer); return status;
        case MC_UBJSON_FLOAT32: { float f = (float)value->real; uint32_t bits = 0; memcpy(&bits, &f, sizeof(bits)); status = mc_buffer_write_u8(output, 'd'); if(status == MC_OK) status = mc_buffer_write_u32_be(output, bits); return status; }
        case MC_UBJSON_FLOAT64: { uint64_t bits = 0; memcpy(&bits, &value->real, sizeof(bits)); status = mc_buffer_write_u8(output, 'D'); if(status == MC_OK) status = write_u64(output, bits); return status; }
        case MC_UBJSON_CHAR: status = mc_buffer_write_u8(output, 'C'); if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)value->integer); return status;
        case MC_UBJSON_STRING: status = mc_buffer_write_u8(output, 'S'); if(status == MC_OK) status = write_length(output, value->string == NULL ? 0 : strlen(value->string)); if(status == MC_OK) status = mc_buffer_write_bytes(output, value->string, value->string == NULL ? 0 : strlen(value->string)); return status;
        case MC_UBJSON_ARRAY:
            status = mc_buffer_write_u8(output, '[');
            for(size_t i = 0; status == MC_OK && i < value->count; i++) status = write_value(output, value->values[i]);
            if(status == MC_OK) status = mc_buffer_write_u8(output, ']');
            return status;
        case MC_UBJSON_OBJECT:
            status = mc_buffer_write_u8(output, '{');
            for(size_t i = 0; status == MC_OK && i < value->count; i++){
                if(value->keys == NULL || value->keys[i] == NULL) return MC_INVALID_ARGUMENT;
                status = write_key(output, value->keys[i]);
                if(status == MC_OK) status = write_value(output, value->values[i]);
            }
            if(status == MC_OK) status = mc_buffer_write_u8(output, '}');
            return status;
        default: return MC_INVALID_ARGUMENT;
    }
}

McStatus mc_markers_write(const McMarkers *markers, McBuffer *output){
    if(markers == NULL || output == NULL || markers->root.kind != MC_UBJSON_OBJECT) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    return write_value(output, &markers->root);
}
