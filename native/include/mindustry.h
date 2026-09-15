#ifndef MINDUSTRY_NATIVE_H
#define MINDUSTRY_NATIVE_H

/*
 * The native port deliberately exposes a small, dependency-free simulation API.
 * Rendering, audio and platform code sit on top of this API so that the
 * simulation can be tested without opening a window.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

#define MC_TICKS_PER_SECOND 60u
#define MC_TILE_SIZE 8.0f
#define MC_MAX_ENTITIES 4096u
#define MC_TEAM_COUNT 6u
#define MC_ITEM_COUNT 22u

#ifndef MC_API
#define MC_API
#endif

typedef enum McStatus{
    MC_OK = 0,
    MC_INVALID_ARGUMENT,
    MC_OUT_OF_MEMORY,
    MC_CAPACITY_EXCEEDED,
    MC_NOT_FOUND,
    MC_IO_ERROR,
    MC_FORMAT_ERROR
} McStatus;

typedef struct McVec2{
    float x;
    float y;
} McVec2;

typedef enum McTeam{
    MC_TEAM_SHARDED = 0,
    MC_TEAM_CRUX,
    MC_TEAM_BLUE,
    MC_TEAM_PURPLE,
    MC_TEAM_GREEN,
    MC_TEAM_DERELICT
} McTeam;

typedef enum McItemId{
    MC_ITEM_SCRAP = 0,
    MC_ITEM_COPPER,
    MC_ITEM_LEAD,
    MC_ITEM_GRAPHITE,
    MC_ITEM_COAL,
    MC_ITEM_TITANIUM,
    MC_ITEM_THORIUM,
    MC_ITEM_SILICON,
    MC_ITEM_PLASTANIUM,
    MC_ITEM_PHASE_FABRIC,
    MC_ITEM_SURGE_ALLOY,
    MC_ITEM_SPORE_POD,
    MC_ITEM_SAND,
    MC_ITEM_BLAST_COMPOUND,
    MC_ITEM_PYRATITE,
    MC_ITEM_METAGLASS,
    MC_ITEM_BERYLLIUM,
    MC_ITEM_TUNGSTEN,
    MC_ITEM_OXIDE,
    MC_ITEM_CARBIDE,
    MC_ITEM_FISSILE_MATTER,
    MC_ITEM_DORMANT_CYST
} McItemId;

typedef struct McItemDefinition{
    const char *name;
    uint32_t color_rgba;
    float explosiveness;
    float flammability;
    float radioactivity;
    float charge;
    int hardness;
    float cost;
    float health_scaling;
    bool low_priority;
    bool buildable;
    bool hidden;
} McItemDefinition;

MC_API const McItemDefinition *mc_item_definition(McItemId id);
MC_API const McItemDefinition *mc_item_definitions(size_t *count);

typedef enum McFloor{
    MC_FLOOR_STONE = 0,
    MC_FLOOR_SAND,
    MC_FLOOR_METAL,
    MC_FLOOR_WATER
} McFloor;

typedef enum McBlockId{
    MC_BLOCK_AIR = 0,
    MC_BLOCK_CORE_SHARD,
    MC_BLOCK_MECHANICAL_DRILL,
    MC_BLOCK_CONVEYOR,
    MC_BLOCK_DUO
} McBlockId;

typedef struct McTile{
    /* Java map IDs are shorts; keeping 16-bit fields avoids truncating IDs. */
    uint16_t floor;
    uint16_t overlay;
    uint16_t block;
} McTile;

typedef struct McTileData{
    uint8_t data;
    uint8_t floor_data;
    uint8_t overlay_data;
    uint32_t extra_data;
} McTileData;

typedef struct McWorld{
    uint16_t width;
    uint16_t height;
    McTile *tiles;
    /* Per-tile state from SaveVersion.writeMap. */
    McTileData *tile_data;
} McWorld;

MC_API McStatus mc_world_init(McWorld *world, uint16_t width, uint16_t height);
MC_API void mc_world_destroy(McWorld *world);
MC_API void mc_world_clear(McWorld *world, McFloor floor);
MC_API McTile *mc_world_tile(McWorld *world, uint16_t x, uint16_t y);
MC_API const McTile *mc_world_tile_const(const McWorld *world, uint16_t x, uint16_t y);
MC_API McTileData *mc_world_tile_data(McWorld *world, uint16_t x, uint16_t y);
MC_API const McTileData *mc_world_tile_data_const(const McWorld *world, uint16_t x, uint16_t y);
MC_API uint64_t mc_world_hash(const McWorld *world);

MC_API const char *mc_block_name(McBlockId id);
MC_API uint16_t mc_block_size(McBlockId id);
MC_API uint32_t mc_block_health(McBlockId id);

typedef struct McInventory{
    uint32_t items[MC_ITEM_COUNT];
    uint32_t capacity;
} McInventory;

MC_API void mc_inventory_clear(McInventory *inventory, uint32_t capacity);
MC_API uint32_t mc_inventory_count(const McInventory *inventory, McItemId item);
MC_API uint32_t mc_inventory_total(const McInventory *inventory);
MC_API uint32_t mc_inventory_space(const McInventory *inventory);
MC_API void mc_inventory_transfer(McInventory *from, McInventory *to, McItemId item, uint32_t amount);
MC_API uint32_t mc_inventory_add(McInventory *inventory, McItemId item, uint32_t amount);
MC_API uint32_t mc_inventory_remove(McInventory *inventory, McItemId item, uint32_t amount);

typedef uint32_t McEntityId;

#define MC_ENTITY_NONE ((McEntityId)0)

typedef enum McEntityKind{
    MC_ENTITY_UNIT = 0,
    MC_ENTITY_BUILDING,
    MC_ENTITY_BULLET,
    MC_ENTITY_EFFECT,
    /* Entity class is preserved, but no native class adapter exists yet. */
    MC_ENTITY_UNKNOWN
} McEntityKind;

typedef struct McEntity{
    bool active;
    McEntityId id;
    McEntityKind kind;
    McTeam team;
    McVec2 position;
    McVec2 velocity;
    float rotation;
    float health;
    float max_health;
    uint32_t lifetime;
    uint16_t tile_x;
    uint16_t tile_y;
    McBlockId block;
    uint8_t class_id;
    uint8_t *save_data;
    size_t save_size;
} McEntity;

MC_API float mc_entity_default_health(McEntityKind kind, McBlockId block);
MC_API float mc_entity_radius(McEntityKind kind, McBlockId block);

typedef struct McSimulation{
    McWorld world;
    McEntity entities[MC_MAX_ENTITIES];
    McInventory team_inventory[MC_TEAM_COUNT];
    uint64_t tick;
    uint32_t next_entity_id;
    uint64_t random_state;
    float wave_time;
    uint32_t wave;
    bool paused;
} McSimulation;

MC_API McStatus mc_simulation_init(McSimulation *simulation, uint16_t width, uint16_t height, uint64_t seed);
MC_API void mc_simulation_destroy(McSimulation *simulation);
MC_API void mc_simulation_reset(McSimulation *simulation, uint64_t seed);
MC_API void mc_simulation_reset_runtime(McSimulation *simulation, uint64_t seed);
MC_API McStatus mc_simulation_step(McSimulation *simulation);
MC_API McEntity *mc_simulation_spawn(McSimulation *simulation, McEntityKind kind, McTeam team, float x, float y);
MC_API McEntity *mc_simulation_spawn_with_id(McSimulation *simulation, McEntityKind kind, McTeam team, McEntityId id, float x, float y);
MC_API McStatus mc_simulation_attach_entity_data(McEntity *entity, uint8_t class_id, const uint8_t *data, size_t size);
MC_API void mc_simulation_destroy_entity(McSimulation *simulation, McEntityId id);
MC_API McEntity *mc_simulation_find(McSimulation *simulation, McEntityId id);
MC_API const McEntity *mc_simulation_find_const(const McSimulation *simulation, McEntityId id);
MC_API McStatus mc_simulation_place_block(McSimulation *simulation, McBlockId block, McTeam team, uint16_t x, uint16_t y);
MC_API uint64_t mc_simulation_hash(const McSimulation *simulation);

#endif
