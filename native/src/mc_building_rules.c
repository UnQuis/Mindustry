#include "mindustry_building_rules.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const McBuildingRule building_rules[] = {
    {
        MC_BLOCK_AIR, MC_BUILDING_GENERIC, "air", 1, 0, 0, 0.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 0.0f, MC_BUILDING_CAP_NONE, false, false, false, false
    },
    {
        MC_BLOCK_CORE_SHARD, MC_BUILDING_CORE, "core-shard", 3, 1100, 10000, 0.0f, 1000.0f,
        0.0f, 0.0f, 1.0f, 24.0f,
        MC_BUILDING_CAP_INVENTORY | MC_BUILDING_CAP_CORE | MC_BUILDING_CAP_POWER |
        MC_BUILDING_CAP_CONFIG | MC_BUILDING_CAP_LINK | MC_BUILDING_CAP_DAMAGE | MC_BUILDING_CAP_TEAM,
        false, true, true, true
    },
    {
        MC_BLOCK_MECHANICAL_DRILL, MC_BUILDING_DRILL, "mechanical-drill", 2, 160, 50, 30.0f, 40.0f,
        0.0f, 0.25f, 1.0f, 8.0f,
        MC_BUILDING_CAP_INVENTORY | MC_BUILDING_CAP_LIQUID | MC_BUILDING_CAP_POWER |
        MC_BUILDING_CAP_DRILL | MC_BUILDING_CAP_CONFIG | MC_BUILDING_CAP_LINK | MC_BUILDING_CAP_DAMAGE | MC_BUILDING_CAP_TEAM,
        true, true, true, true
    },
    {
        MC_BLOCK_CONVEYOR, MC_BUILDING_CONVEYOR, "conveyor", 1, 45, 0, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 8.0f,
        MC_BUILDING_CAP_CONVEYOR | MC_BUILDING_CAP_CONFIG | MC_BUILDING_CAP_LINK |
        MC_BUILDING_CAP_DAMAGE | MC_BUILDING_CAP_TEAM,
        true, true, true, true
    },
    {
        MC_BLOCK_DUO, MC_BUILDING_TURRET, "duo", 1, 40, 20, 20.0f, 60.0f,
        0.0f, 0.5f, 1.0f, 16.0f,
        MC_BUILDING_CAP_INVENTORY | MC_BUILDING_CAP_LIQUID | MC_BUILDING_CAP_POWER |
        MC_BUILDING_CAP_TURRET | MC_BUILDING_CAP_CONFIG | MC_BUILDING_CAP_LINK | MC_BUILDING_CAP_DAMAGE | MC_BUILDING_CAP_TEAM,
        true, true, true, true
    }
};

static const McBuildingRule *rule_at_block(McBlockId block){
    for(size_t i = 0; i < sizeof(building_rules) / sizeof(building_rules[0]); i++){
        if(building_rules[i].block == block) return &building_rules[i];
    }
    return NULL;
}

static void validation_reset(McBuildingValidation *validation){
    if(validation != NULL) *validation = (McBuildingValidation){0};
}

static McBuildingValidationCode validation_fail(McBuildingValidation *validation,
                                                  McBuildingValidationCode code,
                                                  McEntityId entity_id, McEntityId related_id,
                                                  size_t state_index, size_t link_index){
    if(validation != NULL){
        if(validation->code == MC_BUILDING_VALID) {
            validation->code = code;
            validation->entity_id = entity_id;
            validation->related_id = related_id;
            validation->state_index = state_index;
            validation->link_index = link_index;
        }
        validation->error_count++;
    }
    return code;
}

static float finite_clamp(float value, float low, float high){
    if(!isfinite(value)) return low;
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

static float clamp_metric(float value){
    return finite_clamp(value, 0.0f, 1.0f);
}

static bool finite_nonnegative_metric(float value){
    return isfinite(value) && value >= 0.0f;
}

const McBuildingRule *mc_building_rule(McBlockId block){
    return rule_at_block(block);
}

const McBuildingRule *mc_building_rule_for_kind(McBuildingKind kind, size_t ordinal){
    size_t seen = 0;
    for(size_t i = 0; i < sizeof(building_rules) / sizeof(building_rules[0]); i++){
        if(building_rules[i].kind != kind) continue;
        if(seen++ == ordinal) return &building_rules[i];
    }
    return NULL;
}

size_t mc_building_rule_count(void){
    return sizeof(building_rules) / sizeof(building_rules[0]);
}

uint32_t mc_building_capabilities(McBlockId block){
    const McBuildingRule *rule = rule_at_block(block);
    return rule == NULL ? MC_BUILDING_CAP_NONE : rule->capabilities;
}

bool mc_building_has_capability(McBlockId block, McBuildingCapability capability){
    if(capability == MC_BUILDING_CAP_NONE) return true;
    return (mc_building_capabilities(block) & (uint32_t)capability) == (uint32_t)capability;
}

bool mc_building_can_rotate(McBlockId block){
    const McBuildingRule *rule = rule_at_block(block);
    return rule != NULL && rule->rotatable;
}

bool mc_building_is_solid(McBlockId block){
    const McBuildingRule *rule = rule_at_block(block);
    return rule != NULL && rule->solid;
}

bool mc_building_is_team_owned(McBlockId block){
    const McBuildingRule *rule = rule_at_block(block);
    return rule != NULL && rule->team_owned;
}

float mc_building_rule_power_balance(McBlockId block){
    const McBuildingRule *rule = rule_at_block(block);
    return rule == NULL ? 0.0f : rule->power_generation - rule->power_consumption;
}

float mc_building_rule_build_cost(McBlockId block){
    const McBuildingRule *rule = rule_at_block(block);
    return rule == NULL ? 0.0f : rule->build_cost;
}

bool mc_building_rule_accepts_item(McBlockId block, McItemId item){
    (void)item;
    return mc_building_has_capability(block, MC_BUILDING_CAP_INVENTORY) ||
           mc_building_has_capability(block, MC_BUILDING_CAP_CONVEYOR);
}

bool mc_building_rule_accepts_liquid(McBlockId block, McLiquidId liquid){
    (void)liquid;
    return mc_building_has_capability(block, MC_BUILDING_CAP_LIQUID);
}

bool mc_building_rule_accepts_power(McBlockId block){
    return mc_building_has_capability(block, MC_BUILDING_CAP_POWER);
}

static uint32_t inventory_total_saturated(const McInventory *inventory){
    uint64_t total = 0;
    if(inventory == NULL) return 0;
    for(size_t i = 0; i < MC_ITEM_COUNT; i++){
        total += inventory->items[i];
        if(total >= UINT32_MAX) return UINT32_MAX;
    }
    return (uint32_t)total;
}

static float liquid_total(const McBuildingLiquidState *liquids){
    float total = 0.0f;
    if(liquids == NULL) return total;
    for(size_t i = 0; i < MC_BUILDING_LIQUID_COUNT; i++) total += finite_clamp(liquids->amount[i], 0.0f, FLT_MAX);
    return total;
}

McBuildingValidationCode mc_building_state_validate(const McBuildingState *state,
                                                     McBuildingValidation *validation){
    validation_reset(validation);
    if(state == NULL) return validation_fail(validation, MC_BUILDING_INVALID_NULL, 0, 0, 0, 0);
    if(state->entity_id == MC_ENTITY_NONE) return validation_fail(validation, MC_BUILDING_INVALID_ID, state->entity_id, 0, 0, 0);
    if(state->team >= MC_TEAM_COUNT) return validation_fail(validation, MC_BUILDING_INVALID_TEAM, state->entity_id, 0, 0, 0);
    const McBuildingRule *rule = rule_at_block(state->block);
    if(rule == NULL) return validation_fail(validation, MC_BUILDING_INVALID_BLOCK, state->entity_id, 0, 0, 0);
    if(state->kind > MC_BUILDING_LAUNCHER || state->kind != rule->kind || state->size == 0 ||
       state->size != rule->size || state->rotation > 3){
        return validation_fail(validation, MC_BUILDING_INVALID_GEOMETRY, state->entity_id, 0, 0, 0);
    }
    if(!isfinite(state->health) || !isfinite(state->max_health) || state->max_health <= 0.0f ||
       state->health < 0.0f || state->health > state->max_health){
        return validation_fail(validation, MC_BUILDING_INVALID_HEALTH, state->entity_id, 0, 0, 0);
    }
    if(!isfinite(state->build_progress) || state->build_progress < 0.0f || state->build_progress > 1.0f){
        return validation_fail(validation, MC_BUILDING_INVALID_PROGRESS, state->entity_id, 0, 0, 0);
    }
    if(!isfinite(state->time_scale) || state->time_scale < 0.0f || !isfinite(state->heat) || state->heat < 0.0f ||
       !isfinite(state->efficiency) || state->efficiency < 0.0f){
        return validation_fail(validation, MC_BUILDING_INVALID_TIME_SCALE, state->entity_id, 0, 0, 0);
    }
    if(state->inventory.capacity > 0 && inventory_total_saturated(&state->inventory) > state->inventory.capacity){
        return validation_fail(validation, MC_BUILDING_INVALID_INVENTORY, state->entity_id, 0, 0, 0);
    }
    if(!isfinite(state->liquids.capacity) || state->liquids.capacity < 0.0f ||
       state->liquids.dominant >= MC_BUILDING_LIQUID_COUNT || liquid_total(&state->liquids) > state->liquids.capacity + 0.0001f){
        return validation_fail(validation, MC_BUILDING_INVALID_LIQUID, state->entity_id, 0, 0, 0);
    }
    if(!isfinite(state->power.capacity) || !isfinite(state->power.stored) || state->power.capacity < 0.0f ||
       state->power.stored < 0.0f || state->power.stored > state->power.capacity + 0.0001f ||
       !isfinite(state->power.satisfaction) || state->power.satisfaction < 0.0f || state->power.satisfaction > 1.0001f){
        return validation_fail(validation, MC_BUILDING_INVALID_POWER, state->entity_id, 0, 0, 0);
    }
    if(state->config_size > MC_BUILDING_MAX_CONFIG || (state->config == NULL && state->config_size != 0)){
        return validation_fail(validation, MC_BUILDING_INVALID_CONFIG, state->entity_id, 0, 0, 0);
    }
    if(state->kind == MC_BUILDING_DRILL && (!isfinite(state->drill.drill_time) || state->drill.drill_time <= 0.0f)){
        return validation_fail(validation, MC_BUILDING_INVALID_SUBSYSTEM, state->entity_id, 0, 0, 0);
    }
    if(state->kind == MC_BUILDING_TURRET && (!isfinite(state->turret.range) || state->turret.range < 0.0f ||
                                               !isfinite(state->turret.reload_time) || state->turret.reload_time <= 0.0f)){
        return validation_fail(validation, MC_BUILDING_INVALID_SUBSYSTEM, state->entity_id, 0, 0, 0);
    }
    if(state->kind == MC_BUILDING_CONVEYOR && (!isfinite(state->conveyor.speed) || state->conveyor.speed < 0.0f)){
        return validation_fail(validation, MC_BUILDING_INVALID_SUBSYSTEM, state->entity_id, 0, 0, 0);
    }
    if(validation != NULL) validation->code = MC_BUILDING_VALID;
    return MC_BUILDING_VALID;
}

McStatus mc_building_state_normalize(McBuildingState *state, bool repair){
    McBuildingValidation validation;
    McBuildingValidationCode code = mc_building_state_validate(state, &validation);
    if(code == MC_BUILDING_VALID) return MC_OK;
    if(!repair || state == NULL) return MC_FORMAT_ERROR;
    if(state->entity_id == MC_ENTITY_NONE) return MC_INVALID_ARGUMENT;
    if(state->team >= MC_TEAM_COUNT) state->team = MC_TEAM_SHARDED;
    const McBuildingRule *rule = rule_at_block(state->block);
    if(rule == NULL) state->block = MC_BLOCK_AIR;
    rule = rule_at_block(state->block);
    state->kind = rule->kind;
    state->size = rule->size == 0 ? 1 : rule->size;
    if(state->rotation > 3) state->rotation = 0;
    state->max_health = finite_clamp(state->max_health, 1.0f, FLT_MAX);
    state->health = finite_clamp(state->health, 0.0f, state->max_health);
    state->build_progress = finite_clamp(state->build_progress, 0.0f, 1.0f);
    state->heat = finite_clamp(state->heat, 0.0f, FLT_MAX);
    state->time_scale = finite_clamp(state->time_scale, 0.0f, FLT_MAX);
    state->efficiency = finite_clamp(state->efficiency, 0.0f, 1.0f);
    state->inventory.capacity = rule->inventory_capacity;
    uint32_t inventory_left = state->inventory.capacity;
    for(size_t i = 0; i < MC_ITEM_COUNT; i++){
        if(state->inventory.items[i] > inventory_left) state->inventory.items[i] = inventory_left;
        inventory_left -= state->inventory.items[i];
    }
    state->liquids.capacity = finite_clamp(state->liquids.capacity, 0.0f, FLT_MAX);
    float liquid_left = state->liquids.capacity;
    for(size_t i = 0; i < MC_BUILDING_LIQUID_COUNT; i++){
        state->liquids.amount[i] = finite_clamp(state->liquids.amount[i], 0.0f, liquid_left);
        liquid_left -= state->liquids.amount[i];
    }
    if(state->liquids.dominant >= MC_BUILDING_LIQUID_COUNT) state->liquids.dominant = 0;
    state->power.capacity = finite_clamp(state->power.capacity, 0.0f, FLT_MAX);
    state->power.stored = finite_clamp(state->power.stored, 0.0f, state->power.capacity);
    state->power.satisfaction = state->power.capacity == 0.0f ? 1.0f : state->power.stored / state->power.capacity;
    if(state->config_size > MC_BUILDING_MAX_CONFIG){
        free(state->config);
        state->config = NULL;
        state->config_size = 0;
    }
    if(state->health == 0.0f) mc_building_state_mark_dead(state);
    mc_building_recalculate_liquid_dominant(state);
    mc_building_recalculate_efficiency(state);
    if(mc_building_state_validate(state, &validation) != MC_BUILDING_VALID) return MC_FORMAT_ERROR;
    return MC_OK;
}

bool mc_building_state_is_ready(const McBuildingState *state){
    return state != NULL && state->active && state->enabled && !state->dead &&
           state->build_progress >= 1.0f && state->health > 0.0f;
}

bool mc_building_state_is_stalled(const McBuildingState *state){
    if(state == NULL || state->dead || !state->active) return true;
    if(!state->enabled || state->build_progress < 1.0f) return true;
    if(state->kind == MC_BUILDING_FACTORY && (state->factory.input_stalls != 0 || state->factory.power_stalls != 0 ||
                                               state->factory.output_blocked)) return true;
    if(state->kind == MC_BUILDING_DRILL && state->inventory.capacity != 0 && mc_building_item_space(state) == 0) return true;
    return state->efficiency <= 0.0f;
}

float mc_building_state_health_ratio(const McBuildingState *state){
    if(state == NULL || state->max_health <= 0.0f) return 0.0f;
    return finite_clamp(state->health / state->max_health, 0.0f, 1.0f);
}

float mc_building_state_inventory_ratio(const McBuildingState *state){
    if(state == NULL || state->inventory.capacity == 0) return 0.0f;
    return finite_clamp((float)mc_inventory_total(&state->inventory) / (float)state->inventory.capacity, 0.0f, 1.0f);
}

float mc_building_state_liquid_ratio(const McBuildingState *state){
    if(state == NULL || state->liquids.capacity <= 0.0f) return 0.0f;
    return finite_clamp(liquid_total(&state->liquids) / state->liquids.capacity, 0.0f, 1.0f);
}

float mc_building_state_power_ratio(const McBuildingState *state){
    if(state == NULL || state->power.capacity <= 0.0f) return 0.0f;
    return finite_clamp(state->power.stored / state->power.capacity, 0.0f, 1.0f);
}

void mc_building_state_metrics(const McBuildingState *state, McBuildingStateMetrics *metrics){
    if(metrics == NULL) return;
    *metrics = (McBuildingStateMetrics){0};
    if(state == NULL) return;
    metrics->ticks = state->ticks;
    metrics->item_units = mc_inventory_total(&state->inventory);
    metrics->liquid_milliunits = (uint64_t)(liquid_total(&state->liquids) * 1000.0f);
    metrics->power_milliunits = (uint64_t)(finite_clamp(state->power.stored, 0.0f, FLT_MAX) * 1000.0f);
    metrics->produced_units = state->production.produced;
    metrics->consumed_units = state->production.consumed;
    metrics->drilled_units = state->drill.mined;
    metrics->turret_shots = state->turret.shots;
    metrics->processor_instructions = state->processor.executed;
    metrics->utilization = clamp_metric(state->efficiency);
    metrics->health_ratio = mc_building_state_health_ratio(state);
    metrics->inventory_ratio = mc_building_state_inventory_ratio(state);
    metrics->liquid_ratio = mc_building_state_liquid_ratio(state);
    metrics->power_ratio = mc_building_state_power_ratio(state);
    metrics->stalled = mc_building_state_is_stalled(state);
    metrics->healthy = !state->dead && state->health > 0.0f;
}

McStatus mc_building_state_set_flag(McBuildingState *state, uint32_t flag, bool enabled){
    if(state == NULL || flag == 0) return MC_INVALID_ARGUMENT;
    if(enabled) state->flags |= flag;
    else state->flags &= ~flag;
    return MC_OK;
}

bool mc_building_state_has_flag(const McBuildingState *state, uint32_t flag){
    return state != NULL && flag != 0 && (state->flags & flag) == flag;
}

McStatus mc_building_state_set_position(McBuildingState *state, uint16_t x, uint16_t y){
    if(state == NULL) return MC_INVALID_ARGUMENT;
    state->tile_x = x;
    state->tile_y = y;
    return MC_OK;
}

McStatus mc_building_state_set_size(McBuildingState *state, uint16_t size){
    if(state == NULL || size == 0 || size > 64) return MC_INVALID_ARGUMENT;
    state->size = size;
    return MC_OK;
}

McStatus mc_building_state_set_heat(McBuildingState *state, float heat){
    if(state == NULL || !finite_nonnegative_metric(heat)) return MC_INVALID_ARGUMENT;
    state->heat = heat;
    return MC_OK;
}

McStatus mc_building_state_set_efficiency(McBuildingState *state, float efficiency){
    if(state == NULL || !isfinite(efficiency) || efficiency < 0.0f || efficiency > 1.0f) return MC_INVALID_ARGUMENT;
    state->efficiency = efficiency;
    return MC_OK;
}

McStatus mc_building_state_set_active(McBuildingState *state, bool active){
    if(state == NULL || (active && state->dead)) return MC_INVALID_ARGUMENT;
    state->active = active;
    mc_building_recalculate_efficiency(state);
    return MC_OK;
}

McStatus mc_building_state_set_liquid_capacity(McBuildingState *state, float capacity){
    if(state == NULL || !finite_nonnegative_metric(capacity) || liquid_total(&state->liquids) > capacity + 0.0001f){
        return MC_INVALID_ARGUMENT;
    }
    state->liquids.capacity = capacity;
    return MC_OK;
}

McStatus mc_building_state_set_power_demand(McBuildingState *state, float demand){
    if(state == NULL || !finite_nonnegative_metric(demand)) return MC_INVALID_ARGUMENT;
    state->power.consumption = demand;
    return MC_OK;
}

McStatus mc_building_state_set_power_generation(McBuildingState *state, float generation){
    if(state == NULL || !finite_nonnegative_metric(generation)) return MC_INVALID_ARGUMENT;
    state->power.production = generation;
    return MC_OK;
}

static McBuildingValidationCode validate_store_link(const McBuildingStore *store, const McBuildingLink *link,
                                                     size_t index, McBuildingValidation *validation){
    if(link->from == MC_ENTITY_NONE || link->to == MC_ENTITY_NONE || link->from == link->to ||
       link->kind > MC_BUILDING_LINK_PAYLOAD || !isfinite(link->distance) || link->distance < 0.0f ||
       !isfinite(link->capacity) || link->capacity < 0.0f || !isfinite(link->transferred) || link->transferred < 0.0f){
        return validation_fail(validation, MC_BUILDING_INVALID_LINK, link->from, link->to, 0, index);
    }
    if(mc_building_store_find_const(store, link->from) == NULL || mc_building_store_find_const(store, link->to) == NULL){
        return validation_fail(validation, MC_BUILDING_INVALID_LINK, link->from, link->to, 0, index);
    }
    return MC_BUILDING_VALID;
}

McBuildingValidationCode mc_building_store_validate(const McBuildingStore *store,
                                                     McBuildingValidation *validation){
    validation_reset(validation);
    if(store == NULL) return validation_fail(validation, MC_BUILDING_INVALID_NULL, 0, 0, 0, 0);
    for(size_t i = 0; i < store->count; i++){
        if(validation != NULL) validation->checked_states++;
        McBuildingValidation local;
        if(mc_building_state_validate(&store->states[i], &local) != MC_BUILDING_VALID){
            validation_fail(validation, local.code, store->states[i].entity_id, 0, i, 0);
        }
        for(size_t previous = 0; previous < i; previous++){
            if(store->states[previous].entity_id == store->states[i].entity_id){
                validation_fail(validation, MC_BUILDING_INVALID_DUPLICATE, store->states[i].entity_id,
                                store->states[previous].entity_id, i, 0);
            }
            if(store->states[previous].tile_x == store->states[i].tile_x && store->states[previous].tile_y == store->states[i].tile_y){
                validation_fail(validation, MC_BUILDING_INVALID_OVERLAP, store->states[i].entity_id,
                                store->states[previous].entity_id, i, 0);
            }
        }
    }
    for(size_t i = 0; i < store->link_count; i++){
        if(validation != NULL) validation->checked_links++;
        (void)validate_store_link(store, &store->links[i], i, validation);
        for(size_t previous = 0; previous < i; previous++){
            if(store->links[previous].active && store->links[i].active &&
               store->links[previous].from == store->links[i].from && store->links[previous].to == store->links[i].to &&
               store->links[previous].kind == store->links[i].kind){
                validation_fail(validation, MC_BUILDING_INVALID_DUPLICATE, store->links[i].from,
                                store->links[i].to, 0, i);
            }
        }
    }
    if(validation != NULL && validation->error_count != 0) return validation->code;
    if(validation != NULL) validation->code = MC_BUILDING_VALID;
    return MC_BUILDING_VALID;
}

McStatus mc_building_store_normalize(McBuildingStore *store, bool repair,
                                      McBuildingValidation *validation){
    if(store == NULL) return MC_INVALID_ARGUMENT;
    bool changed = false;
    for(size_t i = 0; i < store->count; i++){
        McBuildingValidation before;
        McBuildingValidationCode code = mc_building_state_validate(&store->states[i], &before);
        if(code != MC_BUILDING_VALID){
            if(!repair) {
                if(validation != NULL) *validation = before;
                return MC_FORMAT_ERROR;
            }
            McStatus status = mc_building_state_normalize(&store->states[i], true);
            if(status != MC_OK) return status;
            changed = true;
        }
    }
    McStatus status = mc_building_store_rebuild_networks(store);
    if(status != MC_OK) return status;
    McBuildingValidation after;
    McBuildingValidationCode code = mc_building_store_validate(store, &after);
    if(validation != NULL){
        *validation = after;
        validation->repaired = changed;
    }
    return code == MC_BUILDING_VALID ? MC_OK : MC_FORMAT_ERROR;
}

size_t mc_building_store_collect_ids(const McBuildingStore *store, McBuildingKind kind,
                                     McTeam team, McEntityId *ids, size_t capacity){
    if(store == NULL) return 0;
    size_t written = 0;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state->kind != kind || state->team != team) continue;
        if(ids != NULL && written < capacity) ids[written] = state->entity_id;
        written++;
    }
    return written;
}

size_t mc_building_store_collect_capability(const McBuildingStore *store, McBuildingCapability capability,
                                            McEntityId *ids, size_t capacity){
    if(store == NULL) return 0;
    size_t written = 0;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(!mc_building_has_capability(state->block, capability)) continue;
        if(ids != NULL && written < capacity) ids[written] = state->entity_id;
        written++;
    }
    return written;
}

static uint32_t squared_distance(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by){
    int64_t dx = (int64_t)ax - (int64_t)bx;
    int64_t dy = (int64_t)ay - (int64_t)by;
    uint64_t result = (uint64_t)(dx * dx) + (uint64_t)(dy * dy);
    return result > UINT32_MAX ? UINT32_MAX : (uint32_t)result;
}

const McBuildingState *mc_building_store_find_nearest_const(const McBuildingStore *store,
                                                             uint16_t x, uint16_t y, McTeam team,
                                                             uint32_t capabilities){
    if(store == NULL || team >= MC_TEAM_COUNT) return NULL;
    const McBuildingState *best = NULL;
    uint32_t best_distance = UINT32_MAX;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state->team != team || state->dead || (state->block != MC_BLOCK_AIR &&
           (mc_building_capabilities(state->block) & capabilities) != capabilities)) continue;
        uint32_t distance = squared_distance(x, y, state->tile_x, state->tile_y);
        if(best == NULL || distance < best_distance || (distance == best_distance && state->entity_id < best->entity_id)){
            best = state;
            best_distance = distance;
        }
    }
    return best;
}

McBuildingState *mc_building_store_find_nearest(McBuildingStore *store, uint16_t x, uint16_t y,
                                                 McTeam team, uint32_t capabilities){
    return (McBuildingState *)mc_building_store_find_nearest_const(store, x, y, team, capabilities);
}

size_t mc_building_store_count_kind(const McBuildingStore *store, McBuildingKind kind){
    if(store == NULL) return 0;
    size_t count = 0;
    for(size_t i = 0; i < store->count; i++) if(store->states[i].kind == kind) count++;
    return count;
}

size_t mc_building_store_count_team(const McBuildingStore *store, McTeam team){
    if(store == NULL || team >= MC_TEAM_COUNT) return 0;
    size_t count = 0;
    for(size_t i = 0; i < store->count; i++) if(store->states[i].team == team) count++;
    return count;
}

void mc_building_store_metrics(const McBuildingStore *store, McBuildingStoreMetrics *metrics){
    if(metrics == NULL) return;
    *metrics = (McBuildingStoreMetrics){0};
    if(store == NULL) return;
    metrics->tick = store->tick;
    metrics->state_count = store->count;
    metrics->link_count = store->link_count;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state->active && !state->dead) metrics->active_count++;
        if(state->dead) metrics->dead_count++;
        metrics->item_units += mc_inventory_total(&state->inventory);
        metrics->produced_units += state->production.produced;
        metrics->consumed_units += state->production.consumed;
        metrics->drilled_units += state->drill.mined;
        metrics->turret_shots += state->turret.shots;
        metrics->processor_instructions += state->processor.executed;
        metrics->total_liquid += liquid_total(&state->liquids);
        metrics->total_power += state->power.stored;
        metrics->total_power_capacity += state->power.capacity;
        metrics->average_efficiency += state->efficiency;
    }
    for(size_t i = 0; i < store->link_count; i++) if(store->links[i].active) metrics->connected_link_count++;
    if(store->count != 0) metrics->average_efficiency /= (float)store->count;
    McBuildingValidation validation;
    metrics->valid = mc_building_store_validate(store, &validation) == MC_BUILDING_VALID;
}

static uint32_t state_network_id(const McBuildingState *state, McBuildingLinkKind kind){
    if(kind == MC_BUILDING_LINK_POWER) return state->power.network_id;
    if(kind == MC_BUILDING_LINK_LIQUID) return state->liquids.network_id;
    return 0;
}

McStatus mc_building_store_network_report(const McBuildingStore *store, uint32_t network_id,
                                          McBuildingLinkKind kind, McBuildingNetworkReport *report){
    if(store == NULL || report == NULL || network_id == 0 || kind > MC_BUILDING_LINK_PAYLOAD) return MC_INVALID_ARGUMENT;
    *report = (McBuildingNetworkReport){.network_id = network_id, .kind = kind};
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state_network_id(state, kind) != network_id) continue;
        report->member_count++;
        if(kind == MC_BUILDING_LINK_POWER){
            report->capacity += state->power.capacity;
            report->stored += state->power.stored;
            report->demand += state->power.consumption;
            report->production += state->power.production;
        }else if(kind == MC_BUILDING_LINK_LIQUID){
            report->capacity += state->liquids.capacity;
            report->liquid_volume += liquid_total(&state->liquids);
        }
    }
    for(size_t i = 0; i < store->link_count; i++){
        const McBuildingLink *link = &store->links[i];
        if(!link->active || link->kind != kind) continue;
        const McBuildingState *from = mc_building_store_find_const(store, link->from);
        const McBuildingState *to = mc_building_store_find_const(store, link->to);
        if(from != NULL && to != NULL && state_network_id(from, kind) == network_id && state_network_id(to, kind) == network_id){
            report->link_count++;
            report->capacity += link->capacity;
            report->connected = true;
        }
    }
    report->saturated = kind == MC_BUILDING_LINK_POWER
        ? report->demand > report->stored + report->production
        : report->liquid_volume >= report->capacity && report->capacity > 0.0f;
    return report->member_count == 0 ? MC_NOT_FOUND : MC_OK;
}

size_t mc_building_store_network_ids(const McBuildingStore *store, McBuildingLinkKind kind,
                                     uint32_t *ids, size_t capacity){
    if(store == NULL || kind > MC_BUILDING_LINK_PAYLOAD) return 0;
    size_t count = 0;
    for(size_t i = 0; i < store->count; i++){
        uint32_t id = state_network_id(&store->states[i], kind);
        if(id == 0) continue;
        bool seen = false;
        for(size_t previous = 0; previous < i; previous++) if(state_network_id(&store->states[previous], kind) == id) seen = true;
        if(seen) continue;
        if(ids != NULL && count < capacity) ids[count] = id;
        count++;
    }
    return count;
}

bool mc_building_store_has_overlap(const McBuildingStore *store, uint16_t x, uint16_t y,
                                   uint16_t size, McEntityId ignore){
    if(store == NULL || size == 0) return false;
    uint32_t right = (uint32_t)x + size;
    uint32_t bottom = (uint32_t)y + size;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state->entity_id == ignore || state->dead) continue;
        uint32_t other_right = (uint32_t)state->tile_x + (state->size == 0 ? 1 : state->size);
        uint32_t other_bottom = (uint32_t)state->tile_y + (state->size == 0 ? 1 : state->size);
        if((uint32_t)state->tile_x < right && other_right > x && (uint32_t)state->tile_y < bottom && other_bottom > y) return true;
    }
    return false;
}

McStatus mc_building_store_validate_placement(const McBuildingStore *store, McBlockId block,
                                               McTeam team, uint16_t x, uint16_t y){
    const McBuildingRule *rule = rule_at_block(block);
    if(store == NULL || rule == NULL || block == MC_BLOCK_AIR || team >= MC_TEAM_COUNT) return MC_INVALID_ARGUMENT;
    if(mc_building_store_has_overlap(store, x, y, rule->size, MC_ENTITY_NONE)) return MC_CAPACITY_EXCEEDED;
    return MC_OK;
}

static const McBuildingLink *const_link(const McBuildingStore *store, McEntityId from, McEntityId to,
                                         McBuildingLinkKind kind){
    if(store == NULL) return NULL;
    for(size_t i = 0; i < store->link_count; i++){
        const McBuildingLink *link = &store->links[i];
        if(link->active && link->from == from && link->to == to && link->kind == kind) return link;
    }
    return NULL;
}

McBuildingTransferResult mc_building_store_transfer(const McBuildingStore *store,
                                                    const McBuildingTransferRequest *request){
    McBuildingTransferResult result = {.status = MC_INVALID_ARGUMENT};
    if(store == NULL || request == NULL || request->from == MC_ENTITY_NONE || request->to == MC_ENTITY_NONE ||
       request->kind > MC_BUILDING_LINK_PAYLOAD) return result;
    result.from = request->from;
    result.to = request->to;
    result.kind = request->kind;
    const McBuildingState *source = mc_building_store_find_const(store, request->from);
    const McBuildingState *destination = mc_building_store_find_const(store, request->to);
    const McBuildingLink *link = const_link(store, request->from, request->to, request->kind);
    if(source == NULL || destination == NULL || link == NULL){ result.status = MC_NOT_FOUND; return result; }
    result.link_capacity = link->capacity;
    if(request->kind == MC_BUILDING_LINK_ITEM){
        if(request->item >= MC_ITEM_COUNT){ result.status = MC_INVALID_ARGUMENT; return result; }
        uint32_t wanted = request->all_available ? mc_building_item_count(source, request->item) : request->item_amount;
        uint32_t available = mc_building_item_count(source, request->item);
        uint32_t room = mc_building_item_space(destination);
        uint32_t moved = wanted < available ? wanted : available;
        if(moved > room) moved = room;
        if(link->capacity > 0.0f && (float)moved > link->capacity) moved = (uint32_t)link->capacity;
        result.item_moved = moved;
        result.complete = moved == wanted;
    }else if(request->kind == MC_BUILDING_LINK_LIQUID){
        if(request->liquid >= MC_BUILDING_LIQUID_COUNT || !finite_nonnegative_metric(request->liquid_amount)) return result;
        float wanted = request->all_available ? mc_building_liquid_amount(source, request->liquid) : request->liquid_amount;
        float moved = wanted < mc_building_liquid_amount(source, request->liquid) ? wanted : mc_building_liquid_amount(source, request->liquid);
        float room = mc_building_liquid_space(destination, request->liquid);
        if(moved > room) moved = room;
        if(link->capacity > 0.0f && moved > link->capacity) moved = link->capacity;
        result.liquid_moved = moved;
        result.complete = fabsf(moved - wanted) < 0.0001f;
    }else if(request->kind == MC_BUILDING_LINK_POWER){
        if(!finite_nonnegative_metric(request->power_amount)) return result;
        float wanted = request->all_available ? source->power.stored : request->power_amount;
        float moved = wanted < source->power.stored ? wanted : source->power.stored;
        float room = mc_building_power_space(destination);
        if(moved > room) moved = room;
        if(link->capacity > 0.0f && moved > link->capacity) moved = link->capacity;
        result.power_moved = moved;
        result.complete = fabsf(moved - wanted) < 0.0001f;
    }else{
        result.status = MC_INVALID_ARGUMENT;
        return result;
    }
    result.status = result.complete ? MC_OK : MC_CAPACITY_EXCEEDED;
    return result;
}

McStatus mc_building_store_transfer_apply(McBuildingStore *store,
                                          const McBuildingTransferRequest *request,
                                          McBuildingTransferResult *result){
    if(store == NULL || request == NULL) return MC_INVALID_ARGUMENT;
    McBuildingTransferResult preview = mc_building_store_transfer(store, request);
    if(result != NULL) *result = preview;
    if(preview.status == MC_INVALID_ARGUMENT || preview.status == MC_NOT_FOUND) return preview.status;
    if(request->kind == MC_BUILDING_LINK_ITEM){
        (void)mc_building_remove_item(mc_building_store_find(store, request->from), request->item, preview.item_moved);
        (void)mc_building_add_item(mc_building_store_find(store, request->to), request->item, preview.item_moved);
        store->transferred_items += preview.item_moved;
    }else if(request->kind == MC_BUILDING_LINK_LIQUID){
        float moved = mc_building_remove_liquid(mc_building_store_find(store, request->from), request->liquid, preview.liquid_moved);
        (void)mc_building_add_liquid(mc_building_store_find(store, request->to), request->liquid, moved);
        preview.liquid_moved = moved;
    }else if(request->kind == MC_BUILDING_LINK_POWER){
        float moved = mc_building_consume_power(mc_building_store_find(store, request->from), preview.power_moved);
        (void)mc_building_receive_power(mc_building_store_find(store, request->to), moved);
        preview.power_moved = moved;
    }
    McBuildingLink *link = mc_building_store_find_link(store, request->from, request->to, request->kind);
    if(link != NULL){
        float delta = request->kind == MC_BUILDING_LINK_ITEM ? (float)preview.item_moved
                     : request->kind == MC_BUILDING_LINK_LIQUID ? preview.liquid_moved : preview.power_moved;
        link->transferred += delta;
    }
    preview.complete = request->kind == MC_BUILDING_LINK_ITEM
        ? preview.item_moved == (request->all_available ? preview.item_moved : request->item_amount)
        : request->kind == MC_BUILDING_LINK_LIQUID
            ? preview.liquid_moved + 0.0001f >= (request->all_available ? preview.liquid_moved : request->liquid_amount)
            : preview.power_moved + 0.0001f >= (request->all_available ? preview.power_moved : request->power_amount);
    preview.status = preview.complete ? MC_OK : MC_CAPACITY_EXCEEDED;
    if(result != NULL) *result = preview;
    return preview.status;
}

size_t mc_building_store_transfer_all(McBuildingStore *store, McBuildingLinkKind kind,
                                       McBuildingTransferResult *results, size_t capacity){
    if(store == NULL || kind > MC_BUILDING_LINK_PAYLOAD) return 0;
    size_t processed = 0;
    size_t link_count = store->link_count;
    for(size_t i = 0; i < link_count; i++){
        McBuildingLink *link = &store->links[i];
        if(!link->active || link->kind != kind) continue;
        McBuildingState *source = mc_building_store_find(store, link->from);
        McBuildingTransferRequest request = {
            .from = link->from, .to = link->to, .kind = kind, .item = MC_ITEM_SCRAP,
            .liquid = MC_LIQUID_WATER, .all_available = false
        };
        if(kind == MC_BUILDING_LINK_ITEM){
            bool found = false;
            for(size_t item = 0; item < MC_ITEM_COUNT; item++){
                if(mc_building_item_count(source, (McItemId)item) != 0){ request.item = (McItemId)item; request.item_amount = 1; found = true; break; }
            }
            if(!found) continue;
        }else if(kind == MC_BUILDING_LINK_LIQUID){
            mc_building_recalculate_liquid_dominant(source);
            request.liquid = (McLiquidId)source->liquids.dominant;
            request.liquid_amount = 1.0f;
        }else if(kind == MC_BUILDING_LINK_POWER){
            request.power_amount = source->power.stored * 0.25f;
        }else continue;
        McBuildingTransferResult result;
        (void)mc_building_store_transfer_apply(store, &request, &result);
        if(results != NULL && processed < capacity) results[processed] = result;
        processed++;
    }
    return processed;
}

void mc_building_store_reset_link_counters(McBuildingStore *store){
    if(store == NULL) return;
    for(size_t i = 0; i < store->link_count; i++) store->links[i].transferred = 0.0f;
}

float mc_building_store_total_liquid(const McBuildingStore *store, McLiquidId liquid){
    if(store == NULL || liquid >= MC_BUILDING_LIQUID_COUNT) return 0.0f;
    float total = 0.0f;
    for(size_t i = 0; i < store->count; i++) total += store->states[i].liquids.amount[liquid];
    return total;
}

float mc_building_store_total_power(const McBuildingStore *store){
    if(store == NULL) return 0.0f;
    float total = 0.0f;
    for(size_t i = 0; i < store->count; i++) total += store->states[i].power.stored;
    return total;
}

uint64_t mc_building_store_total_items(const McBuildingStore *store, McItemId item){
    if(store == NULL || item >= MC_ITEM_COUNT) return 0;
    uint64_t total = 0;
    for(size_t i = 0; i < store->count; i++) total += mc_building_item_count(&store->states[i], item);
    return total;
}

void mc_building_config_view_init(McBuildingConfigView *view, const McBuildingState *state){
    if(view == NULL) return;
    *view = (McBuildingConfigView){0};
    if(state != NULL && state->config != NULL){
        view->data = state->config;
        view->size = state->config_size;
        view->valid = true;
    }else if(state != NULL && state->config_size == 0){
        view->valid = true;
    }
}

static bool config_take(McBuildingConfigView *view, size_t size, const uint8_t **data){
    if(view == NULL || !view->valid || view->position > view->size || size > view->size - view->position) return false;
    if(data != NULL) *data = view->data + view->position;
    view->position += size;
    return true;
}

bool mc_building_config_read_u8(McBuildingConfigView *view, uint8_t *value){
    const uint8_t *data = NULL;
    if(value == NULL || !config_take(view, 1, &data)) return false;
    *value = data[0];
    return true;
}

bool mc_building_config_read_u16(McBuildingConfigView *view, uint16_t *value){
    const uint8_t *data = NULL;
    if(value == NULL || !config_take(view, 2, &data)) return false;
    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return true;
}

bool mc_building_config_read_u32(McBuildingConfigView *view, uint32_t *value){
    const uint8_t *data = NULL;
    if(value == NULL || !config_take(view, 4, &data)) return false;
    *value = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    return true;
}

bool mc_building_config_read_f32(McBuildingConfigView *view, float *value){
    uint32_t bits = 0;
    if(value == NULL || !mc_building_config_read_u32(view, &bits)) return false;
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

bool mc_building_config_read_bool(McBuildingConfigView *view, bool *value){
    uint8_t encoded = 0;
    if(value == NULL || !mc_building_config_read_u8(view, &encoded) || encoded > 1) return false;
    *value = encoded != 0;
    return true;
}

bool mc_building_config_read_bytes(McBuildingConfigView *view, uint8_t *data, size_t size){
    const uint8_t *source = NULL;
    if(size != 0 && data == NULL) return false;
    if(!config_take(view, size, &source)) return false;
    if(size != 0) memcpy(data, source, size);
    return true;
}

void mc_building_config_writer_init(McBuildingConfigWriter *writer, McBuffer *output){
    if(writer == NULL) return;
    *writer = (McBuildingConfigWriter){.output = output, .valid = output != NULL};
}

static bool config_put(McBuildingConfigWriter *writer, const void *data, size_t size){
    if(writer == NULL || !writer->valid || mc_buffer_write_bytes(writer->output, data, size) != MC_OK){
        if(writer != NULL) writer->valid = false;
        return false;
    }
    writer->fields++;
    return true;
}

bool mc_building_config_write_u8(McBuildingConfigWriter *writer, uint8_t value){
    return config_put(writer, &value, sizeof(value));
}

bool mc_building_config_write_u16(McBuildingConfigWriter *writer, uint16_t value){
    uint8_t data[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return config_put(writer, data, sizeof(data));
}

bool mc_building_config_write_u32(McBuildingConfigWriter *writer, uint32_t value){
    uint8_t data[4] = {(uint8_t)(value >> 24), (uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
    return config_put(writer, data, sizeof(data));
}

bool mc_building_config_write_f32(McBuildingConfigWriter *writer, float value){
    uint32_t bits = 0;
    if(!isfinite(value)) return false;
    memcpy(&bits, &value, sizeof(bits));
    return mc_building_config_write_u32(writer, bits);
}

bool mc_building_config_write_bool(McBuildingConfigWriter *writer, bool value){
    return mc_building_config_write_u8(writer, value ? 1 : 0);
}

bool mc_building_config_write_bytes(McBuildingConfigWriter *writer, const uint8_t *data, size_t size){
    if(data == NULL && size != 0) return false;
    return config_put(writer, data, size);
}

McStatus mc_building_state_copy_config_from_buffer(McBuildingState *state, const McBuffer *buffer){
    if(state == NULL || buffer == NULL || buffer->size > MC_BUILDING_MAX_CONFIG ||
       (buffer->data == NULL && buffer->size != 0)) return MC_INVALID_ARGUMENT;
    return mc_building_state_set_config(state, buffer->data, buffer->size);
}

bool mc_building_config_view_at_end(const McBuildingConfigView *view){
    return view != NULL && view->valid && view->position == view->size;
}
