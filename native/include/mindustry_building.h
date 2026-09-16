#ifndef MINDUSTRY_NATIVE_BUILDING_H
#define MINDUSTRY_NATIVE_BUILDING_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

#define MC_BUILDING_STATE_MAGIC_0 ((uint8_t)'M')
#define MC_BUILDING_STATE_MAGIC_1 ((uint8_t)'C')
#define MC_BUILDING_STATE_MAGIC_2 ((uint8_t)'B')
#define MC_BUILDING_STATE_MAGIC_3 ((uint8_t)'S')
#define MC_BUILDING_STATE_VERSION 1u
#define MC_BUILDING_LIQUID_COUNT 8u
#define MC_BUILDING_MAX_CONFIG (1024u * 1024u)
#define MC_BUILDING_MAX_RECIPES 64u
#define MC_BUILDING_MAX_LINKS 16384u

typedef enum McLiquidId{
    MC_LIQUID_WATER = 0,
    MC_LIQUID_SLAG,
    MC_LIQUID_CRYOFLUID,
    MC_LIQUID_OIL,
    MC_LIQUID_NEOPLASM,
    MC_LIQUID_SPORE_PRESSURE,
    MC_LIQUID_ARKYCITE,
    MC_LIQUID_FUEL
} McLiquidId;

typedef enum McBuildingKind{
    MC_BUILDING_GENERIC = 0,
    MC_BUILDING_CORE,
    MC_BUILDING_DRILL,
    MC_BUILDING_CONVEYOR,
    MC_BUILDING_TURRET,
    MC_BUILDING_FACTORY,
    MC_BUILDING_POWER,
    MC_BUILDING_LIQUID,
    MC_BUILDING_PROCESSOR,
    MC_BUILDING_STORAGE,
    MC_BUILDING_LAUNCHER
} McBuildingKind;

typedef enum McBuildingLinkKind{
    MC_BUILDING_LINK_ITEM = 0,
    MC_BUILDING_LINK_LIQUID,
    MC_BUILDING_LINK_POWER,
    MC_BUILDING_LINK_PAYLOAD
} McBuildingLinkKind;

typedef struct McBuildingPowerState{
    float capacity;
    float stored;
    float production;
    float consumption;
    float satisfaction;
    uint32_t network_id;
    bool connected;
    bool battery;
} McBuildingPowerState;

typedef struct McBuildingLiquidState{
    float amount[MC_BUILDING_LIQUID_COUNT];
    float capacity;
    uint32_t network_id;
    uint8_t dominant;
    bool connected;
} McBuildingLiquidState;

typedef struct McBuildingProductionState{
    uint16_t recipe;
    uint16_t pending_recipe;
    float progress;
    float warmup;
    float speed_scale;
    uint32_t produced;
    uint32_t consumed;
    bool active;
    bool output_blocked;
} McBuildingProductionState;

typedef struct McBuildingDrillState{
    uint16_t ore;
    uint16_t hardness;
    float progress;
    float drill_time;
    float speed_scale;
    uint32_t mined;
    uint32_t last_output;
    bool dominant;
    bool coolant;
} McBuildingDrillState;

typedef struct McBuildingTurretState{
    McEntityId target;
    McEntityId last_target;
    float reload;
    float reload_time;
    float range;
    float rotation;
    float target_rotation;
    float ammo_fraction;
    uint32_t shots;
    uint32_t misses;
    bool shooting;
    bool coolant;
    bool overdrive;
} McBuildingTurretState;

typedef struct McBuildingConveyorState{
    McItemId item;
    uint32_t item_amount;
    float progress;
    float speed;
    uint32_t moved;
    McEntityId next;
    McEntityId previous;
    bool occupied;
} McBuildingConveyorState;

typedef struct McBuildingFactoryState{
    uint16_t recipe;
    float progress;
    float craft_time;
    float efficiency;
    uint32_t crafts;
    uint32_t power_stalls;
    uint32_t input_stalls;
    bool running;
    bool output_blocked;
} McBuildingFactoryState;

typedef struct McBuildingCoreState{
    uint32_t storage_capacity;
    uint32_t unit_capacity;
    uint32_t units_spawned;
    uint32_t units_lost;
    uint32_t waves_survived;
    bool attack;
    bool launch_ready;
} McBuildingCoreState;

typedef struct McBuildingProcessorState{
    uint32_t processor_id;
    uint32_t program_counter;
    uint32_t executed;
    uint32_t errors;
    float execution_time;
    float range;
    bool enabled;
    bool linked;
} McBuildingProcessorState;

typedef struct McBuildingState{
    McEntityId entity_id;
    McBlockId block;
    McBuildingKind kind;
    McTeam team;
    uint16_t tile_x;
    uint16_t tile_y;
    uint16_t size;
    uint16_t rotation;
    float health;
    float max_health;
    float build_progress;
    float heat;
    float efficiency;
    float time_scale;
    uint64_t ticks;
    uint64_t last_tick;
    uint32_t flags;
    bool enabled;
    bool active;
    bool dead;
    McInventory inventory;
    McBuildingLiquidState liquids;
    McBuildingPowerState power;
    McBuildingProductionState production;
    McBuildingDrillState drill;
    McBuildingTurretState turret;
    McBuildingConveyorState conveyor;
    McBuildingFactoryState factory;
    McBuildingCoreState core;
    McBuildingProcessorState processor;
    uint8_t *config;
    size_t config_size;
} McBuildingState;

typedef struct McBuildingLink{
    McEntityId from;
    McEntityId to;
    McBuildingLinkKind kind;
    float distance;
    float capacity;
    float transferred;
    bool active;
} McBuildingLink;

typedef struct McBuildingRecipe{
    uint16_t id;
    McBuildingKind kind;
    uint16_t input_item;
    uint16_t output_item;
    uint16_t input_amount;
    uint16_t output_amount;
    uint16_t liquid;
    float liquid_amount;
    float craft_time;
    float power_use;
    bool valid;
} McBuildingRecipe;

typedef struct McBuildingStore{
    McBuildingState *states;
    size_t count;
    size_t capacity;
    McBuildingLink *links;
    size_t link_count;
    size_t link_capacity;
    uint32_t next_network_id;
    uint64_t tick;
    uint64_t produced_items;
    uint64_t mined_items;
    uint64_t transferred_items;
    uint64_t consumed_items;
    uint64_t power_ticks;
    uint64_t liquid_ticks;
    uint64_t turret_shots;
    uint64_t processor_steps;
} McBuildingStore;

/* Catalog and defaults. */
MC_API McBuildingKind mc_building_kind_for_block(McBlockId block);
MC_API const char *mc_building_kind_name(McBuildingKind kind);
MC_API uint16_t mc_building_default_size(McBlockId block);
MC_API float mc_building_default_health(McBlockId block);
MC_API float mc_building_default_capacity(McBuildingKind kind);
MC_API McBuildingRecipe mc_building_recipe(uint16_t recipe);
MC_API size_t mc_building_recipe_count(void);

/* Individual state ownership. */
MC_API McStatus mc_building_state_init(McBuildingState *state, McEntityId entity_id,
                                       McBlockId block, McTeam team, uint16_t tile_x, uint16_t tile_y);
MC_API void mc_building_state_destroy(McBuildingState *state);
MC_API void mc_building_state_reset(McBuildingState *state);
MC_API McStatus mc_building_state_set_config(McBuildingState *state, const uint8_t *data, size_t size);
MC_API McStatus mc_building_state_set_health(McBuildingState *state, float health, float max_health);
MC_API McStatus mc_building_state_set_rotation(McBuildingState *state, uint16_t rotation);
MC_API McStatus mc_building_state_set_enabled(McBuildingState *state, bool enabled);
MC_API McStatus mc_building_state_set_kind(McBuildingState *state, McBuildingKind kind);
MC_API void mc_building_state_mark_dead(McBuildingState *state);

/* Store ownership and lookup. */
MC_API void mc_building_store_init(McBuildingStore *store);
MC_API void mc_building_store_destroy(McBuildingStore *store);
MC_API McStatus mc_building_store_clear(McBuildingStore *store);
MC_API McBuildingState *mc_building_store_add(McBuildingStore *store, McEntityId entity_id,
                                               McBlockId block, McTeam team, uint16_t tile_x, uint16_t tile_y);
MC_API McStatus mc_building_store_remove(McBuildingStore *store, McEntityId entity_id);
MC_API McBuildingState *mc_building_store_find(McBuildingStore *store, McEntityId entity_id);
MC_API const McBuildingState *mc_building_store_find_const(const McBuildingStore *store, McEntityId entity_id);
MC_API McBuildingState *mc_building_store_find_tile(McBuildingStore *store, uint16_t tile_x, uint16_t tile_y);
MC_API const McBuildingState *mc_building_store_find_tile_const(const McBuildingStore *store, uint16_t tile_x, uint16_t tile_y);
MC_API McStatus mc_building_store_rebind_entity(McBuildingStore *store, uint16_t tile_x, uint16_t tile_y, McEntityId entity_id);
MC_API McStatus mc_building_store_set_tick(McBuildingStore *store, uint64_t tick);

/* Item, liquid and power state. */
MC_API uint32_t mc_building_item_count(const McBuildingState *state, McItemId item);
MC_API uint32_t mc_building_item_space(const McBuildingState *state);
MC_API uint32_t mc_building_add_item(McBuildingState *state, McItemId item, uint32_t amount);
MC_API uint32_t mc_building_remove_item(McBuildingState *state, McItemId item, uint32_t amount);
MC_API float mc_building_liquid_amount(const McBuildingState *state, McLiquidId liquid);
MC_API float mc_building_liquid_space(const McBuildingState *state, McLiquidId liquid);
MC_API float mc_building_add_liquid(McBuildingState *state, McLiquidId liquid, float amount);
MC_API float mc_building_remove_liquid(McBuildingState *state, McLiquidId liquid, float amount);
MC_API McStatus mc_building_set_power(McBuildingState *state, float stored, float capacity);
MC_API float mc_building_power_space(const McBuildingState *state);
MC_API float mc_building_receive_power(McBuildingState *state, float amount);
MC_API float mc_building_consume_power(McBuildingState *state, float amount);
MC_API void mc_building_recalculate_liquid_dominant(McBuildingState *state);
MC_API void mc_building_recalculate_efficiency(McBuildingState *state);

/* Block-specific state mutation. */
MC_API McStatus mc_building_set_recipe(McBuildingState *state, uint16_t recipe);
MC_API McStatus mc_building_set_drill(McBuildingState *state, uint16_t ore, uint16_t hardness, float drill_time);
MC_API McStatus mc_building_set_turret(McBuildingState *state, float range, float reload_time);
MC_API McStatus mc_building_set_conveyor(McBuildingState *state, float speed);
MC_API McStatus mc_building_set_factory(McBuildingState *state, float craft_time);
MC_API McStatus mc_building_set_core(McBuildingState *state, uint32_t storage_capacity, uint32_t unit_capacity);
MC_API McStatus mc_building_set_processor(McBuildingState *state, uint32_t processor_id, float range);
MC_API McStatus mc_building_set_target(McBuildingState *state, McEntityId target);
MC_API McStatus mc_building_set_build_progress(McBuildingState *state, float progress);
MC_API McStatus mc_building_set_time_scale(McBuildingState *state, float scale);

/* Links and network bookkeeping. */
MC_API McStatus mc_building_store_connect(McBuildingStore *store, McEntityId from, McEntityId to,
                                           McBuildingLinkKind kind, float capacity);
MC_API McStatus mc_building_store_disconnect(McBuildingStore *store, McEntityId from, McEntityId to,
                                              McBuildingLinkKind kind);
MC_API void mc_building_store_disconnect_entity(McBuildingStore *store, McEntityId entity_id);
MC_API McBuildingLink *mc_building_store_find_link(McBuildingStore *store, McEntityId from, McEntityId to,
                                                    McBuildingLinkKind kind);
MC_API McStatus mc_building_store_rebuild_networks(McBuildingStore *store);
MC_API McStatus mc_building_store_transfer_items(McBuildingStore *store, McEntityId from, McEntityId to,
                                                  McItemId item, uint32_t amount);
MC_API float mc_building_store_transfer_liquid(McBuildingStore *store, McEntityId from, McEntityId to,
                                               McLiquidId liquid, float amount);
MC_API float mc_building_store_transfer_power(McBuildingStore *store, McEntityId from, McEntityId to,
                                               float amount);

/* Deterministic building update. */
MC_API McStatus mc_building_state_step(McBuildingState *state, uint64_t tick);
MC_API McStatus mc_building_store_step(McBuildingStore *store, uint64_t tick);
MC_API McStatus mc_building_store_step_range(McBuildingStore *store, uint64_t first_tick, uint32_t ticks);
MC_API McStatus mc_building_store_damage(McBuildingStore *store, McEntityId entity_id, float amount);
MC_API McStatus mc_building_store_heal(McBuildingStore *store, McEntityId entity_id, float amount);
MC_API size_t mc_building_store_active_count(const McBuildingStore *store);
MC_API uint64_t mc_building_store_hash(const McBuildingStore *store);

/* Native custom-chunk payload codec. It is independent from Java map entity
   payloads, so Java saves remain readable while native building fields survive
   a native save/load cycle. */
MC_API McStatus mc_building_store_write(const McBuildingStore *store, McBuffer *output);
MC_API McStatus mc_building_store_read(const uint8_t *data, size_t size, McBuildingStore *store);

#endif
