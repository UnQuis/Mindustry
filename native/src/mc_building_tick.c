#include "mindustry_building.h"

#include <math.h>
#include <stdlib.h>

static bool finite_nonnegative(float value){
    return isfinite(value) && value >= 0.0f;
}

static float clamp01(float value){
    if(value < 0.0f) return 0.0f;
    if(value > 1.0f) return 1.0f;
    return value;
}

static void refresh_power_ratio(McBuildingState *state){
    if(state->power.capacity <= 0.0f) state->power.satisfaction = 1.0f;
    else state->power.satisfaction = clamp01(state->power.stored / state->power.capacity);
}

static float state_delta(const McBuildingState *state, uint64_t tick){
    uint64_t elapsed = tick - state->last_tick;
    if(elapsed > UINT32_MAX) return (float)UINT32_MAX;
    return (float)elapsed;
}

static void tick_common(McBuildingState *state, float elapsed){
    state->heat *= powf(0.995f, elapsed);
    if(state->heat < 0.0001f) state->heat = 0.0f;
    if(state->power.production > 0.0f && state->active && state->enabled){
        float generated = state->power.production * state->efficiency * elapsed;
        (void)mc_building_receive_power(state, generated);
    }
    if(state->power.consumption > 0.0f && state->active && state->enabled){
        float wanted = state->power.consumption * elapsed;
        (void)mc_building_consume_power(state, wanted);
    }
    refresh_power_ratio(state);
    mc_building_recalculate_efficiency(state);
}

static void tick_drill(McBuildingState *state, float elapsed){
    if(state->kind != MC_BUILDING_DRILL || state->drill.drill_time <= 0.0f || state->efficiency <= 0.0f) return;
    float speed = state->efficiency * (state->drill.speed_scale <= 0.0f ? 1.0f : state->drill.speed_scale);
    if(state->drill.coolant) speed *= 1.2f;
    state->drill.progress += speed * elapsed / state->drill.drill_time;
    uint32_t outputs = 0;
    while(state->drill.progress >= 1.0f && outputs < 1024u){
        uint16_t ore = state->drill.ore < MC_ITEM_COUNT ? state->drill.ore : MC_ITEM_COPPER;
        uint32_t accepted = mc_building_add_item(state, (McItemId)ore, 1);
        if(accepted == 0) break;
        state->drill.progress -= 1.0f;
        state->drill.last_output = ore;
        state->drill.mined++;
        outputs++;
    }
}

static bool recipe_input_available(const McBuildingState *state, const McBuildingRecipe *recipe){
    if(recipe->input_amount != 0 && mc_building_item_count(state, (McItemId)recipe->input_item) < recipe->input_amount) return false;
    if(recipe->liquid_amount > 0.0f && mc_building_liquid_amount(state, (McLiquidId)recipe->liquid) < recipe->liquid_amount) return false;
    return true;
}

static bool recipe_output_available(const McBuildingState *state, const McBuildingRecipe *recipe){
    if(recipe->output_amount == 0) return true;
    return mc_building_item_space(state) >= recipe->output_amount;
}

static bool reserve_recipe_inputs(McBuildingState *state, const McBuildingRecipe *recipe){
    if(!recipe_input_available(state, recipe)) return false;
    if(recipe->input_amount != 0){
        if(mc_building_remove_item(state, (McItemId)recipe->input_item, recipe->input_amount) != recipe->input_amount) return false;
    }
    if(recipe->liquid_amount > 0.0f){
        if(mc_building_remove_liquid(state, (McLiquidId)recipe->liquid, recipe->liquid_amount) < recipe->liquid_amount) return false;
    }
    state->production.consumed += recipe->input_amount;
    return true;
}

static void tick_factory(McBuildingState *state, float elapsed){
    if((state->kind != MC_BUILDING_FACTORY && state->kind != MC_BUILDING_GENERIC) || state->efficiency <= 0.0f) return;
    McBuildingRecipe recipe = mc_building_recipe(state->factory.recipe);
    if(!recipe.valid || recipe.kind != MC_BUILDING_FACTORY) return;
    if(state->factory.craft_time <= 0.0f) state->factory.craft_time = recipe.craft_time;
    if(state->factory.progress >= 1.0f){
        if(!recipe_output_available(state, &recipe)){
            state->factory.output_blocked = true;
            state->production.output_blocked = true;
            state->factory.running = false;
            return;
        }
        uint32_t accepted = mc_building_add_item(state, (McItemId)recipe.output_item, recipe.output_amount);
        if(accepted != recipe.output_amount){
            state->factory.output_blocked = true;
            state->production.output_blocked = true;
            return;
        }
        state->factory.progress -= 1.0f;
        state->factory.crafts++;
        state->production.produced += recipe.output_amount;
        state->factory.output_blocked = false;
        state->production.output_blocked = false;
    }
    if(state->factory.progress <= 0.0f){
        if(!reserve_recipe_inputs(state, &recipe)){
            state->factory.input_stalls++;
            state->factory.running = false;
            state->production.active = false;
            return;
        }
        state->factory.running = true;
        state->production.active = true;
    }
    if(recipe.power_use > 0.0f){
        float required = recipe.power_use * elapsed;
        float consumed = mc_building_consume_power(state, required);
        if(consumed < required){
            state->factory.power_stalls++;
            state->factory.running = false;
            state->production.active = false;
            return;
        }
    }
    float speed = state->efficiency * (state->factory.efficiency <= 0.0f ? 1.0f : state->factory.efficiency);
    state->factory.progress += speed * elapsed / state->factory.craft_time;
    state->production.progress = state->factory.progress;
    state->production.warmup = clamp01(state->factory.progress);
    state->production.active = state->factory.running;
}

static void tick_production(McBuildingState *state, float elapsed){
    if(state->kind == MC_BUILDING_FACTORY) tick_factory(state, elapsed);
    else if(state->production.active && state->production.speed_scale > 0.0f){
        state->production.progress += state->efficiency * state->production.speed_scale * elapsed / 60.0f;
        state->production.warmup = clamp01(state->production.progress);
    }
}

static void tick_turret(McBuildingState *state, float elapsed){
    if(state->kind != MC_BUILDING_TURRET || !state->turret.shooting || state->efficiency <= 0.0f) return;
    float speed = state->turret.overdrive ? 1.5f : 1.0f;
    if(state->turret.coolant) speed *= 1.25f;
    state->turret.reload += elapsed * state->efficiency * speed;
    if(state->turret.reload_time <= 0.0f) state->turret.reload_time = 1.0f;
    while(state->turret.reload >= state->turret.reload_time){
        state->turret.reload -= state->turret.reload_time;
        state->turret.shots++;
        state->turret.last_target = state->turret.target;
    }
}

static void tick_conveyor(McBuildingState *state, float elapsed){
    if(state->kind != MC_BUILDING_CONVEYOR || !state->conveyor.occupied || state->efficiency <= 0.0f) return;
    float speed = state->conveyor.speed <= 0.0f ? 1.0f : state->conveyor.speed;
    state->conveyor.progress += elapsed * speed * state->efficiency;
}

static void tick_processor(McBuildingState *state, float elapsed){
    if(state->kind != MC_BUILDING_PROCESSOR || !state->processor.enabled || !state->processor.linked || state->efficiency <= 0.0f) return;
    state->processor.execution_time += elapsed * state->efficiency;
    uint32_t steps = (uint32_t)state->processor.execution_time;
    if(steps == 0) return;
    state->processor.execution_time -= (float)steps;
    state->processor.program_counter += steps;
    state->processor.executed += steps;
}

McStatus mc_building_state_step(McBuildingState *state, uint64_t tick){
    if(state == NULL || tick < state->last_tick) return MC_INVALID_ARGUMENT;
    if(tick == state->last_tick) return MC_OK;
    if(tick - state->last_tick > UINT32_MAX) return MC_CAPACITY_EXCEEDED;
    if(!finite_nonnegative(state->time_scale)) return MC_INVALID_ARGUMENT;
    float elapsed = state_delta(state, tick);
    state->ticks += (uint64_t)elapsed;
    state->last_tick = tick;
    if(state->dead || !state->active) return MC_OK;
    mc_building_recalculate_efficiency(state);
    tick_common(state, elapsed);
    tick_drill(state, elapsed);
    tick_production(state, elapsed);
    tick_turret(state, elapsed);
    tick_conveyor(state, elapsed);
    tick_processor(state, elapsed);
    return MC_OK;
}

static size_t state_index(const McBuildingStore *store, McEntityId id){
    if(store == NULL) return SIZE_MAX;
    for(size_t i = 0; i < store->count; i++) if(store->states[i].entity_id == id) return i;
    return SIZE_MAX;
}

static void transfer_conveyors(McBuildingStore *store){
    for(size_t i = 0; i < store->link_count; i++){
        McBuildingLink *link = &store->links[i];
        if(!link->active || link->kind != MC_BUILDING_LINK_ITEM) continue;
        size_t source_index = state_index(store, link->from);
        size_t destination_index = state_index(store, link->to);
        if(source_index == SIZE_MAX || destination_index == SIZE_MAX) continue;
        McBuildingState *source = &store->states[source_index];
        McBuildingState *destination = &store->states[destination_index];
        McItemId item = source->conveyor.item;
        uint32_t amount = 0;
        bool payload = source->kind == MC_BUILDING_CONVEYOR && source->conveyor.occupied;
        if(payload) amount = source->conveyor.item_amount;
        else{
            for(size_t item_index = 0; item_index < MC_ITEM_COUNT; item_index++){
                if(mc_building_item_count(source, (McItemId)item_index) != 0){
                    item = (McItemId)item_index;
                    amount = mc_building_item_count(source, item);
                    break;
                }
            }
        }
        if(amount == 0) continue;
        if(link->capacity > 0.0f && (float)amount > link->capacity) amount = (uint32_t)link->capacity;
        uint32_t accepted = mc_building_add_item(destination, item, amount);
        if(accepted == 0) continue;
        if(payload){
            source->conveyor.item_amount -= accepted;
            if(source->conveyor.item_amount == 0) source->conveyor.occupied = false;
            source->conveyor.moved += accepted;
            if(source->conveyor.progress >= 1.0f) source->conveyor.progress -= 1.0f;
        }else{
            (void)mc_building_remove_item(source, item, accepted);
        }
        link->transferred += (float)accepted;
        store->transferred_items += accepted;
        destination->conveyor.previous = source->entity_id;
        source->conveyor.next = destination->entity_id;
    }
}

static void transfer_liquids(McBuildingStore *store){
    for(size_t i = 0; i < store->link_count; i++){
        McBuildingLink *link = &store->links[i];
        if(!link->active || link->kind != MC_BUILDING_LINK_LIQUID) continue;
        McBuildingState *source = mc_building_store_find(store, link->from);
        McBuildingState *destination = mc_building_store_find(store, link->to);
        if(source == NULL || destination == NULL) continue;
        mc_building_recalculate_liquid_dominant(source);
        float amount = mc_building_liquid_amount(source, (McLiquidId)source->liquids.dominant);
        if(amount <= 0.0f) continue;
        float moved = mc_building_store_transfer_liquid(store, source->entity_id, destination->entity_id,
                                                        (McLiquidId)source->liquids.dominant, amount < 1.0f ? amount : 1.0f);
        if(moved > 0.0f) store->liquid_ticks++;
    }
}

static void transfer_power(McBuildingStore *store){
    for(size_t i = 0; i < store->link_count; i++){
        McBuildingLink *link = &store->links[i];
        if(!link->active || link->kind != MC_BUILDING_LINK_POWER) continue;
        McBuildingState *source = mc_building_store_find(store, link->from);
        McBuildingState *destination = mc_building_store_find(store, link->to);
        if(source == NULL || destination == NULL) continue;
        float amount = source->power.stored * 0.25f;
        if(amount <= 0.0f) continue;
        float moved = mc_building_store_transfer_power(store, source->entity_id, destination->entity_id, amount);
        if(moved > 0.0f) store->power_ticks++;
    }
}

McStatus mc_building_store_step(McBuildingStore *store, uint64_t tick){
    if(store == NULL || tick < store->tick) return MC_INVALID_ARGUMENT;
    if(tick == store->tick) return MC_OK;
    if(tick == UINT64_MAX || tick - store->tick > UINT32_MAX) return MC_CAPACITY_EXCEEDED;
    for(uint64_t current = store->tick + 1; current <= tick; current++){
        McStatus status = mc_building_store_rebuild_networks(store);
        if(status != MC_OK) return status;
        for(size_t i = 0; i < store->link_count; i++) store->links[i].transferred = 0.0f;
        for(size_t i = 0; i < store->count; i++){
            McBuildingState *state = &store->states[i];
            uint32_t old_mined = state->drill.mined;
            uint32_t old_produced = state->production.produced;
            uint32_t old_consumed = state->production.consumed;
            uint32_t old_processor_steps = state->processor.executed;
            uint32_t old_shots = state->turret.shots;
            status = mc_building_state_step(state, current);
            if(status != MC_OK) return status;
            store->mined_items += state->drill.mined - old_mined;
            store->produced_items += state->production.produced - old_produced;
            store->consumed_items += state->production.consumed - old_consumed;
            store->processor_steps += state->processor.executed - old_processor_steps;
            store->turret_shots += state->turret.shots - old_shots;
        }
        transfer_conveyors(store);
        transfer_liquids(store);
        transfer_power(store);
        store->tick = current;
    }
    return MC_OK;
}

McStatus mc_building_store_step_range(McBuildingStore *store, uint64_t first_tick, uint32_t ticks){
    if(store == NULL || first_tick < store->tick) return MC_INVALID_ARGUMENT;
    if(ticks == 0) return MC_OK;
    uint64_t final_tick = first_tick + (uint64_t)ticks - 1;
    if(final_tick < first_tick) return MC_CAPACITY_EXCEEDED;
    return mc_building_store_step(store, final_tick);
}

McStatus mc_building_store_damage(McBuildingStore *store, McEntityId entity_id, float amount){
    if(store == NULL || amount <= 0.0f || !isfinite(amount)) return MC_INVALID_ARGUMENT;
    McBuildingState *state = mc_building_store_find(store, entity_id);
    if(state == NULL) return MC_NOT_FOUND;
    if(state->dead) return MC_OK;
    state->health = amount >= state->health ? 0.0f : state->health - amount;
    if(state->health <= 0.0f) mc_building_state_mark_dead(state);
    else mc_building_recalculate_efficiency(state);
    return MC_OK;
}

McStatus mc_building_store_heal(McBuildingStore *store, McEntityId entity_id, float amount){
    if(store == NULL || amount <= 0.0f || !isfinite(amount)) return MC_INVALID_ARGUMENT;
    McBuildingState *state = mc_building_store_find(store, entity_id);
    if(state == NULL) return MC_NOT_FOUND;
    if(state->dead){
        state->dead = false;
        state->active = true;
        state->enabled = true;
    }
    state->health += amount;
    if(state->health > state->max_health) state->health = state->max_health;
    return MC_OK;
}

size_t mc_building_store_active_count(const McBuildingStore *store){
    if(store == NULL) return 0;
    size_t active = 0;
    for(size_t i = 0; i < store->count; i++) if(store->states[i].active && !store->states[i].dead) active++;
    return active;
}

static uint64_t building_hash_bytes(uint64_t hash, const void *data, size_t size){
    const uint8_t *bytes = (const uint8_t *)data;
    for(size_t i = 0; i < size; i++){
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t mc_building_store_hash(const McBuildingStore *store){
    if(store == NULL) return 0;
    McBuffer encoded;
    mc_buffer_init(&encoded);
    if(mc_building_store_write(store, &encoded) != MC_OK){
        mc_buffer_destroy(&encoded);
        return 0;
    }
    uint64_t hash = building_hash_bytes(UINT64_C(1469598103934665603), encoded.data, encoded.size);
    mc_buffer_destroy(&encoded);
    return hash;
}
