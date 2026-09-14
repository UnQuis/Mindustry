#include "mindustry.h"
#include "mindustry_format.h"

#include <assert.h>
#include <stdio.h>
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
    test_inventory_capacity();
    test_block_placement();
    test_deterministic_steps();
    test_content_table();
    puts("native tests: ok");
    return 0;
}
