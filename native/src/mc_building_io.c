#include "mindustry_building.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MC_BUILDING_MAX_STATES ((size_t)1000000)

static McStatus write_u64(McBuffer *output, uint64_t value){
    McStatus status = MC_OK;
    for(int i = 7; i >= 0 && status == MC_OK; i--){
        status = mc_buffer_write_u8(output, (uint8_t)(value >> (i * 8)));
    }
    return status;
}

static McStatus read_u64(McBuffer *input, uint64_t *value){
    if(input == NULL || value == NULL || input->position > input->size || input->size - input->position < 8){
        return MC_FORMAT_ERROR;
    }
    uint64_t result = 0;
    for(size_t i = 0; i < 8; i++) result = (result << 8) | input->data[input->position++];
    *value = result;
    return MC_OK;
}

static McStatus write_f32(McBuffer *output, float value){
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return mc_buffer_write_u32_be(output, bits);
}

static McStatus read_f32(McBuffer *input, float *value){
    uint32_t bits = 0;
    McStatus status = mc_buffer_read_u32_be(input, &bits);
    if(status != MC_OK) return status;
    memcpy(value, &bits, sizeof(bits));
    return MC_OK;
}

static McStatus write_bool(McBuffer *output, bool value){
    return mc_buffer_write_u8(output, value ? 1 : 0);
}

static McStatus read_bool(McBuffer *input, bool *value){
    uint8_t encoded = 0;
    McStatus status = mc_buffer_read_u8(input, &encoded);
    if(status != MC_OK) return status;
    if(encoded > 1) return MC_FORMAT_ERROR;
    *value = encoded != 0;
    return MC_OK;
}

static McStatus write_state(const McBuildingState *state, McBuffer *output){
    if(state == NULL || output == NULL || state->team >= MC_TEAM_COUNT || state->kind > MC_BUILDING_LAUNCHER ||
       state->config_size > MC_BUILDING_MAX_CONFIG || (state->config == NULL && state->config_size != 0)) return MC_INVALID_ARGUMENT;
    McStatus status = mc_buffer_write_u32_be(output, state->entity_id);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->block);
    if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)state->kind);
    if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)state->team);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->tile_x);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->tile_y);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->size);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->rotation);
    if(status == MC_OK) status = write_f32(output, state->health);
    if(status == MC_OK) status = write_f32(output, state->max_health);
    if(status == MC_OK) status = write_f32(output, state->build_progress);
    if(status == MC_OK) status = write_f32(output, state->heat);
    if(status == MC_OK) status = write_f32(output, state->efficiency);
    if(status == MC_OK) status = write_f32(output, state->time_scale);
    if(status == MC_OK) status = write_u64(output, state->ticks);
    if(status == MC_OK) status = write_u64(output, state->last_tick);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->flags);
    if(status == MC_OK) status = write_bool(output, state->enabled);
    if(status == MC_OK) status = write_bool(output, state->active);
    if(status == MC_OK) status = write_bool(output, state->dead);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->inventory.capacity);
    for(size_t i = 0; status == MC_OK && i < MC_ITEM_COUNT; i++){
        status = mc_buffer_write_u32_be(output, state->inventory.items[i]);
    }

    if(status == MC_OK) status = write_f32(output, state->liquids.capacity);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->liquids.network_id);
    if(status == MC_OK) status = mc_buffer_write_u8(output, state->liquids.dominant);
    if(status == MC_OK) status = write_bool(output, state->liquids.connected);
    for(size_t i = 0; status == MC_OK && i < MC_BUILDING_LIQUID_COUNT; i++) status = write_f32(output, state->liquids.amount[i]);

    if(status == MC_OK) status = write_f32(output, state->power.capacity);
    if(status == MC_OK) status = write_f32(output, state->power.stored);
    if(status == MC_OK) status = write_f32(output, state->power.production);
    if(status == MC_OK) status = write_f32(output, state->power.consumption);
    if(status == MC_OK) status = write_f32(output, state->power.satisfaction);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->power.network_id);
    if(status == MC_OK) status = write_bool(output, state->power.connected);
    if(status == MC_OK) status = write_bool(output, state->power.battery);

    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->production.recipe);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->production.pending_recipe);
    if(status == MC_OK) status = write_f32(output, state->production.progress);
    if(status == MC_OK) status = write_f32(output, state->production.warmup);
    if(status == MC_OK) status = write_f32(output, state->production.speed_scale);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->production.produced);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->production.consumed);
    if(status == MC_OK) status = write_bool(output, state->production.active);
    if(status == MC_OK) status = write_bool(output, state->production.output_blocked);

    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->drill.ore);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->drill.hardness);
    if(status == MC_OK) status = write_f32(output, state->drill.progress);
    if(status == MC_OK) status = write_f32(output, state->drill.drill_time);
    if(status == MC_OK) status = write_f32(output, state->drill.speed_scale);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->drill.mined);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->drill.last_output);
    if(status == MC_OK) status = write_bool(output, state->drill.dominant);
    if(status == MC_OK) status = write_bool(output, state->drill.coolant);

    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->turret.target);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->turret.last_target);
    if(status == MC_OK) status = write_f32(output, state->turret.reload);
    if(status == MC_OK) status = write_f32(output, state->turret.reload_time);
    if(status == MC_OK) status = write_f32(output, state->turret.range);
    if(status == MC_OK) status = write_f32(output, state->turret.rotation);
    if(status == MC_OK) status = write_f32(output, state->turret.target_rotation);
    if(status == MC_OK) status = write_f32(output, state->turret.ammo_fraction);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->turret.shots);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->turret.misses);
    if(status == MC_OK) status = write_bool(output, state->turret.shooting);
    if(status == MC_OK) status = write_bool(output, state->turret.coolant);
    if(status == MC_OK) status = write_bool(output, state->turret.overdrive);

    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->conveyor.item);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->conveyor.item_amount);
    if(status == MC_OK) status = write_f32(output, state->conveyor.progress);
    if(status == MC_OK) status = write_f32(output, state->conveyor.speed);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->conveyor.moved);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->conveyor.next);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->conveyor.previous);
    if(status == MC_OK) status = write_bool(output, state->conveyor.occupied);

    if(status == MC_OK) status = mc_buffer_write_u16_be(output, state->factory.recipe);
    if(status == MC_OK) status = write_f32(output, state->factory.progress);
    if(status == MC_OK) status = write_f32(output, state->factory.craft_time);
    if(status == MC_OK) status = write_f32(output, state->factory.efficiency);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->factory.crafts);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->factory.power_stalls);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->factory.input_stalls);
    if(status == MC_OK) status = write_bool(output, state->factory.running);
    if(status == MC_OK) status = write_bool(output, state->factory.output_blocked);

    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->core.storage_capacity);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->core.unit_capacity);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->core.units_spawned);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->core.units_lost);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->core.waves_survived);
    if(status == MC_OK) status = write_bool(output, state->core.attack);
    if(status == MC_OK) status = write_bool(output, state->core.launch_ready);

    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->processor.processor_id);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->processor.program_counter);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->processor.executed);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, state->processor.errors);
    if(status == MC_OK) status = write_f32(output, state->processor.execution_time);
    if(status == MC_OK) status = write_f32(output, state->processor.range);
    if(status == MC_OK) status = write_bool(output, state->processor.enabled);
    if(status == MC_OK) status = write_bool(output, state->processor.linked);

    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)state->config_size);
    if(status == MC_OK) status = mc_buffer_write_bytes(output, state->config, state->config_size);
    return status;
}

static McStatus read_state(McBuffer *input, McBuildingState *state){
    *state = (McBuildingState){0};
    uint8_t kind = 0, team = 0, dominant = 0;
    uint16_t value = 0;
    uint32_t config_size = 0;
    McStatus status = mc_buffer_read_u32_be(input, &state->entity_id);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &value);
    state->block = (McBlockId)value;
    if(status == MC_OK) status = mc_buffer_read_u8(input, &kind);
    if(status == MC_OK) status = mc_buffer_read_u8(input, &team);
    if(status == MC_OK && (kind > MC_BUILDING_LAUNCHER || team >= MC_TEAM_COUNT)) status = MC_FORMAT_ERROR;
    state->kind = (McBuildingKind)kind;
    state->team = (McTeam)team;
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->tile_x);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->tile_y);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->size);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->rotation);
    if(status == MC_OK) status = read_f32(input, &state->health);
    if(status == MC_OK) status = read_f32(input, &state->max_health);
    if(status == MC_OK) status = read_f32(input, &state->build_progress);
    if(status == MC_OK) status = read_f32(input, &state->heat);
    if(status == MC_OK) status = read_f32(input, &state->efficiency);
    if(status == MC_OK) status = read_f32(input, &state->time_scale);
    if(status == MC_OK) status = read_u64(input, &state->ticks);
    if(status == MC_OK) status = read_u64(input, &state->last_tick);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->flags);
    if(status == MC_OK) status = read_bool(input, &state->enabled);
    if(status == MC_OK) status = read_bool(input, &state->active);
    if(status == MC_OK) status = read_bool(input, &state->dead);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->inventory.capacity);
    for(size_t i = 0; status == MC_OK && i < MC_ITEM_COUNT; i++) status = mc_buffer_read_u32_be(input, &state->inventory.items[i]);

    if(status == MC_OK) status = read_f32(input, &state->liquids.capacity);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->liquids.network_id);
    if(status == MC_OK) status = mc_buffer_read_u8(input, &dominant);
    if(status == MC_OK && dominant >= MC_BUILDING_LIQUID_COUNT) status = MC_FORMAT_ERROR;
    state->liquids.dominant = dominant;
    if(status == MC_OK) status = read_bool(input, &state->liquids.connected);
    for(size_t i = 0; status == MC_OK && i < MC_BUILDING_LIQUID_COUNT; i++) status = read_f32(input, &state->liquids.amount[i]);

    if(status == MC_OK) status = read_f32(input, &state->power.capacity);
    if(status == MC_OK) status = read_f32(input, &state->power.stored);
    if(status == MC_OK) status = read_f32(input, &state->power.production);
    if(status == MC_OK) status = read_f32(input, &state->power.consumption);
    if(status == MC_OK) status = read_f32(input, &state->power.satisfaction);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->power.network_id);
    if(status == MC_OK) status = read_bool(input, &state->power.connected);
    if(status == MC_OK) status = read_bool(input, &state->power.battery);

    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->production.recipe);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->production.pending_recipe);
    if(status == MC_OK) status = read_f32(input, &state->production.progress);
    if(status == MC_OK) status = read_f32(input, &state->production.warmup);
    if(status == MC_OK) status = read_f32(input, &state->production.speed_scale);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->production.produced);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->production.consumed);
    if(status == MC_OK) status = read_bool(input, &state->production.active);
    if(status == MC_OK) status = read_bool(input, &state->production.output_blocked);

    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->drill.ore);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->drill.hardness);
    if(status == MC_OK) status = read_f32(input, &state->drill.progress);
    if(status == MC_OK) status = read_f32(input, &state->drill.drill_time);
    if(status == MC_OK) status = read_f32(input, &state->drill.speed_scale);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->drill.mined);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->drill.last_output);
    if(status == MC_OK) status = read_bool(input, &state->drill.dominant);
    if(status == MC_OK) status = read_bool(input, &state->drill.coolant);

    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->turret.target);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->turret.last_target);
    if(status == MC_OK) status = read_f32(input, &state->turret.reload);
    if(status == MC_OK) status = read_f32(input, &state->turret.reload_time);
    if(status == MC_OK) status = read_f32(input, &state->turret.range);
    if(status == MC_OK) status = read_f32(input, &state->turret.rotation);
    if(status == MC_OK) status = read_f32(input, &state->turret.target_rotation);
    if(status == MC_OK) status = read_f32(input, &state->turret.ammo_fraction);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->turret.shots);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->turret.misses);
    if(status == MC_OK) status = read_bool(input, &state->turret.shooting);
    if(status == MC_OK) status = read_bool(input, &state->turret.coolant);
    if(status == MC_OK) status = read_bool(input, &state->turret.overdrive);

    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &value);
    state->conveyor.item = (McItemId)value;
    if(status == MC_OK && state->conveyor.item >= MC_ITEM_COUNT) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->conveyor.item_amount);
    if(status == MC_OK) status = read_f32(input, &state->conveyor.progress);
    if(status == MC_OK) status = read_f32(input, &state->conveyor.speed);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->conveyor.moved);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->conveyor.next);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->conveyor.previous);
    if(status == MC_OK) status = read_bool(input, &state->conveyor.occupied);

    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &state->factory.recipe);
    if(status == MC_OK) status = read_f32(input, &state->factory.progress);
    if(status == MC_OK) status = read_f32(input, &state->factory.craft_time);
    if(status == MC_OK) status = read_f32(input, &state->factory.efficiency);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->factory.crafts);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->factory.power_stalls);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->factory.input_stalls);
    if(status == MC_OK) status = read_bool(input, &state->factory.running);
    if(status == MC_OK) status = read_bool(input, &state->factory.output_blocked);

    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->core.storage_capacity);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->core.unit_capacity);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->core.units_spawned);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->core.units_lost);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->core.waves_survived);
    if(status == MC_OK) status = read_bool(input, &state->core.attack);
    if(status == MC_OK) status = read_bool(input, &state->core.launch_ready);

    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->processor.processor_id);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->processor.program_counter);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->processor.executed);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &state->processor.errors);
    if(status == MC_OK) status = read_f32(input, &state->processor.execution_time);
    if(status == MC_OK) status = read_f32(input, &state->processor.range);
    if(status == MC_OK) status = read_bool(input, &state->processor.enabled);
    if(status == MC_OK) status = read_bool(input, &state->processor.linked);

    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &config_size);
    if(status == MC_OK && (config_size > MC_BUILDING_MAX_CONFIG || config_size > input->size - input->position)) status = MC_FORMAT_ERROR;
    if(status == MC_OK && config_size != 0){
        state->config = malloc(config_size);
        if(state->config == NULL) status = MC_OUT_OF_MEMORY;
        else{
            status = mc_buffer_read_bytes(input, state->config, config_size);
            state->config_size = config_size;
        }
    }
    if(status != MC_OK){
        mc_building_state_destroy(state);
        return status;
    }
    if(state->size == 0 || state->max_health <= 0.0f || state->health < 0.0f || !isfinite(state->health) ||
       !isfinite(state->max_health) || !isfinite(state->efficiency) || !isfinite(state->time_scale)){
        mc_building_state_destroy(state);
        return MC_FORMAT_ERROR;
    }
    return MC_OK;
}

McStatus mc_building_store_write(const McBuildingStore *store, McBuffer *output){
    if(store == NULL || output == NULL || store->count > UINT32_MAX || store->link_count > UINT32_MAX){
        return MC_INVALID_ARGUMENT;
    }
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_bytes(output, (uint8_t[]){MC_BUILDING_STATE_MAGIC_0, MC_BUILDING_STATE_MAGIC_1,
        MC_BUILDING_STATE_MAGIC_2, MC_BUILDING_STATE_MAGIC_3}, 4);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, MC_BUILDING_STATE_VERSION);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, 0);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)store->count);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)store->link_count);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, store->next_network_id);
    if(status == MC_OK) status = write_u64(output, store->tick);
    if(status == MC_OK) status = write_u64(output, store->produced_items);
    if(status == MC_OK) status = write_u64(output, store->mined_items);
    if(status == MC_OK) status = write_u64(output, store->transferred_items);
    if(status == MC_OK) status = write_u64(output, store->consumed_items);
    if(status == MC_OK) status = write_u64(output, store->power_ticks);
    if(status == MC_OK) status = write_u64(output, store->liquid_ticks);
    if(status == MC_OK) status = write_u64(output, store->turret_shots);
    if(status == MC_OK) status = write_u64(output, store->processor_steps);
    for(size_t i = 0; status == MC_OK && i < store->count; i++) status = write_state(&store->states[i], output);
    for(size_t i = 0; status == MC_OK && i < store->link_count; i++){
        const McBuildingLink *link = &store->links[i];
        if(link->kind > MC_BUILDING_LINK_PAYLOAD || !isfinite(link->distance) || !isfinite(link->capacity) ||
           !isfinite(link->transferred)) return MC_INVALID_ARGUMENT;
        status = mc_buffer_write_u32_be(output, link->from);
        if(status == MC_OK) status = mc_buffer_write_u32_be(output, link->to);
        if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)link->kind);
        if(status == MC_OK) status = write_f32(output, link->distance);
        if(status == MC_OK) status = write_f32(output, link->capacity);
        if(status == MC_OK) status = write_f32(output, link->transferred);
        if(status == MC_OK) status = write_bool(output, link->active);
    }
    return status;
}

McStatus mc_building_store_read(const uint8_t *data, size_t size, McBuildingStore *store){
    if(data == NULL || store == NULL || size < 4 + 2 + 2 + 4 + 4 + 4 + 8 * 9) return MC_INVALID_ARGUMENT;
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint8_t magic[4] = {0};
    uint16_t version = 0, reserved = 0;
    uint32_t count = 0, link_count = 0;
    McBuildingStore decoded;
    mc_building_store_init(&decoded);
    McStatus status = mc_buffer_read_bytes(&input, magic, sizeof(magic));
    if(status == MC_OK && (magic[0] != MC_BUILDING_STATE_MAGIC_0 || magic[1] != MC_BUILDING_STATE_MAGIC_1 ||
                           magic[2] != MC_BUILDING_STATE_MAGIC_2 || magic[3] != MC_BUILDING_STATE_MAGIC_3)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &version);
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &reserved);
    if(status == MC_OK && (version != MC_BUILDING_STATE_VERSION || reserved != 0)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &count);
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &link_count);
    if(status == MC_OK && (count > MC_BUILDING_MAX_STATES || link_count > MC_BUILDING_MAX_LINKS ||
                           count > (uint32_t)(input.size - input.position))) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &decoded.next_network_id);
    if(status == MC_OK) status = read_u64(&input, &decoded.tick);
    if(status == MC_OK) status = read_u64(&input, &decoded.produced_items);
    if(status == MC_OK) status = read_u64(&input, &decoded.mined_items);
    if(status == MC_OK) status = read_u64(&input, &decoded.transferred_items);
    if(status == MC_OK) status = read_u64(&input, &decoded.consumed_items);
    if(status == MC_OK) status = read_u64(&input, &decoded.power_ticks);
    if(status == MC_OK) status = read_u64(&input, &decoded.liquid_ticks);
    if(status == MC_OK) status = read_u64(&input, &decoded.turret_shots);
    if(status == MC_OK) status = read_u64(&input, &decoded.processor_steps);
    if(status == MC_OK && count != 0){
        decoded.states = calloc(count, sizeof(*decoded.states));
        if(decoded.states == NULL) status = MC_OUT_OF_MEMORY;
        else{ decoded.count = count; decoded.capacity = count; }
    }
    for(size_t i = 0; status == MC_OK && i < decoded.count; i++) status = read_state(&input, &decoded.states[i]);
    if(status == MC_OK && link_count != 0){
        decoded.links = calloc(link_count, sizeof(*decoded.links));
        if(decoded.links == NULL) status = MC_OUT_OF_MEMORY;
        else{ decoded.link_count = link_count; decoded.link_capacity = link_count; }
    }
    for(size_t i = 0; status == MC_OK && i < decoded.link_count; i++){
        McBuildingLink *link = &decoded.links[i];
        uint8_t kind = 0;
        status = mc_buffer_read_u32_be(&input, &link->from);
        if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &link->to);
        if(status == MC_OK) status = mc_buffer_read_u8(&input, &kind);
        if(status == MC_OK && kind > MC_BUILDING_LINK_PAYLOAD) status = MC_FORMAT_ERROR;
        link->kind = (McBuildingLinkKind)kind;
        if(status == MC_OK) status = read_f32(&input, &link->distance);
        if(status == MC_OK) status = read_f32(&input, &link->capacity);
        if(status == MC_OK) status = read_f32(&input, &link->transferred);
        if(status == MC_OK) status = read_bool(&input, &link->active);
        if(status == MC_OK && (!isfinite(link->distance) || !isfinite(link->capacity) || !isfinite(link->transferred))) status = MC_FORMAT_ERROR;
    }
    if(status == MC_OK && input.position != input.size) status = MC_FORMAT_ERROR;
    if(status != MC_OK){
        mc_building_store_destroy(&decoded);
        return status;
    }
    mc_building_store_destroy(store);
    *store = decoded;
    return MC_OK;
}
