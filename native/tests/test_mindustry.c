#include "mindustry.h"
#include "mindustry_format.h"
#include "mindustry_save.h"
#include "mindustry_deflate.h"
#include "mindustry_schematic.h"
#include "mindustry_png.h"
#include "mindustry_map.h"
#include "mindustry_save_loader.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_map_section_entity_records(void){
    McMapSection source = {0};
    assert(mc_map_section_init(&source, 3, 2) == MC_OK);
    source.tiles[2].tile.floor = MC_FLOOR_SAND;
    source.tiles[3].tile.block = MC_BLOCK_CONVEYOR;
    source.tiles[3].flags = 4;
    source.tiles[3].data = 7;
    source.tiles[3].floor_data = 8;
    source.tiles[3].overlay_data = 9;
    source.tiles[3].extra_data = 0x12345678;
    source.tiles[4].tile.block = MC_BLOCK_DUO;
    source.tiles[4].flags = 1;
    source.tiles[4].entity_center = true;
    source.tiles[4].entity_size = 4;
    source.tiles[4].entity_data = malloc(4);
    assert(source.tiles[4].entity_data != NULL);
    static const uint8_t entity_fixture[] = {0x03, 'a', 'b', 'c'};
    memcpy(source.tiles[4].entity_data, entity_fixture, sizeof(entity_fixture));
    source.tiles[5].tile.block = MC_BLOCK_DUO;
    source.tiles[5].flags = 1;
    source.tiles[5].entity_center = false;

    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_map_section_write(&source, &encoded) == MC_OK);
    McMapSection decoded = {0};
    assert(mc_map_section_read(encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.width == source.width && decoded.height == source.height);
    assert(decoded.tiles[3].flags == 4 && decoded.tiles[3].extra_data == 0x12345678);
    assert(decoded.tiles[4].entity_center && decoded.tiles[4].entity_size == 4);
    assert(memcmp(decoded.tiles[4].entity_data, entity_fixture, sizeof(entity_fixture)) == 0);
    assert(decoded.tiles[5].flags == 1 && !decoded.tiles[5].entity_center);
    mc_map_section_destroy(&decoded);
    mc_buffer_destroy(&encoded);
    mc_map_section_destroy(&source);
}

static void test_full_save_loader(void){
    McMapSection map = {0};
    assert(mc_map_section_init(&map, 2, 2) == MC_OK);
    map.tiles[3].tile.block = MC_BLOCK_DUO;
    map.tiles[3].flags = 1;
    map.tiles[3].entity_center = true;
    map.tiles[3].entity_size = 3;
    map.tiles[3].entity_data = malloc(3);
    assert(map.tiles[3].entity_data != NULL);
    memcpy(map.tiles[3].entity_data, "xyz", 3);

    static char *meta_keys[] = {"mapname", "width", "height"};
    static char *meta_values[] = {"loader-fixture", "2", "2"};
    static char *block_names[] = {"air", "core-shard", "mechanical-drill", "conveyor", "duo"};
    static McContentGroup content_group = {MC_CONTENT_BLOCK, block_names, 5};
    static const uint8_t patches[] = {0,0,0,2, 0,0,0,0, 0,0,0,0};
    static const uint8_t legacy_patches[] = {0};
    static const uint8_t entities[] = {0,0, 0,0,0,0, 0,0,0,0};
    static const uint8_t markers[] = {'{', 'i', 2, 'i', 'd', 'l', 0, 0, 0, 42, '}'};
    static const uint8_t custom[] = {0,0,0,0};
    McSaveFile source = {
        .version = 13,
        .meta = {.keys = meta_keys, .values = meta_values, .count = 3},
        .patches = {(uint8_t *)patches, sizeof(patches)},
        .content = {.groups = &content_group, .count = 1},
        .map = map,
        .entities = {(uint8_t *)entities, sizeof(entities)},
        .markers = {(uint8_t *)markers, sizeof(markers)},
        .custom = {(uint8_t *)custom, sizeof(custom)}
    };

    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_save_file_write(&source, &encoded) == MC_OK);
    McSaveFile loaded = {0};
    assert(mc_save_file_load(encoded.data, encoded.size, &loaded) == MC_OK);
    assert(loaded.version == 13);
    assert(strcmp(loaded.meta.values[0], "loader-fixture") == 0);
    assert(mc_content_header_find(&loaded.content, MC_CONTENT_BLOCK, "duo") == 4);
    assert(loaded.map.tiles[3].entity_size == 3);
    assert(memcmp(loaded.map.tiles[3].entity_data, "xyz", 3) == 0);
    assert(loaded.entities.size == sizeof(entities) && memcmp(loaded.entities.data, entities, sizeof(entities)) == 0);
    assert(loaded.entity_data.mapping_count == 0 && loaded.entity_data.record_count == 0);
    assert(loaded.markers.size == sizeof(markers) && memcmp(loaded.markers.data, markers, sizeof(markers)) == 0);
    assert(loaded.marker_data.root.count == 1 && loaded.marker_data.root.values[0]->integer == 42);

    McBuffer rewritten;
    mc_buffer_init(&rewritten);
    assert(mc_save_file_write(&loaded, &rewritten) == MC_OK);
    McSaveFile loaded_again = {0};
    assert(mc_save_file_load(rewritten.data, rewritten.size, &loaded_again) == MC_OK);
    assert(loaded_again.map.tiles[3].entity_size == 3);
    assert(memcmp(loaded_again.map.tiles[3].entity_data, "xyz", 3) == 0);
    assert(loaded_again.marker_data.root.count == 1);

    mc_save_file_destroy(&loaded_again);

    McBuffer raw_save, damaged_save;
    mc_buffer_init(&raw_save);
    mc_buffer_init(&damaged_save);
    assert(mc_zlib_decompress(encoded.data, encoded.size, &raw_save) == MC_OK);
    raw_save.data[7] = 14;
    assert(mc_zlib_compress_stored(raw_save.data, raw_save.size, &damaged_save) == MC_OK);
    McSaveFile unsupported = {0};
    assert(mc_save_file_load(damaged_save.data, damaged_save.size, &unsupported) == MC_FORMAT_ERROR);
    mc_buffer_clear(&raw_save);
    assert(mc_zlib_decompress(encoded.data, encoded.size, &raw_save) == MC_OK);
    assert(mc_buffer_write_u8(&raw_save, 0x7f) == MC_OK);
    assert(mc_zlib_compress_stored(raw_save.data, raw_save.size, &damaged_save) == MC_OK);
    McSaveFile trailing = {0};
    assert(mc_save_file_load(damaged_save.data, damaged_save.size, &trailing) == MC_FORMAT_ERROR);
    mc_buffer_destroy(&damaged_save);
    mc_buffer_destroy(&raw_save);

    source.version = 11;
    source.patches = (McSaveBlob){(uint8_t *)legacy_patches, sizeof(legacy_patches)};
    mc_buffer_clear(&encoded);
    assert(mc_save_file_write(&source, &encoded) == MC_OK);
    McSaveFile legacy_loaded = {0};
    assert(mc_save_file_load(encoded.data, encoded.size, &legacy_loaded) == MC_OK);
    assert(legacy_loaded.version == 11 && legacy_loaded.patch_data.legacy && legacy_loaded.patch_data.count == 0);
    mc_save_file_destroy(&legacy_loaded);

    source.version = 8;
    source.patches = (McSaveBlob){0};
    mc_buffer_clear(&encoded);
    assert(mc_save_file_write(&source, &encoded) == MC_OK);
    McSaveFile v8_loaded = {0};
    assert(mc_save_file_load(encoded.data, encoded.size, &v8_loaded) == MC_OK);
    assert(v8_loaded.version == 8 && v8_loaded.patches.size == 0);
    mc_save_file_destroy(&v8_loaded);

    mc_save_file_destroy(&loaded);
    mc_buffer_destroy(&rewritten);
    mc_buffer_destroy(&encoded);
    mc_map_section_destroy(&map);
}

static void test_markers_codec(void){
    static const uint8_t fixture[] = {'{', 'i', 2, 'i', 'd', 'l', 0, 0, 0, 42, '}'};
    McMarkers markers = {0};
    assert(mc_markers_read(fixture, sizeof(fixture), &markers) == MC_OK);
    assert(markers.root.kind == MC_UBJSON_OBJECT && markers.root.count == 1);
    assert(strcmp(markers.root.keys[0], "id") == 0);
    assert(markers.root.values[0]->kind == MC_UBJSON_INT32 && markers.root.values[0]->integer == 42);
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_markers_write(&markers, &encoded) == MC_OK);
    assert(memcmp(encoded.data, fixture, sizeof(fixture)) == 0);
    mc_markers_destroy(&markers);
    mc_buffer_destroy(&encoded);

    static const uint8_t malformed[] = {'{', 'i', 1, 'x'};
    assert(mc_markers_read(malformed, sizeof(malformed), &markers) == MC_FORMAT_ERROR);
}

static void test_entities_region_codec(void){
    static char mapping_name[] = "custom-building";
    static uint8_t config[] = {1, 0, 0, 0, 42};
    static uint8_t record_data[] = {7, 0, 0, 0, 9, 0xaa};
    McEntityMapping mapping = {.id = 33, .name = mapping_name};
    McTeamPlan plan = {.team = 2, .x = -4, .y = 8, .rotation = 1, .block = 12,
                       .config = config, .config_size = sizeof(config)};
    McEntityRecord record = {.data = record_data, .size = sizeof(record_data)};
    McEntitiesRegion source = {.mapping = &mapping, .mapping_count = 1, .plans = &plan,
                               .plan_count = 1, .records = &record, .record_count = 1};
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_entities_write(&source, &encoded) == MC_OK);
    McEntitiesRegion decoded = {0};
    assert(mc_entities_read(encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.mapping_count == 1 && decoded.mapping[0].id == 33);
    assert(strcmp(decoded.mapping[0].name, mapping_name) == 0);
    assert(decoded.plan_count == 1 && decoded.plans[0].team == 2);
    assert(decoded.plans[0].x == -4 && decoded.plans[0].config_size == sizeof(config));
    assert(memcmp(decoded.plans[0].config, config, sizeof(config)) == 0);
    assert(decoded.record_count == 1 && decoded.records[0].size == sizeof(record_data));
    assert(memcmp(decoded.records[0].data, record_data, sizeof(record_data)) == 0);
    mc_entities_destroy(&decoded);
    mc_buffer_destroy(&encoded);
}

static void test_custom_chunks_codec(void){
    static char name[] = "native-test";
    static uint8_t bytes[] = {9, 8, 7};
    McCustomChunk chunk = {.name = name, .data = bytes, .size = sizeof(bytes)};
    McCustomChunks source = {.chunks = &chunk, .count = 1};
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_custom_chunks_write(&source, &encoded) == MC_OK);
    McCustomChunks decoded = {0};
    assert(mc_custom_chunks_read(encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.count == 1 && strcmp(decoded.chunks[0].name, name) == 0);
    assert(decoded.chunks[0].size == sizeof(bytes));
    assert(memcmp(decoded.chunks[0].data, bytes, sizeof(bytes)) == 0);
    mc_custom_chunks_destroy(&decoded);
    mc_buffer_destroy(&encoded);
}

static void test_data_patches_codec(void){
    static const uint8_t text[] = {'{', '}', '\n'};
    static const uint8_t image[] = {1, 2, 3, 4};
    static char image_name[] = "map/icon";
    McPatchAsset assets[] = {
        {.kind = MC_PATCH_TEXT, .data = (uint8_t *)text, .size = sizeof(text)},
        {.kind = MC_PATCH_IMAGE, .name = image_name, .width = 2, .height = 2,
         .data = (uint8_t *)image, .size = sizeof(image)}
    };
    McDataPatches source = {.format_version = 2, .assets = assets, .count = 2};
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_data_patches_write(12, &source, &encoded) == MC_OK);
    McDataPatches decoded = {0};
    assert(mc_data_patches_read(12, encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.format_version == 2 && decoded.count == 2);
    assert(decoded.assets[0].kind == MC_PATCH_TEXT && decoded.assets[0].size == sizeof(text));
    assert(decoded.assets[1].kind == MC_PATCH_IMAGE && strcmp(decoded.assets[1].name, image_name) == 0);
    assert(decoded.assets[1].width == 2 && decoded.assets[1].height == 2);
    assert(memcmp(decoded.assets[1].data, image, sizeof(image)) == 0);
    mc_data_patches_destroy(&decoded);
    mc_buffer_clear(&encoded);

    McDataPatches legacy = {.legacy = true, .assets = assets, .count = 1};
    assert(mc_data_patches_write(11, &legacy, &encoded) == MC_OK);
    assert(mc_data_patches_read(11, encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.legacy && decoded.count == 1 && decoded.assets[0].size == sizeof(text));
    mc_data_patches_destroy(&decoded);
    mc_buffer_destroy(&encoded);
}

static void test_png_map_image_codec(void){
    static const uint8_t pixels[] = {
        0xff, 0x00, 0x00, 0xff, 0x00, 0xff, 0x00, 0x80, 0x00, 0x00, 0xff, 0x00,
        0xff, 0xff, 0xff, 0xff, 0x12, 0x34, 0x56, 0x78, 0x00, 0x00, 0x00, 0x00
    };
    McImage source = {.width = 3, .height = 2, .rgba = (uint8_t *)pixels};
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_png_write(&source, &encoded) == MC_OK);
    assert(encoded.size > 8);

    McImage decoded = {0};
    assert(mc_png_read(encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.width == source.width && decoded.height == source.height);
    assert(memcmp(decoded.rgba, pixels, sizeof(pixels)) == 0);

    /* Fixture generated with one RGB row for each PNG filter 0..4. */
    static const uint8_t filter_png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d,
        0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x05,
        0x08, 0x02, 0x00, 0x00, 0x00, 0x0f, 0x13, 0xc1, 0xf5, 0x00, 0x00, 0x00, 0x2e,
        0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xe0, 0x12, 0x91, 0xd3, 0x30, 0xb2, 0x71,
        0x0b, 0x88, 0x62, 0x4c, 0xc9, 0xab, 0x90, 0x03, 0x03, 0xa6, 0x28, 0x30, 0xc8, 0xcf,
        0xcf, 0x67, 0x3e, 0x77, 0xf9, 0xc6, 0xf1, 0xe3, 0xc7, 0x6d, 0x6c, 0x6c, 0x58, 0x80,
        0x02, 0x10, 0x59, 0x00, 0x4d, 0x8e, 0x0e, 0x78, 0xf7, 0x64, 0xff, 0x57, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82
    };
    static const uint8_t filter_pixels[] = {
        10,20,30,255, 40,50,60,255, 70,80,90,255,
        100,110,120,255, 130,140,150,255, 160,170,180,255,
        190,200,210,255, 220,230,240,255, 15,25,35,255,
        45,55,65,255, 75,85,95,255, 105,115,125,255,
        135,145,155,255, 165,175,185,255, 195,205,215,255
    };
    mc_image_destroy(&decoded);
    assert(mc_png_read(filter_png, sizeof(filter_png), &decoded) == MC_OK);
    assert(decoded.width == 3 && decoded.height == 5);
    assert(memcmp(decoded.rgba, filter_pixels, sizeof(filter_pixels)) == 0);

    uint8_t *damaged = malloc(encoded.size);
    assert(damaged != NULL);
    memcpy(damaged, encoded.data, encoded.size);
    damaged[encoded.size - 1] ^= 1;
    assert(mc_png_read(damaged, encoded.size, &decoded) == MC_FORMAT_ERROR);
    free(damaged);
    mc_image_destroy(&decoded);
    mc_buffer_destroy(&encoded);
}

static void test_schematic_codec(void){
    static char *keys[] = {"name", "labels"};
    static char *values[] = {"native schematic", "[]"};
    static char *blocks[] = {"conveyor", "duo"};
    static uint8_t integer_config[] = {1, 0, 0, 0, 42};
    static McSchematicTile tiles[] = {
        {.block = 0, .x = 0, .y = 0, .rotation = 0},
        {.block = 1, .x = 1, .y = 0, .rotation = 2, .config = integer_config, .config_size = sizeof(integer_config)},
        {.block = 0, .x = -1, .y = 3, .rotation = 1}
    };
    McSchematic source = {
        .width = 8,
        .height = 6,
        .tags = {.keys = keys, .values = values, .count = 2},
        .blocks = blocks,
        .block_count = 2,
        .tiles = (McSchematicTile *)tiles,
        .tile_count = 3
    };
    McBuffer encoded;
    mc_buffer_init(&encoded);
    assert(mc_schematic_write(&source, &encoded) == MC_OK);
    assert(encoded.size > 5 && memcmp(encoded.data, "msch\x01", 5) == 0);

    McSchematic decoded = {0};
    assert(mc_schematic_read(encoded.data, encoded.size, &decoded) == MC_OK);
    assert(decoded.width == source.width && decoded.height == source.height);
    assert(decoded.tags.count == 2 && strcmp(decoded.tags.values[0], "native schematic") == 0);
    assert(decoded.block_count == 2 && strcmp(decoded.blocks[1], "duo") == 0);
    assert(decoded.tile_count == 3);
    for(size_t i = 0; i < decoded.tile_count; i++){
        assert(decoded.tiles[i].block == tiles[i].block);
        assert(decoded.tiles[i].x == tiles[i].x && decoded.tiles[i].y == tiles[i].y);
        assert(decoded.tiles[i].rotation == tiles[i].rotation);
    }
    assert(decoded.tiles[0].config_size == 1 && decoded.tiles[0].config[0] == 0);
    assert(decoded.tiles[1].config_size == sizeof(integer_config));
    assert(memcmp(decoded.tiles[1].config, integer_config, sizeof(integer_config)) == 0);

    uint8_t invalid_header[6] = {'m', 's', 'c', 'h', 2, 0};
    assert(mc_schematic_read(invalid_header, sizeof(invalid_header), &decoded) == MC_FORMAT_ERROR);
    mc_schematic_destroy(&decoded);
    mc_buffer_destroy(&encoded);
}

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

    /* The high-level writer composes the same regions and zlib stream for a
       map with no building records. It rejects non-air building tiles. */
    source.tiles[4].block = MC_BLOCK_AIR;
    McPlainMapSaveOptions options = {
        .version = 13,
        .meta = meta,
        .meta_count = 3,
        .content = content,
        .content_count = 1,
        .world = &source
    };
    McBuffer generated;
    mc_buffer_init(&generated);
    assert(mc_save_write_plain_map(&options, &generated) == MC_OK);
    McPlainMapSave generated_save = {0};
    assert(mc_save_read_plain_map(generated.data, generated.size, &generated_save) == MC_OK);
    assert(generated_save.world.width == source.width && generated_save.world.height == source.height);
    assert(generated_save.world.tiles[4].block == MC_BLOCK_AIR);
    mc_plain_map_save_destroy(&generated_save);
    mc_buffer_destroy(&generated);

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
    static const char *current_blocks[] = {"duo", "air", "core-shard"};
    static const McContentGroupView current[] = {{MC_CONTENT_BLOCK, current_blocks, 3}};
    McContentRemap remap = {0};
    assert(mc_content_remap_build(&decoded, current, 1, &remap) == MC_OK);
    assert(mc_content_remap_find(&remap, MC_CONTENT_ITEM, 0) == -1);
    assert(mc_content_remap_find(&remap, MC_CONTENT_BLOCK, 0) == 1);
    assert(mc_content_remap_find(&remap, MC_CONTENT_BLOCK, 1) == 2);
    mc_content_remap_destroy(&remap);
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
    test_map_section_entity_records();
    test_full_save_loader();
    test_markers_codec();
    test_entities_region_codec();
    test_custom_chunks_codec();
    test_data_patches_codec();
    test_png_map_image_codec();
    test_schematic_codec();
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
