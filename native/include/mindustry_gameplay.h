#ifndef MINDUSTRY_NATIVE_GAMEPLAY_H
#define MINDUSTRY_NATIVE_GAMEPLAY_H

#include "mindustry_save_loader.h"
#include "mindustry_building.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McGameplayRuntime{
    uint64_t saved;
    uint64_t playtime;
    uint32_t build;
    uint64_t tick;
    uint32_t wave;
    float wave_time;
    int32_t player_team;
    bool no_cores;
    char *map_name;
    char *rules_json;
    char *stats_json;
    char *locales_json;
    char *mods_json;
    char *sector_preset;
    char *view_position;
    char *controlled_type;
} McGameplayRuntime;

typedef struct McGameplayCounters{
    uint64_t steps;
    uint64_t waves_started;
    uint64_t blocks_placed;
    uint64_t blocks_removed;
    uint64_t entities_spawned;
    uint64_t entities_destroyed;
    uint64_t damage_dealt;
    uint64_t damage_received;
    uint64_t items_added;
    uint64_t items_removed;
} McGameplayCounters;

typedef struct McGameplayTeamState{
    bool active;
    uint32_t core_count;
    uint32_t building_count;
    McInventory inventory;
} McGameplayTeamState;

typedef struct McGameplay{
    McSimulation simulation;
    McBuildingStore buildings;
    McSaveFile save;
    McGameplayRuntime runtime;
    McGameplayCounters counters;
    McGameplayTeamState teams[MC_TEAM_COUNT];
    bool initialized;
} McGameplay;

/* Lifecycle and MSAV integration. The adopt function transfers ownership of
   the decoded save to gameplay and clears the caller's McSaveFile. */
MC_API McStatus mc_gameplay_init(McGameplay *gameplay, uint16_t width, uint16_t height, uint64_t seed);
MC_API void mc_gameplay_destroy(McGameplay *gameplay);
MC_API McStatus mc_gameplay_load(McGameplay *gameplay, const uint8_t *compressed, size_t size,
                                 const McContentRemap *remap, bool unknown_to_air);
MC_API McStatus mc_gameplay_adopt_save(McGameplay *gameplay, McSaveFile *save,
                                       const McContentRemap *remap, bool unknown_to_air);
MC_API McStatus mc_gameplay_write(McGameplay *gameplay, McBuffer *compressed);
MC_API McStatus mc_gameplay_save_metadata(McGameplay *gameplay);

/* Fixed-rate runtime operations. */
MC_API McStatus mc_gameplay_step(McGameplay *gameplay);
MC_API void mc_gameplay_set_paused(McGameplay *gameplay, bool paused);
MC_API McStatus mc_gameplay_set_wave(McGameplay *gameplay, uint32_t wave, float wave_time);
MC_API McStatus mc_gameplay_set_player_team(McGameplay *gameplay, int32_t team);
MC_API const McGameplayRuntime *mc_gameplay_runtime(const McGameplay *gameplay);
MC_API const McGameplayCounters *mc_gameplay_counters(const McGameplay *gameplay);
MC_API const McGameplayTeamState *mc_gameplay_team(const McGameplay *gameplay, McTeam team);
MC_API McBuildingStore *mc_gameplay_buildings(McGameplay *gameplay);
MC_API const McBuildingStore *mc_gameplay_buildings_const(const McGameplay *gameplay);
MC_API McBuildingState *mc_gameplay_find_building(McGameplay *gameplay, McEntityId entity_id);
MC_API McStatus mc_gameplay_step_buildings(McGameplay *gameplay, uint64_t tick);

/* World and entity operations used by the native gameplay layer. */
MC_API McTile *mc_gameplay_tile(McGameplay *gameplay, uint16_t x, uint16_t y);
MC_API const McTile *mc_gameplay_tile_const(const McGameplay *gameplay, uint16_t x, uint16_t y);
MC_API McTileData *mc_gameplay_tile_data(McGameplay *gameplay, uint16_t x, uint16_t y);
MC_API const McTileData *mc_gameplay_tile_data_const(const McGameplay *gameplay, uint16_t x, uint16_t y);
MC_API McStatus mc_gameplay_place_block(McGameplay *gameplay, McBlockId block, McTeam team, uint16_t x, uint16_t y);
MC_API McStatus mc_gameplay_remove_block(McGameplay *gameplay, uint16_t x, uint16_t y);
MC_API McEntity *mc_gameplay_spawn(McGameplay *gameplay, McEntityKind kind, McTeam team, float x, float y);
MC_API McEntity *mc_gameplay_spawn_raw(McGameplay *gameplay, McEntityKind kind, McTeam team, McEntityId id,
                                       float x, float y, uint8_t class_id, const uint8_t *data, size_t size);
MC_API McStatus mc_gameplay_destroy_entity(McGameplay *gameplay, McEntityId id);
MC_API McEntity *mc_gameplay_find_entity(McGameplay *gameplay, McEntityId id);
MC_API McStatus mc_gameplay_damage_entity(McGameplay *gameplay, McEntityId id, float amount, McTeam source_team);
MC_API McStatus mc_gameplay_heal_entity(McGameplay *gameplay, McEntityId id, float amount);

/* Team inventories and basic native resource accounting. */
MC_API McStatus mc_gameplay_add_items(McGameplay *gameplay, McTeam team, McItemId item, uint32_t amount);
MC_API McStatus mc_gameplay_remove_items(McGameplay *gameplay, McTeam team, McItemId item, uint32_t amount);
MC_API McStatus mc_gameplay_transfer_items(McGameplay *gameplay, McTeam from, McTeam to, McItemId item, uint32_t amount);
MC_API McStatus mc_gameplay_set_team_active(McGameplay *gameplay, McTeam team, bool active);
MC_API McStatus mc_gameplay_set_team_cores(McGameplay *gameplay, McTeam team, uint32_t cores);

/* Text metadata is retained in both the runtime view and the MSAV tags. */
MC_API const char *mc_gameplay_get_metadata(const McGameplay *gameplay, const char *key);
MC_API McStatus mc_gameplay_set_metadata(McGameplay *gameplay, const char *key, const char *value);

#endif
