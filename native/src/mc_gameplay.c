#include "mindustry_gameplay.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *duplicate_string(const char *value){
    if(value == NULL) return NULL;
    size_t length = strlen(value);
    char *copy = malloc(length + 1);
    if(copy == NULL) return NULL;
    memcpy(copy, value, length + 1);
    return copy;
}

static const char *tag_value(const McSaveTags *tags, const char *key){
    if(tags == NULL || key == NULL) return NULL;
    for(size_t i = 0; i < tags->count; i++){
        if(tags->keys[i] != NULL && strcmp(tags->keys[i], key) == 0) return tags->values[i];
    }
    return NULL;
}

static bool parse_unsigned(const char *text, uint64_t *value){
    if(text == NULL || value == NULL || *text == '\0') return false;
    errno = 0;
    char *end = NULL;
    unsigned long long parsed = strtoull(text, &end, 10);
    if(errno == ERANGE || end == text || *end != '\0') return false;
    *value = (uint64_t)parsed;
    return true;
}

static bool parse_signed(const char *text, int64_t *value){
    if(text == NULL || value == NULL || *text == '\0') return false;
    errno = 0;
    char *end = NULL;
    long long parsed = strtoll(text, &end, 10);
    if(errno == ERANGE || end == text || *end != '\0') return false;
    *value = (int64_t)parsed;
    return true;
}

static bool parse_real(const char *text, double *value){
    if(text == NULL || value == NULL || *text == '\0') return false;
    errno = 0;
    char *end = NULL;
    double parsed = strtod(text, &end);
    if(errno == ERANGE || end == text || *end != '\0') return false;
    *value = parsed;
    return true;
}

static bool parse_boolean(const char *text, bool fallback){
    if(text == NULL) return fallback;
    if(strcmp(text, "true") == 0 || strcmp(text, "1") == 0) return true;
    if(strcmp(text, "false") == 0 || strcmp(text, "0") == 0) return false;
    return fallback;
}

static void runtime_destroy(McGameplayRuntime *runtime){
    if(runtime == NULL) return;
    free(runtime->map_name);
    free(runtime->rules_json);
    free(runtime->stats_json);
    free(runtime->locales_json);
    free(runtime->mods_json);
    free(runtime->sector_preset);
    free(runtime->view_position);
    free(runtime->controlled_type);
    *runtime = (McGameplayRuntime){0};
}

static McStatus runtime_set_string(char **destination, const char *value){
    char *copy = duplicate_string(value == NULL ? "" : value);
    if(copy == NULL) return MC_OUT_OF_MEMORY;
    free(*destination);
    *destination = copy;
    return MC_OK;
}

static void gameplay_team_reset(McGameplay *gameplay){
    for(size_t i = 0; i < MC_TEAM_COUNT; i++){
        gameplay->teams[i] = (McGameplayTeamState){0};
        gameplay->teams[i].active = i == MC_TEAM_SHARDED;
        mc_inventory_clear(&gameplay->teams[i].inventory, 10000);
        mc_inventory_clear(&gameplay->simulation.team_inventory[i], 10000);
    }
}

static void runtime_reset(McGameplayRuntime *runtime){
    if(runtime == NULL) return;
    runtime_destroy(runtime);
    runtime->wave = 1;
    runtime->player_team = MC_TEAM_SHARDED;
    runtime->rules_json = duplicate_string("{}");
    runtime->stats_json = duplicate_string("{}");
    runtime->locales_json = duplicate_string("{}");
    runtime->mods_json = duplicate_string("[]");
    runtime->sector_preset = duplicate_string("");
    runtime->view_position = duplicate_string("0,0");
    runtime->controlled_type = duplicate_string("");
}

static McStatus runtime_read_metadata(McGameplay *gameplay){
    McGameplayRuntime *runtime = &gameplay->runtime;
    const McSaveTags *tags = &gameplay->save.meta;
    runtime_reset(runtime);
    if(runtime->rules_json == NULL || runtime->stats_json == NULL || runtime->locales_json == NULL ||
       runtime->mods_json == NULL || runtime->sector_preset == NULL || runtime->view_position == NULL ||
       runtime->controlled_type == NULL) return MC_OUT_OF_MEMORY;

    uint64_t unsigned_value = 0;
    int64_t signed_value = 0;
    double real_value = 0.0;
    const char *value = tag_value(tags, "saved");
    if(parse_unsigned(value, &unsigned_value)) runtime->saved = unsigned_value;
    value = tag_value(tags, "playtime");
    if(parse_unsigned(value, &unsigned_value)) runtime->playtime = unsigned_value;
    value = tag_value(tags, "build");
    if(parse_unsigned(value, &unsigned_value)) runtime->build = (uint32_t)(unsigned_value > UINT32_MAX ? UINT32_MAX : unsigned_value);
    value = tag_value(tags, "tick");
    if(parse_real(value, &real_value) && real_value >= 0.0 && real_value <= (double)UINT64_MAX) runtime->tick = (uint64_t)real_value;
    value = tag_value(tags, "wave");
    if(parse_unsigned(value, &unsigned_value)) runtime->wave = (uint32_t)(unsigned_value > UINT32_MAX ? UINT32_MAX : unsigned_value);
    value = tag_value(tags, "wavetime");
    if(parse_real(value, &real_value) && real_value >= 0.0 && real_value <= 3.402823466e38) runtime->wave_time = (float)real_value;
    value = tag_value(tags, "playerteam");
    if(parse_signed(value, &signed_value) && signed_value >= INT32_MIN && signed_value <= INT32_MAX) runtime->player_team = (int32_t)signed_value;
    runtime->no_cores = parse_boolean(tag_value(tags, "nocores"), false);

    McStatus status = runtime_set_string(&runtime->map_name, tag_value(tags, "mapname"));
    if(status != MC_OK) return status;
    status = runtime_set_string(&runtime->rules_json, tag_value(tags, "rules"));
    if(status != MC_OK) return status;
    status = runtime_set_string(&runtime->stats_json, tag_value(tags, "stats"));
    if(status != MC_OK) return status;
    status = runtime_set_string(&runtime->locales_json, tag_value(tags, "locales"));
    if(status != MC_OK) return status;
    status = runtime_set_string(&runtime->mods_json, tag_value(tags, "mods"));
    if(status != MC_OK) return status;
    status = runtime_set_string(&runtime->sector_preset, tag_value(tags, "sectorPreset"));
    if(status != MC_OK) return status;
    status = runtime_set_string(&runtime->view_position, tag_value(tags, "viewpos"));
    if(status != MC_OK) return status;
    return runtime_set_string(&runtime->controlled_type, tag_value(tags, "controlledType"));
}

static McStatus tags_set(McSaveTags *tags, const char *key, const char *value){
    if(tags == NULL || key == NULL || value == NULL) return MC_INVALID_ARGUMENT;
    for(size_t i = 0; i < tags->count; i++){
        if(strcmp(tags->keys[i], key) != 0) continue;
        char *copy = duplicate_string(value);
        if(copy == NULL) return MC_OUT_OF_MEMORY;
        free(tags->values[i]);
        tags->values[i] = copy;
        return MC_OK;
    }
    if(tags->count == SIZE_MAX || tags->count + 1 > SIZE_MAX / sizeof(*tags->keys)) return MC_CAPACITY_EXCEEDED;
    char *key_copy = duplicate_string(key);
    char *value_copy = duplicate_string(value);
    if(key_copy == NULL || value_copy == NULL){
        free(key_copy);
        free(value_copy);
        return MC_OUT_OF_MEMORY;
    }
    char **keys = realloc(tags->keys, (tags->count + 1) * sizeof(*keys));
    char **values = realloc(tags->values, (tags->count + 1) * sizeof(*values));
    if(keys == NULL || values == NULL){
        /* A failed second realloc leaves the first allocation valid but not
           safely recoverable through the old pointer. Keep the old arrays by
           using a temporary copy path for this rare allocation failure. */
        if(keys != NULL) tags->keys = keys;
        if(values != NULL) tags->values = values;
        free(key_copy);
        free(value_copy);
        return MC_OUT_OF_MEMORY;
    }
    tags->keys = keys;
    tags->values = values;
    tags->keys[tags->count] = key_copy;
    tags->values[tags->count] = value_copy;
    tags->count++;
    return MC_OK;
}

static McStatus set_unsigned_tag(McSaveTags *tags, const char *key, uint64_t value){
    char text[32];
    int written = snprintf(text, sizeof(text), "%llu", (unsigned long long)value);
    if(written < 0 || (size_t)written >= sizeof(text)) return MC_CAPACITY_EXCEEDED;
    return tags_set(tags, key, text);
}

static McStatus set_real_tag(McSaveTags *tags, const char *key, double value){
    char text[64];
    int written = snprintf(text, sizeof(text), "%.9g", value);
    if(written < 0 || (size_t)written >= sizeof(text)) return MC_CAPACITY_EXCEEDED;
    return tags_set(tags, key, text);
}

static McStatus set_signed_tag(McSaveTags *tags, const char *key, int64_t value){
    char text[32];
    int written = snprintf(text, sizeof(text), "%lld", (long long)value);
    if(written < 0 || (size_t)written >= sizeof(text)) return MC_CAPACITY_EXCEEDED;
    return tags_set(tags, key, text);
}

static McStatus runtime_write_metadata(McGameplay *gameplay){
    McGameplayRuntime *runtime = &gameplay->runtime;
    McSaveTags *tags = &gameplay->save.meta;
    McStatus status = set_unsigned_tag(tags, "wave", runtime->wave);
    if(status != MC_OK) return status;
    status = set_unsigned_tag(tags, "tick", runtime->tick);
    if(status != MC_OK) return status;
    status = set_real_tag(tags, "wavetime", runtime->wave_time);
    if(status != MC_OK) return status;
    status = set_signed_tag(tags, "playerteam", runtime->player_team);
    if(status != MC_OK) return status;
    status = tags_set(tags, "nocores", runtime->no_cores ? "true" : "false");
    if(status != MC_OK) return status;
    status = tags_set(tags, "mapname", runtime->map_name == NULL ? "Unknown" : runtime->map_name);
    if(status != MC_OK) return status;
    status = tags_set(tags, "rules", runtime->rules_json == NULL ? "{}" : runtime->rules_json);
    if(status != MC_OK) return status;
    status = tags_set(tags, "stats", runtime->stats_json == NULL ? "{}" : runtime->stats_json);
    if(status != MC_OK) return status;
    status = tags_set(tags, "locales", runtime->locales_json == NULL ? "{}" : runtime->locales_json);
    if(status != MC_OK) return status;
    status = tags_set(tags, "mods", runtime->mods_json == NULL ? "[]" : runtime->mods_json);
    if(status != MC_OK) return status;
    status = tags_set(tags, "sectorPreset", runtime->sector_preset == NULL ? "" : runtime->sector_preset);
    if(status != MC_OK) return status;
    status = tags_set(tags, "viewpos", runtime->view_position == NULL ? "0,0" : runtime->view_position);
    if(status != MC_OK) return status;
    return tags_set(tags, "controlledType", runtime->controlled_type == NULL ? "" : runtime->controlled_type);
}

static void gameplay_recount_teams(McGameplay *gameplay){
    gameplay_team_reset(gameplay);
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        McEntity *entity = &gameplay->simulation.entities[i];
        if(!entity->active || entity->team >= MC_TEAM_COUNT) continue;
        gameplay->teams[entity->team].active = true;
        if(entity->kind == MC_ENTITY_BUILDING) gameplay->teams[entity->team].building_count++;
    }
    if(gameplay->runtime.player_team >= 0 && gameplay->runtime.player_team < (int32_t)MC_TEAM_COUNT){
        gameplay->teams[gameplay->runtime.player_team].active = true;
    }
}

static McStatus replace_bytes(uint8_t **destination, size_t *destination_size, const uint8_t *data, size_t size){
    if(destination == NULL || destination_size == NULL || (data == NULL && size != 0)) return MC_INVALID_ARGUMENT;
    uint8_t *copy = NULL;
    if(size != 0){
        copy = malloc(size);
        if(copy == NULL) return MC_OUT_OF_MEMORY;
        memcpy(copy, data, size);
    }
    free(*destination);
    *destination = copy;
    *destination_size = size;
    return MC_OK;
}

static uint16_t block_size_or_one(uint16_t block){
    uint16_t size = mc_block_size((McBlockId)block);
    return size == 0 ? 1 : size;
}

static McEntity *find_building_at(McGameplay *gameplay, uint16_t x, uint16_t y){
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        McEntity *entity = &gameplay->simulation.entities[i];
        if(!entity->active || entity->kind != MC_ENTITY_BUILDING) continue;
        uint16_t size = block_size_or_one((uint16_t)entity->block);
        if(x >= entity->tile_x && y >= entity->tile_y && x - entity->tile_x < size && y - entity->tile_y < size){
            return entity;
        }
    }
    return NULL;
}

static McStatus restore_map_buildings(McGameplay *gameplay){
    McTeam team = MC_TEAM_SHARDED;
    if(gameplay->runtime.player_team >= 0 && gameplay->runtime.player_team < (int32_t)MC_TEAM_COUNT){
        team = (McTeam)gameplay->runtime.player_team;
    }
    size_t total = (size_t)gameplay->save.map.width * gameplay->save.map.height;
    for(size_t i = 0; i < total; i++){
        McMapTileRecord *tile = &gameplay->save.map.tiles[i];
        if((tile->flags & 1u) == 0 || !tile->entity_center) continue;
        uint16_t x = (uint16_t)(i % gameplay->save.map.width);
        uint16_t y = (uint16_t)(i / gameplay->save.map.width);
        McEntity *entity = mc_simulation_spawn(&gameplay->simulation, MC_ENTITY_BUILDING, team,
            ((float)x + 0.5f) * MC_TILE_SIZE, ((float)y + 0.5f) * MC_TILE_SIZE);
        if(entity == NULL) return MC_CAPACITY_EXCEEDED;
        entity->tile_x = x;
        entity->tile_y = y;
        entity->block = (McBlockId)tile->tile.block;
        uint16_t block_size = block_size_or_one(tile->tile.block);
        entity->position.x = ((float)x + (float)block_size * 0.5f) * MC_TILE_SIZE;
        entity->position.y = ((float)y + (float)block_size * 0.5f) * MC_TILE_SIZE;
        entity->health = mc_entity_default_health(MC_ENTITY_BUILDING, entity->block);
        entity->max_health = entity->health;
        McStatus status = mc_simulation_attach_entity_data(entity, 0, tile->entity_data, tile->entity_size);
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

static McStatus restore_world_entities(McGameplay *gameplay){
    for(size_t i = 0; i < gameplay->save.entity_data.record_count; i++){
        const McEntityRecord *record = &gameplay->save.entity_data.records[i];
        McEntity *entity = mc_simulation_spawn_with_id(&gameplay->simulation, MC_ENTITY_UNKNOWN,
            MC_TEAM_SHARDED, record->id, 0.0f, 0.0f);
        if(entity == NULL) return MC_CAPACITY_EXCEEDED;
        entity->class_id = record->class_id;
        McStatus status = mc_simulation_attach_entity_data(entity, record->class_id, record->data, record->size);
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

static void clear_runtime_save(McGameplay *gameplay){
    mc_save_file_destroy(&gameplay->save);
    runtime_destroy(&gameplay->runtime);
    gameplay->counters = (McGameplayCounters){0};
}

McStatus mc_gameplay_init(McGameplay *gameplay, uint16_t width, uint16_t height, uint64_t seed){
    if(gameplay == NULL) return MC_INVALID_ARGUMENT;
    *gameplay = (McGameplay){0};
    McStatus status = mc_simulation_init(&gameplay->simulation, width, height, seed);
    if(status != MC_OK) return status;
    runtime_reset(&gameplay->runtime);
    if(gameplay->runtime.rules_json == NULL || gameplay->runtime.stats_json == NULL ||
       gameplay->runtime.locales_json == NULL || gameplay->runtime.mods_json == NULL ||
       gameplay->runtime.sector_preset == NULL || gameplay->runtime.view_position == NULL ||
       gameplay->runtime.controlled_type == NULL){
        mc_gameplay_destroy(gameplay);
        return MC_OUT_OF_MEMORY;
    }
    gameplay->runtime.map_name = duplicate_string("Unknown");
    if(gameplay->runtime.map_name == NULL){
        mc_gameplay_destroy(gameplay);
        return MC_OUT_OF_MEMORY;
    }
    gameplay_team_reset(gameplay);
    gameplay->initialized = true;
    return MC_OK;
}

void mc_gameplay_destroy(McGameplay *gameplay){
    if(gameplay == NULL) return;
    mc_save_file_destroy(&gameplay->save);
    mc_simulation_destroy(&gameplay->simulation);
    runtime_destroy(&gameplay->runtime);
    *gameplay = (McGameplay){0};
}

McStatus mc_gameplay_adopt_save(McGameplay *gameplay, McSaveFile *save,
                                const McContentRemap *remap, bool unknown_to_air){
    if(gameplay == NULL || save == NULL || save->map.tiles == NULL || save->map.width == 0 || save->map.height == 0){
        return MC_INVALID_ARGUMENT;
    }
    if(!gameplay->initialized){
        McStatus init_status = mc_gameplay_init(gameplay, save->map.width, save->map.height, 1);
        if(init_status != MC_OK) return init_status;
    }
    McSaveFile incoming = *save;
    *save = (McSaveFile){0};
    clear_runtime_save(gameplay);
    gameplay->save = incoming;

    McStatus status = runtime_read_metadata(gameplay);
    if(status != MC_OK) return status;
    if(remap != NULL){
        status = mc_map_section_apply_content_remap(&gameplay->save.map, remap, unknown_to_air);
        if(status != MC_OK) return status;
    }

    mc_simulation_reset_runtime(&gameplay->simulation, 1);
    mc_world_destroy(&gameplay->simulation.world);
    status = mc_map_section_copy_to_world(&gameplay->save.map, &gameplay->simulation.world);
    if(status != MC_OK) return status;
    gameplay->simulation.tick = gameplay->runtime.tick;
    gameplay->simulation.wave = gameplay->runtime.wave == 0 ? 1 : gameplay->runtime.wave;
    gameplay->simulation.wave_time = gameplay->runtime.wave_time;
    gameplay->simulation.paused = false;
    status = restore_world_entities(gameplay);
    if(status == MC_OK) status = restore_map_buildings(gameplay);
    if(status != MC_OK) return status;
    gameplay_recount_teams(gameplay);
    gameplay->initialized = true;
    return MC_OK;
}

McStatus mc_gameplay_load(McGameplay *gameplay, const uint8_t *compressed, size_t size,
                          const McContentRemap *remap, bool unknown_to_air){
    if(gameplay == NULL || compressed == NULL || size == 0) return MC_INVALID_ARGUMENT;
    McSaveFile save = {0};
    McStatus status = mc_save_file_load(compressed, size, &save);
    if(status != MC_OK) return status;
    status = mc_gameplay_adopt_save(gameplay, &save, remap, unknown_to_air);
    if(status != MC_OK) mc_save_file_destroy(&save);
    return status;
}

static McStatus sync_map_from_world(McGameplay *gameplay){
    McMapSection *map = &gameplay->save.map;
    McWorld *world = &gameplay->simulation.world;
    if(map->tiles == NULL || world->tiles == NULL || map->width != world->width || map->height != world->height){
        return MC_INVALID_ARGUMENT;
    }
    size_t total = (size_t)map->width * map->height;
    for(size_t i = 0; i < total; i++){
        map->tiles[i].tile = world->tiles[i];
        if(world->tile_data != NULL){
            map->tiles[i].data = world->tile_data[i].data;
            map->tiles[i].floor_data = world->tile_data[i].floor_data;
            map->tiles[i].overlay_data = world->tile_data[i].overlay_data;
            map->tiles[i].extra_data = world->tile_data[i].extra_data;
        }
        if(map->tiles[i].tile.block == MC_BLOCK_AIR){
            map->tiles[i].flags = 0;
            map->tiles[i].entity_center = false;
            free(map->tiles[i].entity_data);
            map->tiles[i].entity_data = NULL;
            map->tiles[i].entity_size = 0;
        }
    }
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        McEntity *entity = &gameplay->simulation.entities[i];
        if(!entity->active || entity->kind != MC_ENTITY_BUILDING) continue;
        if(entity->tile_x >= map->width || entity->tile_y >= map->height) continue;
        size_t index = (size_t)entity->tile_y * map->width + entity->tile_x;
        McMapTileRecord *tile = &map->tiles[index];
        tile->flags |= 1u;
        tile->entity_center = true;
        if(entity->save_size != 0){
            McStatus status = replace_bytes(&tile->entity_data, &tile->entity_size, entity->save_data, entity->save_size);
            if(status != MC_OK) return status;
        }else if(tile->entity_size == 0){
            static const uint8_t empty_revision[] = {0};
            McStatus status = replace_bytes(&tile->entity_data, &tile->entity_size, empty_revision, sizeof(empty_revision));
            if(status != MC_OK) return status;
        }
    }
    return MC_OK;
}

static McStatus sync_entities_from_simulation(McGameplay *gameplay){
    McEntitiesRegion *region = &gameplay->save.entity_data;
    size_t count = 0;
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        const McEntity *entity = &gameplay->simulation.entities[i];
        if(entity->active && entity->kind != MC_ENTITY_BUILDING) count++;
    }
    McEntityRecord *records = count == 0 ? NULL : calloc(count, sizeof(*records));
    if(count != 0 && records == NULL) return MC_OUT_OF_MEMORY;
    size_t position = 0;
    McStatus status = MC_OK;
    for(size_t i = 0; i < MC_MAX_ENTITIES && status == MC_OK; i++){
        const McEntity *entity = &gameplay->simulation.entities[i];
        if(!entity->active || entity->kind == MC_ENTITY_BUILDING) continue;
        records[position].class_id = entity->class_id;
        records[position].id = entity->id;
        if(entity->save_size != 0){
            records[position].data = malloc(entity->save_size);
            if(records[position].data == NULL){ status = MC_OUT_OF_MEMORY; break; }
            memcpy(records[position].data, entity->save_data, entity->save_size);
            records[position].size = entity->save_size;
        }
        position++;
    }
    if(status != MC_OK){
        for(size_t i = 0; i < count; i++) free(records[i].data);
        free(records);
        return status;
    }
    for(size_t i = 0; i < region->record_count; i++) free(region->records[i].data);
    free(region->records);
    region->records = records;
    region->record_count = count;
    free(gameplay->save.entities.data);
    gameplay->save.entities.data = NULL;
    gameplay->save.entities.size = 0;
    return MC_OK;
}

McStatus mc_gameplay_save_metadata(McGameplay *gameplay){
    if(gameplay == NULL || !gameplay->initialized) return MC_INVALID_ARGUMENT;
    gameplay->runtime.tick = gameplay->simulation.tick;
    gameplay->runtime.wave = gameplay->simulation.wave;
    gameplay->runtime.wave_time = gameplay->simulation.wave_time;
    return runtime_write_metadata(gameplay);
}

McStatus mc_gameplay_write(McGameplay *gameplay, McBuffer *compressed){
    if(gameplay == NULL || compressed == NULL || !gameplay->initialized || gameplay->save.map.tiles == NULL){
        return MC_INVALID_ARGUMENT;
    }
    McStatus status = sync_map_from_world(gameplay);
    if(status != MC_OK) return status;
    status = sync_entities_from_simulation(gameplay);
    if(status != MC_OK) return status;
    status = mc_gameplay_save_metadata(gameplay);
    if(status != MC_OK) return status;
    return mc_save_file_write(&gameplay->save, compressed);
}

McStatus mc_gameplay_step(McGameplay *gameplay){
    if(gameplay == NULL || !gameplay->initialized) return MC_INVALID_ARGUMENT;
    uint32_t old_wave = gameplay->simulation.wave;
    McStatus status = mc_simulation_step(&gameplay->simulation);
    if(status != MC_OK) return status;
    gameplay->counters.steps++;
    if(gameplay->simulation.wave != old_wave){
        gameplay->counters.waves_started += (uint64_t)(gameplay->simulation.wave - old_wave);
    }
    gameplay->runtime.tick = gameplay->simulation.tick;
    gameplay->runtime.wave = gameplay->simulation.wave;
    gameplay->runtime.wave_time = gameplay->simulation.wave_time;
    return MC_OK;
}

void mc_gameplay_set_paused(McGameplay *gameplay, bool paused){
    if(gameplay == NULL || !gameplay->initialized) return;
    gameplay->simulation.paused = paused;
}

McStatus mc_gameplay_set_wave(McGameplay *gameplay, uint32_t wave, float wave_time){
    if(gameplay == NULL || !gameplay->initialized || wave == 0 || wave_time < 0.0f) return MC_INVALID_ARGUMENT;
    gameplay->simulation.wave = wave;
    gameplay->simulation.wave_time = wave_time;
    gameplay->runtime.wave = wave;
    gameplay->runtime.wave_time = wave_time;
    return MC_OK;
}

McStatus mc_gameplay_set_player_team(McGameplay *gameplay, int32_t team){
    if(gameplay == NULL || !gameplay->initialized || team < 0 || team >= (int32_t)MC_TEAM_COUNT){
        return MC_INVALID_ARGUMENT;
    }
    gameplay->runtime.player_team = team;
    gameplay->teams[team].active = true;
    return MC_OK;
}

const McGameplayRuntime *mc_gameplay_runtime(const McGameplay *gameplay){
    return gameplay == NULL ? NULL : &gameplay->runtime;
}

const McGameplayCounters *mc_gameplay_counters(const McGameplay *gameplay){
    return gameplay == NULL ? NULL : &gameplay->counters;
}

const McGameplayTeamState *mc_gameplay_team(const McGameplay *gameplay, McTeam team){
    if(gameplay == NULL || team >= MC_TEAM_COUNT) return NULL;
    return &gameplay->teams[team];
}

McTile *mc_gameplay_tile(McGameplay *gameplay, uint16_t x, uint16_t y){
    return gameplay == NULL ? NULL : mc_world_tile(&gameplay->simulation.world, x, y);
}

const McTile *mc_gameplay_tile_const(const McGameplay *gameplay, uint16_t x, uint16_t y){
    return gameplay == NULL ? NULL : mc_world_tile_const(&gameplay->simulation.world, x, y);
}

McTileData *mc_gameplay_tile_data(McGameplay *gameplay, uint16_t x, uint16_t y){
    return gameplay == NULL ? NULL : mc_world_tile_data(&gameplay->simulation.world, x, y);
}

const McTileData *mc_gameplay_tile_data_const(const McGameplay *gameplay, uint16_t x, uint16_t y){
    return gameplay == NULL ? NULL : mc_world_tile_data_const(&gameplay->simulation.world, x, y);
}

McStatus mc_gameplay_place_block(McGameplay *gameplay, McBlockId block, McTeam team, uint16_t x, uint16_t y){
    if(gameplay == NULL || !gameplay->initialized) return MC_INVALID_ARGUMENT;
    McStatus status = mc_simulation_place_block(&gameplay->simulation, block, team, x, y);
    if(status != MC_OK) return status;
    gameplay->counters.blocks_placed++;
    gameplay->counters.entities_spawned++;
    gameplay->teams[team].active = true;
    gameplay->teams[team].building_count++;
    return MC_OK;
}

McStatus mc_gameplay_remove_block(McGameplay *gameplay, uint16_t x, uint16_t y){
    if(gameplay == NULL || !gameplay->initialized) return MC_INVALID_ARGUMENT;
    McTile *target = mc_world_tile(&gameplay->simulation.world, x, y);
    if(target == NULL || target->block == MC_BLOCK_AIR) return MC_NOT_FOUND;
    McEntity *building = find_building_at(gameplay, x, y);
    uint16_t root_x = x, root_y = y;
    uint16_t size = block_size_or_one(target->block);
    McTeam team = MC_TEAM_SHARDED;
    McEntityId id = MC_ENTITY_NONE;
    if(building != NULL){
        root_x = building->tile_x;
        root_y = building->tile_y;
        size = block_size_or_one((uint16_t)building->block);
        team = building->team;
        id = building->id;
    }
    if(size > gameplay->simulation.world.width || size > gameplay->simulation.world.height ||
       root_x > gameplay->simulation.world.width - size || root_y > gameplay->simulation.world.height - size){
        return MC_FORMAT_ERROR;
    }
    for(uint16_t dy = 0; dy < size; dy++){
        for(uint16_t dx = 0; dx < size; dx++){
            McTile *tile = mc_world_tile(&gameplay->simulation.world, (uint16_t)(root_x + dx), (uint16_t)(root_y + dy));
            if(tile != NULL) tile->block = MC_BLOCK_AIR;
            McTileData *data = mc_world_tile_data(&gameplay->simulation.world, (uint16_t)(root_x + dx), (uint16_t)(root_y + dy));
            if(data != NULL) *data = (McTileData){0};
        }
    }
    if(id != MC_ENTITY_NONE){
        mc_simulation_destroy_entity(&gameplay->simulation, id);
        if(team < MC_TEAM_COUNT && gameplay->teams[team].building_count > 0) gameplay->teams[team].building_count--;
    }
    gameplay->counters.blocks_removed++;
    return MC_OK;
}

McEntity *mc_gameplay_spawn(McGameplay *gameplay, McEntityKind kind, McTeam team, float x, float y){
    if(gameplay == NULL || !gameplay->initialized) return NULL;
    McEntity *entity = mc_simulation_spawn(&gameplay->simulation, kind, team, x, y);
    if(entity != NULL){
        gameplay->counters.entities_spawned++;
        gameplay->teams[team].active = true;
    }
    return entity;
}

McEntity *mc_gameplay_spawn_raw(McGameplay *gameplay, McEntityKind kind, McTeam team, McEntityId id,
                                float x, float y, uint8_t class_id, const uint8_t *data, size_t size){
    if(gameplay == NULL || !gameplay->initialized) return NULL;
    McEntity *entity = mc_simulation_spawn_with_id(&gameplay->simulation, kind, team, id, x, y);
    if(entity == NULL) return NULL;
    if(mc_simulation_attach_entity_data(entity, class_id, data, size) != MC_OK){
        mc_simulation_destroy_entity(&gameplay->simulation, id);
        return NULL;
    }
    gameplay->counters.entities_spawned++;
    gameplay->teams[team].active = true;
    return entity;
}

McStatus mc_gameplay_destroy_entity(McGameplay *gameplay, McEntityId id){
    if(gameplay == NULL || !gameplay->initialized) return MC_INVALID_ARGUMENT;
    McEntity *entity = mc_simulation_find(&gameplay->simulation, id);
    if(entity == NULL) return MC_NOT_FOUND;
    McTeam team = entity->team;
    if(entity->kind == MC_ENTITY_BUILDING && team < MC_TEAM_COUNT && gameplay->teams[team].building_count > 0){
        gameplay->teams[team].building_count--;
    }
    mc_simulation_destroy_entity(&gameplay->simulation, id);
    gameplay->counters.entities_destroyed++;
    return MC_OK;
}

McEntity *mc_gameplay_find_entity(McGameplay *gameplay, McEntityId id){
    return gameplay == NULL ? NULL : mc_simulation_find(&gameplay->simulation, id);
}

McStatus mc_gameplay_damage_entity(McGameplay *gameplay, McEntityId id, float amount, McTeam source_team){
    if(gameplay == NULL || !gameplay->initialized || amount < 0.0f || source_team >= MC_TEAM_COUNT){
        return MC_INVALID_ARGUMENT;
    }
    McEntity *entity = mc_simulation_find(&gameplay->simulation, id);
    if(entity == NULL) return MC_NOT_FOUND;
    entity->health -= amount;
    gameplay->counters.damage_dealt += (uint64_t)amount;
    if(entity->health <= 0.0f){
        entity->health = 0.0f;
        return mc_gameplay_destroy_entity(gameplay, id);
    }
    return MC_OK;
}

McStatus mc_gameplay_heal_entity(McGameplay *gameplay, McEntityId id, float amount){
    if(gameplay == NULL || !gameplay->initialized || amount < 0.0f) return MC_INVALID_ARGUMENT;
    McEntity *entity = mc_simulation_find(&gameplay->simulation, id);
    if(entity == NULL) return MC_NOT_FOUND;
    entity->health += amount;
    if(entity->health > entity->max_health) entity->health = entity->max_health;
    gameplay->counters.damage_received += (uint64_t)amount;
    return MC_OK;
}

McStatus mc_gameplay_add_items(McGameplay *gameplay, McTeam team, McItemId item, uint32_t amount){
    if(gameplay == NULL || !gameplay->initialized || team >= MC_TEAM_COUNT || item >= MC_ITEM_COUNT) return MC_INVALID_ARGUMENT;
    uint32_t accepted = mc_inventory_add(&gameplay->simulation.team_inventory[team], item, amount);
    gameplay->teams[team].inventory = gameplay->simulation.team_inventory[team];
    gameplay->teams[team].active = true;
    gameplay->counters.items_added += accepted;
    return accepted == amount ? MC_OK : MC_CAPACITY_EXCEEDED;
}

McStatus mc_gameplay_remove_items(McGameplay *gameplay, McTeam team, McItemId item, uint32_t amount){
    if(gameplay == NULL || !gameplay->initialized || team >= MC_TEAM_COUNT || item >= MC_ITEM_COUNT) return MC_INVALID_ARGUMENT;
    uint32_t removed = mc_inventory_remove(&gameplay->simulation.team_inventory[team], item, amount);
    gameplay->teams[team].inventory = gameplay->simulation.team_inventory[team];
    gameplay->counters.items_removed += removed;
    return removed == amount ? MC_OK : MC_NOT_FOUND;
}

McStatus mc_gameplay_transfer_items(McGameplay *gameplay, McTeam from, McTeam to, McItemId item, uint32_t amount){
    if(gameplay == NULL || !gameplay->initialized || from >= MC_TEAM_COUNT || to >= MC_TEAM_COUNT || item >= MC_ITEM_COUNT){
        return MC_INVALID_ARGUMENT;
    }
    uint32_t available = mc_inventory_count(&gameplay->simulation.team_inventory[from], item);
    uint32_t source_amount = available < amount ? available : amount;
    uint32_t destination_space = mc_inventory_space(&gameplay->simulation.team_inventory[to]);
    uint32_t moved = source_amount < destination_space ? source_amount : destination_space;
    mc_inventory_transfer(&gameplay->simulation.team_inventory[from], &gameplay->simulation.team_inventory[to], item, moved);
    gameplay->teams[from].inventory = gameplay->simulation.team_inventory[from];
    gameplay->teams[to].inventory = gameplay->simulation.team_inventory[to];
    gameplay->counters.items_removed += moved;
    gameplay->counters.items_added += moved;
    return moved == amount ? MC_OK : MC_CAPACITY_EXCEEDED;
}

McStatus mc_gameplay_set_team_active(McGameplay *gameplay, McTeam team, bool active){
    if(gameplay == NULL || !gameplay->initialized || team >= MC_TEAM_COUNT) return MC_INVALID_ARGUMENT;
    gameplay->teams[team].active = active;
    return MC_OK;
}

McStatus mc_gameplay_set_team_cores(McGameplay *gameplay, McTeam team, uint32_t cores){
    if(gameplay == NULL || !gameplay->initialized || team >= MC_TEAM_COUNT) return MC_INVALID_ARGUMENT;
    gameplay->teams[team].core_count = cores;
    gameplay->teams[team].active = true;
    return MC_OK;
}

const char *mc_gameplay_get_metadata(const McGameplay *gameplay, const char *key){
    if(gameplay == NULL || !gameplay->initialized) return NULL;
    return tag_value(&gameplay->save.meta, key);
}

McStatus mc_gameplay_set_metadata(McGameplay *gameplay, const char *key, const char *value){
    if(gameplay == NULL || !gameplay->initialized || key == NULL || value == NULL) return MC_INVALID_ARGUMENT;
    McStatus status = tags_set(&gameplay->save.meta, key, value);
    if(status != MC_OK) return status;
    if(strcmp(key, "mapname") == 0) return runtime_set_string(&gameplay->runtime.map_name, value);
    if(strcmp(key, "rules") == 0) return runtime_set_string(&gameplay->runtime.rules_json, value);
    if(strcmp(key, "stats") == 0) return runtime_set_string(&gameplay->runtime.stats_json, value);
    if(strcmp(key, "locales") == 0) return runtime_set_string(&gameplay->runtime.locales_json, value);
    if(strcmp(key, "mods") == 0) return runtime_set_string(&gameplay->runtime.mods_json, value);
    if(strcmp(key, "sectorPreset") == 0) return runtime_set_string(&gameplay->runtime.sector_preset, value);
    if(strcmp(key, "viewpos") == 0) return runtime_set_string(&gameplay->runtime.view_position, value);
    if(strcmp(key, "controlledType") == 0) return runtime_set_string(&gameplay->runtime.controlled_type, value);
    return MC_OK;
}
