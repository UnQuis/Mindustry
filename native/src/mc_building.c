#include "mindustry_building.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const McBuildingRecipe recipes[] = {
    {0, MC_BUILDING_GENERIC, MC_ITEM_COPPER, MC_ITEM_COPPER, 0, 0, MC_LIQUID_WATER, 0.0f, 1.0f, 0.0f, true},
    {1, MC_BUILDING_FACTORY, MC_ITEM_COPPER, MC_ITEM_GRAPHITE, 2, 1, MC_LIQUID_WATER, 0.0f, 60.0f, 1.0f, true},
    {2, MC_BUILDING_FACTORY, MC_ITEM_LEAD, MC_ITEM_SILICON, 2, 1, MC_LIQUID_WATER, 0.0f, 90.0f, 1.5f, true},
    {3, MC_BUILDING_FACTORY, MC_ITEM_TITANIUM, MC_ITEM_PLASTANIUM, 2, 1, MC_LIQUID_OIL, 0.25f, 120.0f, 2.0f, true},
    {4, MC_BUILDING_POWER, MC_ITEM_COAL, MC_ITEM_COAL, 1, 0, MC_LIQUID_WATER, 0.0f, 30.0f, 0.25f, true},
    {5, MC_BUILDING_FACTORY, MC_ITEM_THORIUM, MC_ITEM_PHASE_FABRIC, 2, 1, MC_LIQUID_CRYOFLUID, 0.5f, 180.0f, 3.0f, true},
    {6, MC_BUILDING_FACTORY, MC_ITEM_COPPER, MC_ITEM_SURGE_ALLOY, 3, 1, MC_LIQUID_WATER, 0.1f, 150.0f, 2.5f, true},
    {7, MC_BUILDING_FACTORY, MC_ITEM_BERYLLIUM, MC_ITEM_CARBIDE, 2, 1, MC_LIQUID_OIL, 0.4f, 180.0f, 3.0f, true}
};

static McStatus reserve_states(McBuildingStore *store, size_t needed){
    if(needed <= store->capacity) return MC_OK;
    size_t capacity = store->capacity == 0 ? 16 : store->capacity;
    while(capacity < needed){
        if(capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    if(capacity > SIZE_MAX / sizeof(*store->states)) return MC_CAPACITY_EXCEEDED;
    McBuildingState *states = realloc(store->states, capacity * sizeof(*states));
    if(states == NULL) return MC_OUT_OF_MEMORY;
    store->states = states;
    store->capacity = capacity;
    return MC_OK;
}

static McStatus reserve_links(McBuildingStore *store, size_t needed){
    if(needed > MC_BUILDING_MAX_LINKS) return MC_CAPACITY_EXCEEDED;
    if(needed <= store->link_capacity) return MC_OK;
    size_t capacity = store->link_capacity == 0 ? 32 : store->link_capacity;
    while(capacity < needed){
        if(capacity > SIZE_MAX / 2) { capacity = needed; break; }
        capacity *= 2;
    }
    if(capacity > SIZE_MAX / sizeof(*store->links)) return MC_CAPACITY_EXCEEDED;
    McBuildingLink *links = realloc(store->links, capacity * sizeof(*links));
    if(links == NULL) return MC_OUT_OF_MEMORY;
    store->links = links;
    store->link_capacity = capacity;
    return MC_OK;
}

McBuildingKind mc_building_kind_for_block(McBlockId block){
    switch(block){
        case MC_BLOCK_CORE_SHARD: return MC_BUILDING_CORE;
        case MC_BLOCK_MECHANICAL_DRILL: return MC_BUILDING_DRILL;
        case MC_BLOCK_CONVEYOR: return MC_BUILDING_CONVEYOR;
        case MC_BLOCK_DUO: return MC_BUILDING_TURRET;
        case MC_BLOCK_AIR: return MC_BUILDING_GENERIC;
        default: return MC_BUILDING_GENERIC;
    }
}

const char *mc_building_kind_name(McBuildingKind kind){
    switch(kind){
        case MC_BUILDING_GENERIC: return "generic";
        case MC_BUILDING_CORE: return "core";
        case MC_BUILDING_DRILL: return "drill";
        case MC_BUILDING_CONVEYOR: return "conveyor";
        case MC_BUILDING_TURRET: return "turret";
        case MC_BUILDING_FACTORY: return "factory";
        case MC_BUILDING_POWER: return "power";
        case MC_BUILDING_LIQUID: return "liquid";
        case MC_BUILDING_PROCESSOR: return "processor";
        case MC_BUILDING_STORAGE: return "storage";
        case MC_BUILDING_LAUNCHER: return "launcher";
        default: return "unknown";
    }
}

uint16_t mc_building_default_size(McBlockId block){
    uint16_t size = mc_block_size(block);
    return size == 0 ? 1 : size;
}

float mc_building_default_health(McBlockId block){
    uint32_t health = mc_block_health(block);
    return health == 0 ? 1.0f : (float)health;
}

float mc_building_default_capacity(McBuildingKind kind){
    switch(kind){
        case MC_BUILDING_CORE: return 10000.0f;
        case MC_BUILDING_STORAGE: return 2000.0f;
        case MC_BUILDING_POWER: return 1000.0f;
        case MC_BUILDING_LIQUID: return 120.0f;
        case MC_BUILDING_DRILL: return 50.0f;
        case MC_BUILDING_FACTORY: return 100.0f;
        case MC_BUILDING_TURRET: return 30.0f;
        case MC_BUILDING_CONVEYOR: return 0.0f;
        case MC_BUILDING_PROCESSOR: return 0.0f;
        case MC_BUILDING_LAUNCHER: return 500.0f;
        case MC_BUILDING_GENERIC: return 20.0f;
        default: return 20.0f;
    }
}

McBuildingRecipe mc_building_recipe(uint16_t recipe){
    for(size_t i = 0; i < sizeof(recipes) / sizeof(recipes[0]); i++){
        if(recipes[i].id == recipe) return recipes[i];
    }
    return (McBuildingRecipe){0};
}

size_t mc_building_recipe_count(void){
    return sizeof(recipes) / sizeof(recipes[0]);
}

void mc_building_state_destroy(McBuildingState *state){
    if(state == NULL) return;
    free(state->config);
    *state = (McBuildingState){0};
}

McStatus mc_building_state_init(McBuildingState *state, McEntityId entity_id, McBlockId block,
                                McTeam team, uint16_t tile_x, uint16_t tile_y){
    if(state == NULL || entity_id == MC_ENTITY_NONE || team >= MC_TEAM_COUNT) return MC_INVALID_ARGUMENT;
    *state = (McBuildingState){0};
    state->entity_id = entity_id;
    state->block = block;
    state->kind = mc_building_kind_for_block(block);
    state->team = team;
    state->tile_x = tile_x;
    state->tile_y = tile_y;
    state->size = mc_building_default_size(block);
    state->health = mc_building_default_health(block);
    state->max_health = state->health;
    state->build_progress = 1.0f;
    state->efficiency = 1.0f;
    state->time_scale = 1.0f;
    state->enabled = true;
    state->active = true;
    state->inventory.capacity = (uint32_t)mc_building_default_capacity(state->kind);
    state->liquids.capacity = mc_building_default_capacity(state->kind);
    state->power.capacity = mc_building_default_capacity(state->kind);
    state->power.satisfaction = 1.0f;
    state->production.speed_scale = 1.0f;
    state->drill.speed_scale = 1.0f;
    state->drill.drill_time = 60.0f;
    state->turret.range = state->kind == MC_BUILDING_TURRET ? 120.0f : 0.0f;
    state->turret.reload_time = state->kind == MC_BUILDING_TURRET ? 30.0f : 0.0f;
    state->conveyor.speed = state->kind == MC_BUILDING_CONVEYOR ? 1.0f : 0.0f;
    state->factory.craft_time = 60.0f;
    state->processor.range = state->kind == MC_BUILDING_PROCESSOR ? 10.0f : 0.0f;
    return MC_OK;
}

void mc_building_state_reset(McBuildingState *state){
    if(state == NULL) return;
    McEntityId id = state->entity_id;
    McBlockId block = state->block;
    McTeam team = state->team;
    uint16_t tile_x = state->tile_x;
    uint16_t tile_y = state->tile_y;
    mc_building_state_destroy(state);
    (void)mc_building_state_init(state, id, block, team, tile_x, tile_y);
}

McStatus mc_building_state_set_config(McBuildingState *state, const uint8_t *data, size_t size){
    if(state == NULL || (data == NULL && size != 0) || size > MC_BUILDING_MAX_CONFIG) return MC_INVALID_ARGUMENT;
    uint8_t *copy = NULL;
    if(size != 0){
        copy = malloc(size);
        if(copy == NULL) return MC_OUT_OF_MEMORY;
        memcpy(copy, data, size);
    }
    free(state->config);
    state->config = copy;
    state->config_size = size;
    return MC_OK;
}

McStatus mc_building_state_set_health(McBuildingState *state, float health, float max_health){
    if(state == NULL || !isfinite(health) || !isfinite(max_health) || max_health <= 0.0f || health < 0.0f){
        return MC_INVALID_ARGUMENT;
    }
    state->max_health = max_health;
    state->health = health > max_health ? max_health : health;
    state->dead = state->health <= 0.0f;
    state->active = !state->dead;
    return MC_OK;
}

McStatus mc_building_state_set_rotation(McBuildingState *state, uint16_t rotation){
    if(state == NULL || rotation > 3) return MC_INVALID_ARGUMENT;
    state->rotation = rotation;
    return MC_OK;
}

McStatus mc_building_state_set_enabled(McBuildingState *state, bool enabled){
    if(state == NULL || state->dead) return MC_INVALID_ARGUMENT;
    state->enabled = enabled;
    mc_building_recalculate_efficiency(state);
    return MC_OK;
}

McStatus mc_building_state_set_kind(McBuildingState *state, McBuildingKind kind){
    if(state == NULL || kind > MC_BUILDING_LAUNCHER) return MC_INVALID_ARGUMENT;
    state->kind = kind;
    state->inventory.capacity = (uint32_t)mc_building_default_capacity(kind);
    state->liquids.capacity = mc_building_default_capacity(kind);
    state->power.capacity = mc_building_default_capacity(kind);
    return MC_OK;
}

void mc_building_state_mark_dead(McBuildingState *state){
    if(state == NULL) return;
    state->dead = true;
    state->active = false;
    state->enabled = false;
    state->health = 0.0f;
    state->turret.shooting = false;
    state->turret.target = MC_ENTITY_NONE;
}

void mc_building_store_init(McBuildingStore *store){
    if(store == NULL) return;
    *store = (McBuildingStore){.next_network_id = 1};
}

void mc_building_store_destroy(McBuildingStore *store){
    if(store == NULL) return;
    for(size_t i = 0; i < store->count; i++) mc_building_state_destroy(&store->states[i]);
    free(store->states);
    free(store->links);
    *store = (McBuildingStore){0};
}

McStatus mc_building_store_clear(McBuildingStore *store){
    if(store == NULL) return MC_INVALID_ARGUMENT;
    for(size_t i = 0; i < store->count; i++) mc_building_state_destroy(&store->states[i]);
    store->count = 0;
    store->link_count = 0;
    store->tick = 0;
    store->produced_items = 0;
    store->mined_items = 0;
    store->transferred_items = 0;
    store->consumed_items = 0;
    store->power_ticks = 0;
    store->liquid_ticks = 0;
    store->turret_shots = 0;
    store->processor_steps = 0;
    return MC_OK;
}

McBuildingState *mc_building_store_find(McBuildingStore *store, McEntityId entity_id){
    if(store == NULL || entity_id == MC_ENTITY_NONE) return NULL;
    for(size_t i = 0; i < store->count; i++){
        if(store->states[i].entity_id == entity_id) return &store->states[i];
    }
    return NULL;
}

const McBuildingState *mc_building_store_find_const(const McBuildingStore *store, McEntityId entity_id){
    if(store == NULL || entity_id == MC_ENTITY_NONE) return NULL;
    for(size_t i = 0; i < store->count; i++){
        if(store->states[i].entity_id == entity_id) return &store->states[i];
    }
    return NULL;
}

McBuildingState *mc_building_store_find_tile(McBuildingStore *store, uint16_t tile_x, uint16_t tile_y){
    if(store == NULL) return NULL;
    for(size_t i = 0; i < store->count; i++){
        McBuildingState *state = &store->states[i];
        uint16_t size = state->size == 0 ? 1 : state->size;
        if(tile_x >= state->tile_x && tile_y >= state->tile_y && tile_x - state->tile_x < size && tile_y - state->tile_y < size){
            return state;
        }
    }
    return NULL;
}

const McBuildingState *mc_building_store_find_tile_const(const McBuildingStore *store, uint16_t tile_x, uint16_t tile_y){
    return mc_building_store_find_tile((McBuildingStore *)store, tile_x, tile_y);
}

McBuildingState *mc_building_store_add(McBuildingStore *store, McEntityId entity_id, McBlockId block,
                                        McTeam team, uint16_t tile_x, uint16_t tile_y){
    if(store == NULL || entity_id == MC_ENTITY_NONE || mc_building_store_find(store, entity_id) != NULL) return NULL;
    if(mc_building_store_find_tile(store, tile_x, tile_y) != NULL) return NULL;
    if(reserve_states(store, store->count + 1) != MC_OK) return NULL;
    McBuildingState *state = &store->states[store->count];
    if(mc_building_state_init(state, entity_id, block, team, tile_x, tile_y) != MC_OK) return NULL;
    store->count++;
    return state;
}

McStatus mc_building_store_remove(McBuildingStore *store, McEntityId entity_id){
    if(store == NULL) return MC_INVALID_ARGUMENT;
    for(size_t i = 0; i < store->count; i++){
        if(store->states[i].entity_id != entity_id) continue;
        mc_building_store_disconnect_entity(store, entity_id);
        mc_building_state_destroy(&store->states[i]);
        if(i + 1 < store->count) store->states[i] = store->states[store->count - 1];
        store->count--;
        return MC_OK;
    }
    return MC_NOT_FOUND;
}

McStatus mc_building_store_rebind_entity(McBuildingStore *store, uint16_t tile_x, uint16_t tile_y, McEntityId entity_id){
    McBuildingState *state = mc_building_store_find_tile(store, tile_x, tile_y);
    if(state == NULL || entity_id == MC_ENTITY_NONE) return MC_NOT_FOUND;
    if(mc_building_store_find(store, entity_id) != NULL && state->entity_id != entity_id) return MC_CAPACITY_EXCEEDED;
    state->entity_id = entity_id;
    return MC_OK;
}

McStatus mc_building_store_set_tick(McBuildingStore *store, uint64_t tick){
    if(store == NULL || tick < store->tick) return MC_INVALID_ARGUMENT;
    store->tick = tick;
    return MC_OK;
}

uint32_t mc_building_item_count(const McBuildingState *state, McItemId item){
    return state == NULL ? 0 : mc_inventory_count(&state->inventory, item);
}

uint32_t mc_building_item_space(const McBuildingState *state){
    return state == NULL ? 0 : mc_inventory_space(&state->inventory);
}

uint32_t mc_building_add_item(McBuildingState *state, McItemId item, uint32_t amount){
    if(state == NULL || !state->active) return 0;
    return mc_inventory_add(&state->inventory, item, amount);
}

uint32_t mc_building_remove_item(McBuildingState *state, McItemId item, uint32_t amount){
    if(state == NULL) return 0;
    return mc_inventory_remove(&state->inventory, item, amount);
}

float mc_building_liquid_amount(const McBuildingState *state, McLiquidId liquid){
    if(state == NULL || liquid >= MC_BUILDING_LIQUID_COUNT) return 0.0f;
    return state->liquids.amount[liquid];
}

float mc_building_liquid_space(const McBuildingState *state, McLiquidId liquid){
    if(state == NULL || liquid >= MC_BUILDING_LIQUID_COUNT) return 0.0f;
    float used = 0.0f;
    for(size_t i = 0; i < MC_BUILDING_LIQUID_COUNT; i++) used += state->liquids.amount[i];
    return used < state->liquids.capacity ? state->liquids.capacity - used : 0.0f;
}

float mc_building_add_liquid(McBuildingState *state, McLiquidId liquid, float amount){
    if(state == NULL || liquid >= MC_BUILDING_LIQUID_COUNT || amount <= 0.0f || !isfinite(amount)) return 0.0f;
    float space = mc_building_liquid_space(state, liquid);
    float accepted = amount < space ? amount : space;
    state->liquids.amount[liquid] += accepted;
    mc_building_recalculate_liquid_dominant(state);
    return accepted;
}

float mc_building_remove_liquid(McBuildingState *state, McLiquidId liquid, float amount){
    if(state == NULL || liquid >= MC_BUILDING_LIQUID_COUNT || amount <= 0.0f || !isfinite(amount)) return 0.0f;
    float removed = amount < state->liquids.amount[liquid] ? amount : state->liquids.amount[liquid];
    state->liquids.amount[liquid] -= removed;
    mc_building_recalculate_liquid_dominant(state);
    return removed;
}

McStatus mc_building_set_power(McBuildingState *state, float stored, float capacity){
    if(state == NULL || !isfinite(stored) || !isfinite(capacity) || capacity < 0.0f || stored < 0.0f) return MC_INVALID_ARGUMENT;
    state->power.capacity = capacity;
    state->power.stored = stored > capacity ? capacity : stored;
    state->power.satisfaction = capacity == 0.0f ? 1.0f : state->power.stored / capacity;
    mc_building_recalculate_efficiency(state);
    return MC_OK;
}

float mc_building_power_space(const McBuildingState *state){
    if(state == NULL || state->power.stored >= state->power.capacity) return 0.0f;
    return state->power.capacity - state->power.stored;
}

float mc_building_receive_power(McBuildingState *state, float amount){
    if(state == NULL || amount <= 0.0f || !isfinite(amount)) return 0.0f;
    float accepted = amount < mc_building_power_space(state) ? amount : mc_building_power_space(state);
    state->power.stored += accepted;
    state->power.satisfaction = state->power.capacity == 0.0f ? 1.0f : state->power.stored / state->power.capacity;
    return accepted;
}

float mc_building_consume_power(McBuildingState *state, float amount){
    if(state == NULL || amount <= 0.0f || !isfinite(amount)) return 0.0f;
    float consumed = amount < state->power.stored ? amount : state->power.stored;
    state->power.stored -= consumed;
    state->power.satisfaction = state->power.capacity == 0.0f ? 1.0f : state->power.stored / state->power.capacity;
    return consumed;
}

void mc_building_recalculate_liquid_dominant(McBuildingState *state){
    if(state == NULL) return;
    float maximum = 0.0f;
    uint8_t dominant = 0;
    for(uint8_t i = 0; i < MC_BUILDING_LIQUID_COUNT; i++){
        if(state->liquids.amount[i] > maximum){ maximum = state->liquids.amount[i]; dominant = i; }
    }
    state->liquids.dominant = dominant;
    state->liquids.connected = maximum > 0.0f;
}

void mc_building_recalculate_efficiency(McBuildingState *state){
    if(state == NULL){ return; }
    if(!state->active || state->dead || !state->enabled || state->build_progress <= 0.0f){
        state->efficiency = 0.0f;
        return;
    }
    float power = state->power.capacity == 0.0f ? 1.0f : state->power.satisfaction;
    if(power < 0.0f) power = 0.0f;
    if(power > 1.0f) power = 1.0f;
    state->efficiency = state->build_progress * power * (state->time_scale < 0.0f ? 0.0f : state->time_scale);
}

McStatus mc_building_set_recipe(McBuildingState *state, uint16_t recipe){
    if(state == NULL || !mc_building_recipe(recipe).valid) return MC_NOT_FOUND;
    McBuildingRecipe definition = mc_building_recipe(recipe);
    state->production.pending_recipe = recipe;
    state->production.recipe = recipe;
    state->factory.recipe = recipe;
    state->factory.craft_time = definition.craft_time;
    return MC_OK;
}

McStatus mc_building_set_drill(McBuildingState *state, uint16_t ore, uint16_t hardness, float drill_time){
    if(state == NULL || drill_time <= 0.0f || !isfinite(drill_time)) return MC_INVALID_ARGUMENT;
    state->drill.ore = ore;
    state->drill.hardness = hardness;
    state->drill.drill_time = drill_time;
    return MC_OK;
}

McStatus mc_building_set_turret(McBuildingState *state, float range, float reload_time){
    if(state == NULL || range < 0.0f || reload_time <= 0.0f || !isfinite(range) || !isfinite(reload_time)) return MC_INVALID_ARGUMENT;
    state->turret.range = range;
    state->turret.reload_time = reload_time;
    return MC_OK;
}

McStatus mc_building_set_conveyor(McBuildingState *state, float speed){
    if(state == NULL || speed < 0.0f || !isfinite(speed)) return MC_INVALID_ARGUMENT;
    state->conveyor.speed = speed;
    return MC_OK;
}

McStatus mc_building_set_factory(McBuildingState *state, float craft_time){
    if(state == NULL || craft_time <= 0.0f || !isfinite(craft_time)) return MC_INVALID_ARGUMENT;
    state->factory.craft_time = craft_time;
    return MC_OK;
}

McStatus mc_building_set_core(McBuildingState *state, uint32_t storage_capacity, uint32_t unit_capacity){
    if(state == NULL || storage_capacity == 0 || unit_capacity == 0) return MC_INVALID_ARGUMENT;
    state->core.storage_capacity = storage_capacity;
    state->core.unit_capacity = unit_capacity;
    state->inventory.capacity = storage_capacity;
    return MC_OK;
}

McStatus mc_building_set_processor(McBuildingState *state, uint32_t processor_id, float range){
    if(state == NULL || range < 0.0f || !isfinite(range)) return MC_INVALID_ARGUMENT;
    state->processor.processor_id = processor_id;
    state->processor.range = range;
    state->processor.enabled = true;
    return MC_OK;
}

McStatus mc_building_set_target(McBuildingState *state, McEntityId target){
    if(state == NULL || state->kind != MC_BUILDING_TURRET) return MC_INVALID_ARGUMENT;
    state->turret.target = target;
    state->turret.shooting = target != MC_ENTITY_NONE;
    return MC_OK;
}

McStatus mc_building_set_build_progress(McBuildingState *state, float progress){
    if(state == NULL || !isfinite(progress) || progress < 0.0f || progress > 1.0f) return MC_INVALID_ARGUMENT;
    state->build_progress = progress;
    mc_building_recalculate_efficiency(state);
    return MC_OK;
}

McStatus mc_building_set_time_scale(McBuildingState *state, float scale){
    if(state == NULL || !isfinite(scale) || scale < 0.0f) return MC_INVALID_ARGUMENT;
    state->time_scale = scale;
    mc_building_recalculate_efficiency(state);
    return MC_OK;
}

McBuildingLink *mc_building_store_find_link(McBuildingStore *store, McEntityId from, McEntityId to,
                                             McBuildingLinkKind kind){
    if(store == NULL) return NULL;
    for(size_t i = 0; i < store->link_count; i++){
        McBuildingLink *link = &store->links[i];
        if(link->active && link->from == from && link->to == to && link->kind == kind) return link;
    }
    return NULL;
}

McStatus mc_building_store_connect(McBuildingStore *store, McEntityId from, McEntityId to,
                                   McBuildingLinkKind kind, float capacity){
    if(store == NULL || from == MC_ENTITY_NONE || to == MC_ENTITY_NONE || from == to || kind > MC_BUILDING_LINK_PAYLOAD ||
       capacity < 0.0f || !isfinite(capacity) || mc_building_store_find_const(store, from) == NULL ||
       mc_building_store_find_const(store, to) == NULL) return MC_INVALID_ARGUMENT;
    McBuildingLink *existing = mc_building_store_find_link(store, from, to, kind);
    if(existing != NULL){ existing->capacity = capacity; return MC_OK; }
    McStatus status = reserve_links(store, store->link_count + 1);
    if(status != MC_OK) return status;
    store->links[store->link_count++] = (McBuildingLink){
        .from = from, .to = to, .kind = kind, .capacity = capacity, .active = true
    };
    return mc_building_store_rebuild_networks(store);
}

McStatus mc_building_store_disconnect(McBuildingStore *store, McEntityId from, McEntityId to,
                                      McBuildingLinkKind kind){
    McBuildingLink *link = mc_building_store_find_link(store, from, to, kind);
    if(link == NULL) return MC_NOT_FOUND;
    link->active = false;
    return mc_building_store_rebuild_networks(store);
}

void mc_building_store_disconnect_entity(McBuildingStore *store, McEntityId entity_id){
    if(store == NULL) return;
    for(size_t i = 0; i < store->link_count; i++){
        if(store->links[i].from == entity_id || store->links[i].to == entity_id) store->links[i].active = false;
    }
    (void)mc_building_store_rebuild_networks(store);
}

static McStatus rebuild_resource_networks(McBuildingStore *store, bool power){
    uint32_t *network_ids = NULL;
    if(store->count != 0){
        network_ids = malloc(store->count * sizeof(*network_ids));
        if(network_ids == NULL) return MC_OUT_OF_MEMORY;
    }
    for(size_t i = 0; i < store->count; i++){
        network_ids[i] = 0;
        if(power) store->states[i].power.connected = false;
        else store->states[i].liquids.connected = false;
    }
    for(size_t start = 0; start < store->count; start++){
        if(network_ids[start] != 0) continue;
        uint32_t network = store->next_network_id++;
        if(network == 0) network = store->next_network_id++;
        size_t *queue = store->count == 0 ? NULL : malloc(store->count * sizeof(*queue));
        if(store->count != 0 && queue == NULL){ free(network_ids); return MC_OUT_OF_MEMORY; }
        size_t head = 0;
        size_t tail = 1;
        queue[0] = start;
        network_ids[start] = network;
        while(head < tail){
            size_t current = queue[head++];
            McEntityId id = store->states[current].entity_id;
            for(size_t link_index = 0; link_index < store->link_count; link_index++){
                McBuildingLink *link = &store->links[link_index];
                McBuildingLinkKind wanted = power ? MC_BUILDING_LINK_POWER : MC_BUILDING_LINK_LIQUID;
                if(!link->active || link->kind != wanted) continue;
                McEntityId other = MC_ENTITY_NONE;
                if(link->from == id) other = link->to;
                else if(link->to == id) other = link->from;
                if(other == MC_ENTITY_NONE) continue;
                for(size_t next = 0; next < store->count; next++){
                    if(store->states[next].entity_id != other) continue;
                    if(power) store->states[next].power.connected = true;
                    else store->states[next].liquids.connected = true;
                    if(network_ids[next] == 0){
                        network_ids[next] = network;
                        if(tail < store->count) queue[tail++] = next;
                    }
                    break;
                }
                if(power) store->states[current].power.connected = true;
                else store->states[current].liquids.connected = true;
            }
        }
        free(queue);
    }
    for(size_t i = 0; i < store->count; i++){
        if(power) store->states[i].power.network_id = network_ids[i];
        else store->states[i].liquids.network_id = network_ids[i];
    }
    free(network_ids);
    return MC_OK;
}

McStatus mc_building_store_rebuild_networks(McBuildingStore *store){
    if(store == NULL) return MC_INVALID_ARGUMENT;
    if(store->next_network_id == 0) store->next_network_id = 1;
    for(size_t i = 0; i < store->link_count; i++){
        McBuildingLink *link = &store->links[i];
        if(!link->active) continue;
        if(mc_building_store_find_const(store, link->from) == NULL || mc_building_store_find_const(store, link->to) == NULL){
            link->active = false;
        }
    }
    McStatus status = rebuild_resource_networks(store, true);
    if(status != MC_OK) return status;
    return rebuild_resource_networks(store, false);
}

McStatus mc_building_store_transfer_items(McBuildingStore *store, McEntityId from, McEntityId to,
                                          McItemId item, uint32_t amount){
    if(store == NULL || item >= MC_ITEM_COUNT) return MC_INVALID_ARGUMENT;
    McBuildingState *source = mc_building_store_find(store, from);
    McBuildingState *destination = mc_building_store_find(store, to);
    McBuildingLink *link = mc_building_store_find_link(store, from, to, MC_BUILDING_LINK_ITEM);
    if(source == NULL || destination == NULL || link == NULL) return MC_NOT_FOUND;
    uint32_t available = mc_building_item_count(source, item);
    uint32_t moved = amount < available ? amount : available;
    if(link->capacity > 0.0f && (float)moved > link->capacity) moved = (uint32_t)link->capacity;
    uint32_t accepted = mc_building_add_item(destination, item, moved);
    (void)mc_building_remove_item(source, item, accepted);
    link->transferred += (float)accepted;
    store->transferred_items += accepted;
    return accepted == amount ? MC_OK : MC_CAPACITY_EXCEEDED;
}

float mc_building_store_transfer_liquid(McBuildingStore *store, McEntityId from, McEntityId to,
                                        McLiquidId liquid, float amount){
    if(store == NULL || liquid >= MC_BUILDING_LIQUID_COUNT) return 0.0f;
    McBuildingState *source = mc_building_store_find(store, from);
    McBuildingState *destination = mc_building_store_find(store, to);
    McBuildingLink *link = mc_building_store_find_link(store, from, to, MC_BUILDING_LINK_LIQUID);
    if(source == NULL || destination == NULL || link == NULL) return 0.0f;
    float moved = mc_building_remove_liquid(source, liquid, amount);
    if(link->capacity > 0.0f && moved > link->capacity) moved = link->capacity;
    float accepted = mc_building_add_liquid(destination, liquid, moved);
    if(accepted < moved) (void)mc_building_add_liquid(source, liquid, moved - accepted);
    link->transferred += accepted;
    return accepted;
}

float mc_building_store_transfer_power(McBuildingStore *store, McEntityId from, McEntityId to, float amount){
    if(store == NULL) return 0.0f;
    McBuildingState *source = mc_building_store_find(store, from);
    McBuildingState *destination = mc_building_store_find(store, to);
    McBuildingLink *link = mc_building_store_find_link(store, from, to, MC_BUILDING_LINK_POWER);
    if(source == NULL || destination == NULL || link == NULL) return 0.0f;
    float moved = mc_building_consume_power(source, amount);
    if(link->capacity > 0.0f && moved > link->capacity) moved = link->capacity;
    float accepted = mc_building_receive_power(destination, moved);
    if(accepted < moved) (void)mc_building_receive_power(source, moved - accepted);
    link->transferred += accepted;
    return accepted;
}
