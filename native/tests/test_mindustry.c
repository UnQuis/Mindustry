#include "mindustry.h"
#include "mindustry_format.h"
#include "mindustry_save.h"
#include "mindustry_deflate.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_world_bounds_and_clear(void){
    McWorld world = {0};
    assert(mc_world_init(&world, 4, 3) == MC_OK);
    assert(world.tiles != NULL);
    assert(mc_world_tile(&world, 3, 2)->floor == MC_FLOOR_STONE);
    assert(mc_world_tile(&world, 4, 2) == NULL);
    mc_world_clear(&world, MC_FLOOR_SAND);
    assert(mc_world_tile_const(&world, 0, 0)->floor == MC_FLOOR_SAND);
    mc_world_destroy(&world);
    assert(world.tiles == NULL);
}

static void test_java_map_section_codec(void){
    McWorld world = {0};
    assert(mc_world_init(&world, 3, 2) == MC_OK);

    /* Six tiles are deliberately arranged into three floor and two block runs. */
    world.tiles[0].floor = MC_FLOOR_STONE;
    world.tiles[1].floor = MC_FLOOR_STONE;
    world.tiles[2].floor = MC_FLOOR_SAND;
    world.tiles[3].floor = MC_FLOOR_SAND;
    world.tiles[4].floor = MC_FLOOR_SAND;
    world.tiles[5].floor = MC_FLOOR_STONE;
    world.tiles[4].block = MC_BLOCK_DUO;
    world.tiles[5].block = MC_BLOCK_DUO;

    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_map_write_section(&world, &encoded) == MC_OK);
    static const uint8_t expected[] = {
        0x00, 0x03, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x01,
        0x00, 0x01, 0x00, 0x00, 0x02,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x03,
        0x00, 0x04, 0x00, 0x01
    };
    assert(encoded.size == sizeof(expected));
    assert(memcmp(encoded.data, expected, sizeof(expected)) == 0);

    McWorld decoded = {0};
    assert(mc_map_read_section(&decoded, encoded.data, encoded.size) == MC_OK);
    assert(decoded.width == 3 && decoded.height == 2);
    for(size_t i = 0; i < 6; i++){
        assert(decoded.tiles[i].floor == world.tiles[i].floor);
        assert(decoded.tiles[i].overlay == world.tiles[i].overlay);
        assert(decoded.tiles[i].block == world.tiles[i].block);
    }

    /* A decoder must reject truncation, trailing bytes and unsupported flags. */
    assert(mc_map_read_section(&decoded, encoded.data, encoded.size - 1) == MC_FORMAT_ERROR);
    uint8_t with_trailing_byte[sizeof(expected) + 1];
    memcpy(with_trailing_byte, expected, sizeof(expected));
    with_trailing_byte[sizeof(expected)] = 0;
    assert(mc_map_read_section(&decoded, with_trailing_byte, sizeof(with_trailing_byte)) == MC_FORMAT_ERROR);
    uint8_t with_entity_flag[sizeof(expected)];
    memcpy(with_entity_flag, expected, sizeof(expected));
    with_entity_flag[21] = 1;
    assert(mc_map_read_section(&decoded, with_entity_flag, sizeof(with_entity_flag)) == MC_FORMAT_ERROR);

    mc_world_destroy(&decoded);
    mc_buffer_destroy(&encoded);
    mc_world_destroy(&world);
}

static void test_java_save_container(void){
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_save_write_header(&encoded, 13) == MC_OK);

    static const McSaveTag tags[] = {
        {"mapname", "ground zero"},
        {"rocket", "\xF0\x9F\x9A\x80"}
    };
    assert(mc_save_write_string_map(&encoded, tags, 2) == MC_OK);
    static const uint8_t payload[] = {0x01, 0x02, 0x03, 0x04};
    assert(mc_save_write_region(&encoded, payload, sizeof(payload)) == MC_OK);

    encoded.position = 0;
    uint32_t version = 0;
    assert(mc_save_read_header(&encoded, &version) == MC_OK);
    assert(version == 13);
    McSaveTags decoded_tags = {0};
    assert(mc_save_read_string_map(&encoded, &decoded_tags) == MC_OK);
    assert(decoded_tags.count == 2);
    assert(strcmp(decoded_tags.keys[0], "mapname") == 0);
    assert(strcmp(decoded_tags.values[0], "ground zero") == 0);
    assert(strcmp(decoded_tags.keys[1], "rocket") == 0);
    assert(strcmp(decoded_tags.values[1], "\xF0\x9F\x9A\x80") == 0);
    const uint8_t *decoded_payload = NULL;
    size_t decoded_size = 0;
    assert(mc_save_read_region(&encoded, &decoded_payload, &decoded_size) == MC_OK);
    assert(decoded_size == sizeof(payload));
    assert(memcmp(decoded_payload, payload, sizeof(payload)) == 0);
    assert(encoded.position == encoded.size);
    mc_save_tags_destroy(&decoded_tags);

    /* Java modified UTF-8 encodes U+1F680 as two UTF-16 surrogate triplets. */
    McBuffer utf;
    mc_buffer_init(&utf);
    assert(mc_save_write_utf(&utf, "\xF0\x9F\x9A\x80") == MC_OK);
    static const uint8_t modified_rocket[] = {0x00, 0x06, 0xED, 0xA0, 0xBD, 0xED, 0xBA, 0x80};
    assert(utf.size == sizeof(modified_rocket));
    assert(memcmp(utf.data, modified_rocket, sizeof(modified_rocket)) == 0);
    utf.position = 0;
    char *decoded_rocket = NULL;
    assert(mc_save_read_utf(&utf, &decoded_rocket) == MC_OK);
    assert(strcmp(decoded_rocket, "\xF0\x9F\x9A\x80") == 0);
    free(decoded_rocket);
    mc_buffer_destroy(&utf);

    mc_buffer_destroy(&encoded);
}

static void test_zlib_deflate_wrapper(void){
    static const uint8_t dynamic_stream[] = {
        0x78, 0x9c, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0x57, 0xc8, 0x40,
        0x22, 0xcb, 0xf3, 0x8b, 0x72, 0x52, 0x14, 0x51, 0x84, 0x46,
        0x25, 0x86, 0x95, 0x04, 0x00, 0xf5, 0x4a, 0xb4, 0x65
    };
    const char phrase[] = "hello hello hello world! ";
    const size_t phrase_size = sizeof(phrase) - 1;
    uint8_t expected[sizeof(phrase) * 20 - 20];
    for(size_t i = 0; i < 20; i++) memcpy(expected + i * phrase_size, phrase, phrase_size);

    McBuffer decoded;
    mc_buffer_init(&decoded);
    assert(mc_zlib_decompress(dynamic_stream, sizeof(dynamic_stream), &decoded) == MC_OK);
    assert(decoded.size == sizeof(expected));
    assert(memcmp(decoded.data, expected, sizeof(expected)) == 0);

    static const uint8_t dynamic_huffman_stream[] = {
        0x78, 0x9c, 0xed, 0xc1, 0x01, 0x0d, 0x00, 0x00, 0x00, 0xc2,
        0xa0, 0xac, 0xef, 0x5f, 0xc2, 0x1c, 0x6e, 0x40, 0x01, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0xbf, 0x01,
        0x9f, 0xbb, 0xcd, 0xe3
    };
    uint8_t dynamic_expected[10000];
    memset(dynamic_expected, 'a', sizeof(dynamic_expected));
    mc_buffer_clear(&decoded);
    assert(mc_zlib_decompress(dynamic_huffman_stream, sizeof(dynamic_huffman_stream), &decoded) == MC_OK);
    assert(decoded.size == sizeof(dynamic_expected));
    assert(memcmp(decoded.data, dynamic_expected, sizeof(dynamic_expected)) == 0);

    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_zlib_compress_stored(expected, sizeof(expected), &encoded) == MC_OK);
    McBuffer round_trip;
    mc_buffer_init(&round_trip);
    assert(mc_zlib_decompress(encoded.data, encoded.size, &round_trip) == MC_OK);
    assert(round_trip.size == sizeof(expected));
    assert(memcmp(round_trip.data, expected, sizeof(expected)) == 0);

    static const uint8_t expected_empty[] = {0x78, 0x01, 0x01, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x00, 0x01};
    mc_buffer_clear(&encoded);
    assert(mc_zlib_compress_stored(NULL, 0, &encoded) == MC_OK);
    assert(encoded.size == sizeof(expected_empty));
    assert(memcmp(encoded.data, expected_empty, sizeof(expected_empty)) == 0);

    uint8_t damaged[sizeof(dynamic_stream)];
    memcpy(damaged, dynamic_stream, sizeof(damaged));
    damaged[sizeof(damaged) - 1] ^= 1;
    assert(mc_zlib_decompress(damaged, sizeof(damaged), &round_trip) == MC_FORMAT_ERROR);
    damaged[0] = 0;
    assert(mc_zlib_decompress(damaged, sizeof(damaged), &round_trip) == MC_FORMAT_ERROR);

    mc_buffer_destroy(&round_trip);
    mc_buffer_destroy(&encoded);
    mc_buffer_destroy(&decoded);
}

static void test_plain_map_save_reader(void){
    McWorld source = {0};
    assert(mc_world_init(&source, 3, 2) == MC_OK);
    source.tiles[2].floor = MC_FLOOR_SAND;
    source.tiles[4].block = MC_BLOCK_DUO;

    static const McSaveTag meta[] = {
        {"mapname", "native-fixture"},
        {"width", "3"},
        {"height", "2"}
    };
    static const char *blocks[] = {"air", "core-shard", "mechanical-drill", "conveyor", "duo"};
    static const McContentGroupView content[] = {{MC_CONTENT_BLOCK, blocks, 5}};

    McBuffer uncompressed;
    McBuffer region;
    mc_buffer_init(&uncompressed);
    mc_buffer_init(&region);
    assert(mc_save_write_header(&uncompressed, 13) == MC_OK);

    assert(mc_save_write_string_map(&region, meta, 3) == MC_OK);
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);
    mc_buffer_clear(&region);
    assert(mc_buffer_write_u32_be(&region, 2) == MC_OK); /* DataPatcher.patchFormatVersion. */
    assert(mc_buffer_write_u32_be(&region, 0) == MC_OK); /* No external data assets. */
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);
    mc_buffer_clear(&region);
    assert(mc_save_write_content_header(&region, content, 1) == MC_OK);
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);
    mc_buffer_clear(&region);
    assert(mc_map_write_section(&source, &region) == MC_OK);
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);
    mc_buffer_clear(&region);
    assert(mc_buffer_write_u16_be(&region, 0) == MC_OK); /* custom entity mapping */
    assert(mc_buffer_write_u32_be(&region, 0) == MC_OK); /* team build plans */
    assert(mc_buffer_write_u32_be(&region, 0) == MC_OK); /* serialized world entities */
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);
    mc_buffer_clear(&region);
    assert(mc_buffer_write_u8(&region, 0) == MC_OK); /* marker payload is skipped by this stage */
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);
    mc_buffer_clear(&region);
    assert(mc_buffer_write_u32_be(&region, 0) == MC_OK); /* no custom chunks */
    assert(mc_save_write_region(&uncompressed, region.data, region.size) == MC_OK);

    McBuffer compressed;
    mc_buffer_init(&compressed);
    assert(mc_zlib_compress_stored(uncompressed.data, uncompressed.size, &compressed) == MC_OK);
    McPlainMapSave loaded = {0};
    assert(mc_save_read_plain_map(compressed.data, compressed.size, &loaded) == MC_OK);
    assert(loaded.version == 13);
    assert(strcmp(loaded.meta.values[0], "native-fixture") == 0);
    assert(mc_content_header_find(&loaded.content, MC_CONTENT_BLOCK, "duo") == 4);
    assert(loaded.world.width == source.width && loaded.world.height == source.height);
    for(size_t i = 0; i < 6; i++){
        assert(loaded.world.tiles[i].floor == source.tiles[i].floor);
        assert(loaded.world.tiles[i].block == source.tiles[i].block);
    }

    mc_plain_map_save_destroy(&loaded);
    mc_buffer_destroy(&compressed);
    mc_buffer_destroy(&region);
    mc_buffer_destroy(&uncompressed);
    mc_world_destroy(&source);
}

static void test_java_content_header(void){
    static const char *items[] = {"copper", "lead"};
    static const char *blocks[] = {"air", "core-shard"};
    static const McContentGroupView groups[] = {
        {MC_CONTENT_ITEM, items, 2},
        {MC_CONTENT_BLOCK, blocks, 2}
    };

    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_save_write_content_header(&encoded, groups, 2) == MC_OK);
    static const uint8_t expected_prefix[] = {0x02, 0x00, 0x00, 0x02, 0x00, 0x06};
    assert(encoded.size > sizeof(expected_prefix));
    assert(memcmp(encoded.data, expected_prefix, sizeof(expected_prefix)) == 0);

    encoded.position = 0;
    McContentHeader decoded = {0};
    assert(mc_save_read_content_header(&encoded, &decoded) == MC_OK);
    assert(decoded.count == 2);
    assert(mc_content_header_find(&decoded, MC_CONTENT_ITEM, "copper") == 0);
    assert(mc_content_header_find(&decoded, MC_CONTENT_ITEM, "lead") == 1);
    assert(mc_content_header_find(&decoded, MC_CONTENT_BLOCK, "core-shard") == 1);
    assert(mc_content_header_find(&decoded, MC_CONTENT_BLOCK, "missing") == -1);
    mc_content_header_destroy(&decoded);
    mc_buffer_destroy(&encoded);
}

static void test_inventory_capacity(void){
    McInventory inventory;
    mc_inventory_clear(&inventory, 10);
    assert(mc_inventory_add(&inventory, MC_ITEM_COPPER, 7) == 7);
    assert(mc_inventory_add(&inventory, MC_ITEM_LEAD, 7) == 3);
    assert(mc_inventory_count(&inventory, MC_ITEM_COPPER) == 7);
    assert(mc_inventory_remove(&inventory, MC_ITEM_COPPER, 20) == 7);
    assert(mc_inventory_count(&inventory, MC_ITEM_COPPER) == 0);
}

static void test_block_placement(void){
    McSimulation simulation;
    assert(mc_simulation_init(&simulation, 16, 16, 42) == MC_OK);
    assert(mc_simulation_place_block(&simulation, MC_BLOCK_CORE_SHARD, MC_TEAM_SHARDED, 1, 2) == MC_OK);
    assert(mc_world_tile(&simulation.world, 1, 2)->block == MC_BLOCK_CORE_SHARD);
    assert(mc_world_tile(&simulation.world, 4, 4)->block == MC_BLOCK_AIR);
    assert(mc_simulation_place_block(&simulation, MC_BLOCK_DUO, MC_TEAM_SHARDED, 1, 2) == MC_CAPACITY_EXCEEDED);
    mc_simulation_destroy(&simulation);
}

static void test_deterministic_steps(void){
    McSimulation a, b;
    assert(mc_simulation_init(&a, 8, 8, 1234) == MC_OK);
    assert(mc_simulation_init(&b, 8, 8, 1234) == MC_OK);
    McEntity *unit_a = mc_simulation_spawn(&a, MC_ENTITY_UNIT, MC_TEAM_SHARDED, 1.0f, 2.0f);
    McEntity *unit_b = mc_simulation_spawn(&b, MC_ENTITY_UNIT, MC_TEAM_SHARDED, 1.0f, 2.0f);
    assert(unit_a != NULL && unit_b != NULL);
    unit_a->velocity.x = unit_b->velocity.x = 60.0f;
    unit_a->velocity.y = unit_b->velocity.y = -12.0f;
    for(int i = 0; i < 180; i++){
        assert(mc_simulation_step(&a) == MC_OK);
        assert(mc_simulation_step(&b) == MC_OK);
        assert(mc_simulation_hash(&a) == mc_simulation_hash(&b));
    }
    assert(a.tick == 180);
    assert(unit_a->position.x > 170.0f && unit_a->position.y < -30.0f);
    mc_simulation_destroy(&a);
    mc_simulation_destroy(&b);
}

static void test_content_table(void){
    size_t count = 0;
    const McItemDefinition *items = mc_item_definitions(&count);
    assert(items != NULL && count == MC_ITEM_COUNT);
    assert(items[MC_ITEM_COPPER].hardness == 1);
    assert(items[MC_ITEM_COPPER].buildable);
    assert(!items[MC_ITEM_COAL].buildable);
    assert(items[MC_ITEM_FISSILE_MATTER].hidden);
    assert(mc_block_health(MC_BLOCK_CORE_SHARD) == 1100);
    assert(mc_block_health(MC_BLOCK_MECHANICAL_DRILL) == 160);
    assert(mc_block_health(MC_BLOCK_CONVEYOR) == 45);
    assert(mc_block_health(MC_BLOCK_DUO) == 40);
}

int main(void){
    test_world_bounds_and_clear();
    test_java_map_section_codec();
    test_java_save_container();
    test_zlib_deflate_wrapper();
    test_plain_map_save_reader();
    test_java_content_header();
    test_inventory_capacity();
    test_block_placement();
    test_deterministic_steps();
    test_content_table();
    puts("native tests: ok");
    return 0;
}
