#include "mindustry_deflate.h"

#include <string.h>

#define MC_HUFF_MAX_BITS 15
#define MC_HUFF_MAX_NODES 600

typedef struct McBitReader{
    const uint8_t *data;
    size_t size;
    size_t bit_position;
} McBitReader;

typedef struct McHuffman{
    int16_t child[2][MC_HUFF_MAX_NODES];
    int16_t symbol[MC_HUFF_MAX_NODES];
    uint16_t nodes;
} McHuffman;

static McStatus read_bits(McBitReader *reader, unsigned count, uint32_t *value){
    if(reader == NULL || value == NULL || count > 24 || reader->bit_position > reader->size * 8u ||
       count > reader->size * 8u - reader->bit_position) return MC_FORMAT_ERROR;
    uint32_t result = 0;
    for(unsigned i = 0; i < count; i++){
        size_t bit = reader->bit_position++;
        uint32_t bit_value = ((uint32_t)reader->data[bit >> 3] >> (bit & 7u)) & 1u;
        result |= bit_value << i;
    }
    *value = result;
    return MC_OK;
}

static void align_bits(McBitReader *reader){
    reader->bit_position = (reader->bit_position + 7u) & ~((size_t)7u);
}

static void huffman_init(McHuffman *tree){
    tree->nodes = 1;
    for(size_t i = 0; i < MC_HUFF_MAX_NODES; i++){
        tree->child[0][i] = -1;
        tree->child[1][i] = -1;
        tree->symbol[i] = -1;
    }
}

static McStatus huffman_build(McHuffman *tree, const uint8_t *lengths, size_t count){
    if(tree == NULL || lengths == NULL || count == 0 || count > 288) return MC_INVALID_ARGUMENT;
    huffman_init(tree);
    uint16_t length_count[MC_HUFF_MAX_BITS + 1] = {0};
    for(size_t i = 0; i < count; i++){
        if(lengths[i] > MC_HUFF_MAX_BITS) return MC_FORMAT_ERROR;
        if(lengths[i] != 0) length_count[lengths[i]]++;
    }

    uint16_t next_code[MC_HUFF_MAX_BITS + 1] = {0};
    uint32_t code = 0;
    for(unsigned bits = 1; bits <= MC_HUFF_MAX_BITS; bits++){
        code = (code + length_count[bits - 1]) << 1;
        next_code[bits] = (uint16_t)code;
        if(code + length_count[bits] > (UINT32_C(1) << bits)) return MC_FORMAT_ERROR;
    }

    for(size_t symbol = 0; symbol < count; symbol++){
        unsigned length = lengths[symbol];
        if(length == 0) continue;
        uint32_t canonical = next_code[length]++;
        int16_t node = 0;
        for(unsigned bit = 0; bit < length; bit++){
            /* Deflate's Huffman bit stream carries the canonical code MSB first. */
            unsigned direction = (canonical >> (length - bit - 1u)) & 1u;
            int16_t next = tree->child[direction][node];
            if(next < 0){
                if(tree->nodes >= MC_HUFF_MAX_NODES) return MC_CAPACITY_EXCEEDED;
                next = (int16_t)tree->nodes++;
                tree->child[direction][node] = next;
                tree->child[0][next] = -1;
                tree->child[1][next] = -1;
                tree->symbol[next] = -1;
            }
            node = next;
            if(bit + 1u < length && tree->symbol[node] >= 0) return MC_FORMAT_ERROR;
        }
        if(tree->symbol[node] >= 0 || tree->child[0][node] >= 0 || tree->child[1][node] >= 0){
            return MC_FORMAT_ERROR;
        }
        tree->symbol[node] = (int16_t)symbol;
    }
    return tree->symbol[0] >= 0 ? MC_FORMAT_ERROR : MC_OK;
}

static McStatus huffman_decode(McBitReader *reader, const McHuffman *tree, uint16_t *symbol){
    int16_t node = 0;
    for(unsigned depth = 0; depth <= MC_HUFF_MAX_BITS; depth++){
        if(tree->symbol[node] >= 0){
            *symbol = (uint16_t)tree->symbol[node];
            return MC_OK;
        }
        uint32_t bit = 0;
        if(read_bits(reader, 1, &bit) != MC_OK || tree->child[bit][node] < 0) return MC_FORMAT_ERROR;
        node = tree->child[bit][node];
    }
    return MC_FORMAT_ERROR;
}

static McStatus build_fixed_trees(McHuffman *literal, McHuffman *distance){
    uint8_t literal_lengths[288];
    uint8_t distance_lengths[32];
    for(size_t i = 0; i < 288; i++){
        literal_lengths[i] = i <= 143 ? 8 : i <= 255 ? 9 : i <= 279 ? 7 : 8;
    }
    for(size_t i = 0; i < 32; i++) distance_lengths[i] = 5;
    McStatus status = huffman_build(literal, literal_lengths, 288);
    if(status != MC_OK) return status;
    return huffman_build(distance, distance_lengths, 32);
}

static McStatus build_dynamic_trees(McBitReader *reader, McHuffman *literal, McHuffman *distance){
    uint32_t value = 0;
    if(read_bits(reader, 5, &value) != MC_OK) return MC_FORMAT_ERROR;
    size_t literal_count = value + 257;
    if(read_bits(reader, 5, &value) != MC_OK) return MC_FORMAT_ERROR;
    size_t distance_count = value + 1;
    if(read_bits(reader, 4, &value) != MC_OK) return MC_FORMAT_ERROR;
    size_t code_length_count = value + 4;

    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t code_length_lengths[19] = {0};
    for(size_t i = 0; i < code_length_count; i++){
        if(read_bits(reader, 3, &value) != MC_OK) return MC_FORMAT_ERROR;
        code_length_lengths[order[i]] = (uint8_t)value;
    }
    McHuffman code_length_tree;
    if(huffman_build(&code_length_tree, code_length_lengths, 19) != MC_OK) return MC_FORMAT_ERROR;

    uint8_t lengths[288 + 32] = {0};
    size_t total = literal_count + distance_count;
    size_t position = 0;
    while(position < total){
        uint16_t symbol = 0;
        if(huffman_decode(reader, &code_length_tree, &symbol) != MC_OK) return MC_FORMAT_ERROR;
        if(symbol <= 15){
            lengths[position++] = (uint8_t)symbol;
        }else if(symbol == 16){
            if(position == 0 || read_bits(reader, 2, &value) != MC_OK) return MC_FORMAT_ERROR;
            size_t repeat = value + 3;
            if(repeat > total - position) return MC_FORMAT_ERROR;
            uint8_t previous = lengths[position - 1];
            for(size_t i = 0; i < repeat; i++) lengths[position++] = previous;
        }else if(symbol == 17){
            if(read_bits(reader, 3, &value) != MC_OK) return MC_FORMAT_ERROR;
            size_t repeat = value + 3;
            if(repeat > total - position) return MC_FORMAT_ERROR;
            for(size_t i = 0; i < repeat; i++) lengths[position++] = 0;
        }else if(symbol == 18){
            if(read_bits(reader, 7, &value) != MC_OK) return MC_FORMAT_ERROR;
            size_t repeat = value + 11;
            if(repeat > total - position) return MC_FORMAT_ERROR;
            for(size_t i = 0; i < repeat; i++) lengths[position++] = 0;
        }else{
            return MC_FORMAT_ERROR;
        }
    }
    if(lengths[256] == 0) return MC_FORMAT_ERROR;
    if(huffman_build(literal, lengths, literal_count) != MC_OK) return MC_FORMAT_ERROR;
    if(huffman_build(distance, lengths + literal_count, distance_count) != MC_OK) return MC_FORMAT_ERROR;
    return MC_OK;
}

static const uint16_t length_base[29] = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
};
static const uint8_t length_extra[29] = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
    2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
};
static const uint16_t distance_base[30] = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129,
    193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097,
    6145, 8193, 12289, 16385, 24577
};
static const uint8_t distance_extra[30] = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
    6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
};

static McStatus decode_huffman_block(McBitReader *reader, const McHuffman *literal,
    const McHuffman *distance, McBuffer *output){
    for(;;){
        uint16_t symbol = 0;
        if(huffman_decode(reader, literal, &symbol) != MC_OK) return MC_FORMAT_ERROR;
        if(symbol <= 255){
            if(mc_buffer_write_u8(output, (uint8_t)symbol) != MC_OK) return MC_OUT_OF_MEMORY;
            continue;
        }
        if(symbol == 256) return MC_OK;
        if(symbol < 257 || symbol > 285) return MC_FORMAT_ERROR;

        size_t length_index = symbol - 257;
        uint32_t extra = 0;
        if(read_bits(reader, length_extra[length_index], &extra) != MC_OK) return MC_FORMAT_ERROR;
        size_t length = length_base[length_index] + extra;

        uint16_t distance_symbol = 0;
        if(huffman_decode(reader, distance, &distance_symbol) != MC_OK || distance_symbol >= 30){
            return MC_FORMAT_ERROR;
        }
        extra = 0;
        if(read_bits(reader, distance_extra[distance_symbol], &extra) != MC_OK) return MC_FORMAT_ERROR;
        size_t copy_distance = distance_base[distance_symbol] + extra;
        if(copy_distance == 0 || copy_distance > output->size) return MC_FORMAT_ERROR;
        for(size_t i = 0; i < length; i++){
            uint8_t value = output->data[output->size - copy_distance];
            if(mc_buffer_write_u8(output, value) != MC_OK) return MC_OUT_OF_MEMORY;
        }
    }
}

static uint32_t adler32(const uint8_t *data, size_t size){
    uint32_t first = 1;
    uint32_t second = 0;
    for(size_t i = 0; i < size; i++){
        first = (first + data[i]) % 65521u;
        second = (second + first) % 65521u;
    }
    return (second << 16) | first;
}

McStatus mc_zlib_compress_stored(const uint8_t *data, size_t size, McBuffer *output){
    if(output == NULL || (data == NULL && size != 0)) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    /* CM=8, CINFO=7, FCHECK chosen so the header is divisible by 31. */
    if(mc_buffer_write_u8(output, 0x78) != MC_OK || mc_buffer_write_u8(output, 0x01) != MC_OK){
        return MC_OUT_OF_MEMORY;
    }
    size_t position = 0;
    do{
        size_t remaining = size - position;
        uint16_t count = (uint16_t)(remaining > UINT16_MAX ? UINT16_MAX : remaining);
        bool final = position + count == size;
        if(mc_buffer_write_u8(output, final ? 1 : 0) != MC_OK ||
           mc_buffer_write_u8(output, (uint8_t)(count & 0xffu)) != MC_OK ||
           mc_buffer_write_u8(output, (uint8_t)(count >> 8)) != MC_OK ||
           mc_buffer_write_u8(output, (uint8_t)(~count & 0xffu)) != MC_OK ||
           mc_buffer_write_u8(output, (uint8_t)((uint16_t)~count >> 8)) != MC_OK ||
           mc_buffer_write_bytes(output, count == 0 ? (const uint8_t *)"" : data + position, count) != MC_OK) return MC_OUT_OF_MEMORY;
        position += count;
    }while(position < size);
    return mc_buffer_write_u32_be(output, adler32(data, size));
}

McStatus mc_zlib_decompress(const uint8_t *data, size_t size, McBuffer *output){
    if(output == NULL || data == NULL || size < 6) return MC_INVALID_ARGUMENT;
    uint8_t cmf = data[0], flg = data[1];
    if((cmf & 0x0fu) != 8 || (cmf >> 4) > 7 || ((uint16_t)cmf << 8 | flg) % 31 != 0 ||
       (flg & 0x20u) != 0) return MC_FORMAT_ERROR;

    McBitReader reader = {.data = data + 2, .size = size - 6, .bit_position = 0};
    mc_buffer_clear(output);
    bool final = false;
    while(!final){
        uint32_t bit = 0, type = 0;
        if(read_bits(&reader, 1, &bit) != MC_OK || read_bits(&reader, 2, &type) != MC_OK) return MC_FORMAT_ERROR;
        final = bit != 0;
        if(type == 0){
            align_bits(&reader);
            uint32_t length = 0, inverse = 0;
            if(read_bits(&reader, 16, &length) != MC_OK || read_bits(&reader, 16, &inverse) != MC_OK ||
               (uint16_t)length != (uint16_t)~inverse) return MC_FORMAT_ERROR;
            for(uint32_t i = 0; i < length; i++){
                uint32_t byte = 0;
                if(read_bits(&reader, 8, &byte) != MC_OK || mc_buffer_write_u8(output, (uint8_t)byte) != MC_OK){
                    return MC_FORMAT_ERROR;
                }
            }
        }else if(type == 1 || type == 2){
            McHuffman literal, distance;
            McStatus status = type == 1 ? build_fixed_trees(&literal, &distance) :
                build_dynamic_trees(&reader, &literal, &distance);
            if(status != MC_OK || decode_huffman_block(&reader, &literal, &distance, output) != MC_OK){
                return MC_FORMAT_ERROR;
            }
        }else{
            return MC_FORMAT_ERROR;
        }
    }

    align_bits(&reader);
    if(reader.bit_position != reader.size * 8u) return MC_FORMAT_ERROR;
    const uint8_t *checksum = data + size - 4;
    uint32_t expected = ((uint32_t)checksum[0] << 24) | ((uint32_t)checksum[1] << 16) |
        ((uint32_t)checksum[2] << 8) | checksum[3];
    if(adler32(output->data, output->size) != expected) return MC_FORMAT_ERROR;
    return MC_OK;
}
