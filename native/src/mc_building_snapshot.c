#include "mindustry_building_snapshot.h"

#include <stdlib.h>
#include <string.h>

#define MC_BUILDING_SNAPSHOT_MAGIC_0 ((uint8_t)'M')
#define MC_BUILDING_SNAPSHOT_MAGIC_1 ((uint8_t)'C')
#define MC_BUILDING_SNAPSHOT_MAGIC_2 ((uint8_t)'S')
#define MC_BUILDING_SNAPSHOT_MAGIC_3 ((uint8_t)'N')
#define MC_BUILDING_SNAPSHOT_VERSION 1u
#define MC_BUILDING_SNAPSHOT_MAX ((size_t)1000000)

static uint64_t snapshot_hash_bytes(uint64_t hash, const void *data, size_t size){
    const uint8_t *bytes = (const uint8_t *)data;
    for(size_t i = 0; i < size; i++){
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static McStatus copy_state(McBuildingState *destination, const McBuildingState *source){
    if(destination == NULL || source == NULL) return MC_INVALID_ARGUMENT;
    *destination = *source;
    destination->config = NULL;
    destination->config_size = 0;
    return mc_building_state_set_config(destination, source->config, source->config_size);
}

static void replace_state(McBuildingState *destination, const McBuildingState *source){
    uint8_t *old_config = destination->config;
    *destination = *source;
    destination->config = NULL;
    destination->config_size = 0;
    free(old_config);
    (void)mc_building_state_set_config(destination, source->config, source->config_size);
}

static uint64_t state_hash(const McBuildingState *state){
    if(state == NULL) return 0;
    McBuildingState copy = *state;
    copy.config = NULL;
    copy.config_size = 0;
    uint64_t hash = snapshot_hash_bytes(UINT64_C(1469598103934665603), &copy, sizeof(copy));
    hash = snapshot_hash_bytes(hash, state->config, state->config_size);
    return hash;
}

static bool state_equal(const McBuildingState *left, const McBuildingState *right){
    if(left == NULL || right == NULL) return left == right;
    if(left->config_size != right->config_size) return false;
    McBuildingState left_copy = *left;
    McBuildingState right_copy = *right;
    left_copy.config = NULL;
    right_copy.config = NULL;
    left_copy.config_size = 0;
    right_copy.config_size = 0;
    if(memcmp(&left_copy, &right_copy, sizeof(left_copy)) != 0) return false;
    return left->config_size == 0 || memcmp(left->config, right->config, left->config_size) == 0;
}

void mc_building_snapshot_init(McBuildingSnapshot *snapshot){
    if(snapshot == NULL) return;
    *snapshot = (McBuildingSnapshot){0};
}

void mc_building_snapshot_destroy(McBuildingSnapshot *snapshot){
    if(snapshot == NULL) return;
    if(snapshot->initialized) mc_building_state_destroy(&snapshot->state);
    *snapshot = (McBuildingSnapshot){0};
}

McStatus mc_building_snapshot_capture(McBuildingSnapshot *snapshot, const McBuildingState *state){
    if(snapshot == NULL || state == NULL) return MC_INVALID_ARGUMENT;
    McBuildingSnapshot temporary;
    mc_building_snapshot_init(&temporary);
    McStatus status = copy_state(&temporary.state, state);
    if(status != MC_OK){ mc_building_snapshot_destroy(&temporary); return status; }
    temporary.initialized = true;
    temporary.hash = state_hash(&temporary.state);
    mc_building_snapshot_destroy(snapshot);
    *snapshot = temporary;
    return MC_OK;
}

McStatus mc_building_snapshot_copy(McBuildingSnapshot *destination, const McBuildingSnapshot *source){
    if(destination == NULL || source == NULL || !source->initialized) return MC_INVALID_ARGUMENT;
    return mc_building_snapshot_capture(destination, &source->state);
}

bool mc_building_snapshot_equal(const McBuildingSnapshot *left, const McBuildingSnapshot *right){
    if(left == NULL || right == NULL || !left->initialized || !right->initialized) return left == right && left != NULL && left->initialized;
    return left->hash == right->hash && state_equal(&left->state, &right->state);
}

uint64_t mc_building_snapshot_hash(const McBuildingSnapshot *snapshot){
    return snapshot == NULL || !snapshot->initialized ? 0 : snapshot->hash;
}

const McBuildingState *mc_building_snapshot_state(const McBuildingSnapshot *snapshot){
    return snapshot == NULL || !snapshot->initialized ? NULL : &snapshot->state;
}

void mc_building_delta_init(McBuildingDelta *delta){
    if(delta == NULL) return;
    *delta = (McBuildingDelta){0};
}

void mc_building_delta_destroy(McBuildingDelta *delta){
    if(delta == NULL) return;
    if(delta->initialized) mc_building_state_destroy(&delta->state);
    *delta = (McBuildingDelta){0};
}

static uint32_t delta_fields(const McBuildingState *before, const McBuildingState *after){
    uint32_t fields = MC_BUILDING_DELTA_NONE;
    if(before->block != after->block || before->kind != after->kind || before->team != after->team ||
       before->tile_x != after->tile_x || before->tile_y != after->tile_y || before->size != after->size ||
       before->rotation != after->rotation) fields |= MC_BUILDING_DELTA_GEOMETRY;
    if(before->health != after->health || before->max_health != after->max_health || before->dead != after->dead) fields |= MC_BUILDING_DELTA_HEALTH;
    if(before->build_progress != after->build_progress || before->heat != after->heat || before->efficiency != after->efficiency ||
       before->time_scale != after->time_scale || before->ticks != after->ticks || before->last_tick != after->last_tick ||
       before->flags != after->flags || before->enabled != after->enabled || before->active != after->active) fields |= MC_BUILDING_DELTA_RUNTIME;
    if(memcmp(&before->inventory, &after->inventory, sizeof(before->inventory)) != 0) fields |= MC_BUILDING_DELTA_INVENTORY;
    if(memcmp(&before->liquids, &after->liquids, sizeof(before->liquids)) != 0) fields |= MC_BUILDING_DELTA_LIQUIDS;
    if(memcmp(&before->power, &after->power, sizeof(before->power)) != 0) fields |= MC_BUILDING_DELTA_POWER;
    if(memcmp(&before->production, &after->production, sizeof(before->production)) != 0) fields |= MC_BUILDING_DELTA_PRODUCTION;
    if(memcmp(&before->drill, &after->drill, sizeof(before->drill)) != 0) fields |= MC_BUILDING_DELTA_DRILL;
    if(memcmp(&before->turret, &after->turret, sizeof(before->turret)) != 0) fields |= MC_BUILDING_DELTA_TURRET;
    if(memcmp(&before->conveyor, &after->conveyor, sizeof(before->conveyor)) != 0) fields |= MC_BUILDING_DELTA_CONVEYOR;
    if(memcmp(&before->factory, &after->factory, sizeof(before->factory)) != 0) fields |= MC_BUILDING_DELTA_FACTORY;
    if(memcmp(&before->core, &after->core, sizeof(before->core)) != 0) fields |= MC_BUILDING_DELTA_CORE;
    if(memcmp(&before->processor, &after->processor, sizeof(before->processor)) != 0) fields |= MC_BUILDING_DELTA_PROCESSOR;
    if(before->config_size != after->config_size || (before->config_size != 0 && memcmp(before->config, after->config, before->config_size) != 0)) fields |= MC_BUILDING_DELTA_CONFIG;
    return fields;
}

McStatus mc_building_delta_compute(McBuildingDelta *delta, const McBuildingSnapshot *before,
                                   const McBuildingSnapshot *after){
    if(delta == NULL || before == NULL || after == NULL || !before->initialized || !after->initialized ||
       before->state.entity_id != after->state.entity_id) return MC_INVALID_ARGUMENT;
    McBuildingDelta temporary;
    mc_building_delta_init(&temporary);
    temporary.fields = delta_fields(&before->state, &after->state);
    temporary.entity_id = after->state.entity_id;
    temporary.from_tick = before->state.last_tick;
    temporary.to_tick = after->state.last_tick;
    McStatus status = copy_state(&temporary.state, &after->state);
    if(status != MC_OK){ mc_building_delta_destroy(&temporary); return status; }
    temporary.initialized = true;
    mc_building_delta_destroy(delta);
    *delta = temporary;
    return MC_OK;
}

static void apply_delta_fields(McBuildingState *state, const McBuildingState *source, uint32_t fields){
    if(fields & MC_BUILDING_DELTA_GEOMETRY){
        state->block = source->block;
        state->kind = source->kind;
        state->team = source->team;
        state->tile_x = source->tile_x;
        state->tile_y = source->tile_y;
        state->size = source->size;
        state->rotation = source->rotation;
    }
    if(fields & MC_BUILDING_DELTA_HEALTH){
        state->health = source->health;
        state->max_health = source->max_health;
        state->dead = source->dead;
    }
    if(fields & MC_BUILDING_DELTA_RUNTIME){
        state->build_progress = source->build_progress;
        state->heat = source->heat;
        state->efficiency = source->efficiency;
        state->time_scale = source->time_scale;
        state->ticks = source->ticks;
        state->last_tick = source->last_tick;
        state->flags = source->flags;
        state->enabled = source->enabled;
        state->active = source->active;
    }
    if(fields & MC_BUILDING_DELTA_INVENTORY) state->inventory = source->inventory;
    if(fields & MC_BUILDING_DELTA_LIQUIDS) state->liquids = source->liquids;
    if(fields & MC_BUILDING_DELTA_POWER) state->power = source->power;
    if(fields & MC_BUILDING_DELTA_PRODUCTION) state->production = source->production;
    if(fields & MC_BUILDING_DELTA_DRILL) state->drill = source->drill;
    if(fields & MC_BUILDING_DELTA_TURRET) state->turret = source->turret;
    if(fields & MC_BUILDING_DELTA_CONVEYOR) state->conveyor = source->conveyor;
    if(fields & MC_BUILDING_DELTA_FACTORY) state->factory = source->factory;
    if(fields & MC_BUILDING_DELTA_CORE) state->core = source->core;
    if(fields & MC_BUILDING_DELTA_PROCESSOR) state->processor = source->processor;
}

McStatus mc_building_delta_apply(McBuildingState *state, const McBuildingDelta *delta){
    if(state == NULL || delta == NULL || !delta->initialized || state->entity_id != delta->entity_id) return MC_INVALID_ARGUMENT;
    apply_delta_fields(state, &delta->state, delta->fields);
    if(delta->fields & MC_BUILDING_DELTA_CONFIG){
        McStatus status = mc_building_state_set_config(state, delta->state.config, delta->state.config_size);
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

bool mc_building_delta_has(const McBuildingDelta *delta, McBuildingDeltaField field){
    return delta != NULL && delta->initialized && field != MC_BUILDING_DELTA_NONE &&
           (delta->fields & (uint32_t)field) == (uint32_t)field;
}

size_t mc_building_delta_field_count(const McBuildingDelta *delta){
    if(delta == NULL || !delta->initialized) return 0;
    size_t count = 0;
    uint32_t fields = delta->fields;
    while(fields != 0){ count += fields & 1u; fields >>= 1; }
    return count;
}

void mc_building_snapshot_set_init(McBuildingSnapshotSet *set){
    if(set == NULL) return;
    *set = (McBuildingSnapshotSet){0};
}

void mc_building_snapshot_set_destroy(McBuildingSnapshotSet *set){
    if(set == NULL) return;
    for(size_t i = 0; i < set->count; i++) mc_building_snapshot_destroy(&set->snapshots[i]);
    free(set->snapshots);
    *set = (McBuildingSnapshotSet){0};
}

static McStatus snapshot_set_reserve(McBuildingSnapshotSet *set, size_t needed){
    if(needed <= set->capacity) return MC_OK;
    size_t capacity = set->capacity == 0 ? 16 : set->capacity;
    while(capacity < needed){
        if(capacity > SIZE_MAX / 2){ capacity = needed; break; }
        capacity *= 2;
    }
    if(capacity > SIZE_MAX / sizeof(*set->snapshots)) return MC_CAPACITY_EXCEEDED;
    McBuildingSnapshot *snapshots = realloc(set->snapshots, capacity * sizeof(*snapshots));
    if(snapshots == NULL) return MC_OUT_OF_MEMORY;
    for(size_t i = set->capacity; i < capacity; i++) mc_building_snapshot_init(&snapshots[i]);
    set->snapshots = snapshots;
    set->capacity = capacity;
    return MC_OK;
}

McStatus mc_building_snapshot_set_capture(McBuildingSnapshotSet *set, const McBuildingStore *store){
    if(set == NULL || store == NULL || store->count > MC_BUILDING_SNAPSHOT_MAX) return MC_INVALID_ARGUMENT;
    McBuildingSnapshotSet temporary;
    mc_building_snapshot_set_init(&temporary);
    McStatus status = snapshot_set_reserve(&temporary, store->count);
    if(status != MC_OK){ mc_building_snapshot_set_destroy(&temporary); return status; }
    temporary.count = store->count;
    temporary.tick = store->tick;
    for(size_t i = 0; status == MC_OK && i < store->count; i++){
        status = mc_building_snapshot_capture(&temporary.snapshots[i], &store->states[i]);
    }
    if(status == MC_OK) temporary.hash = mc_building_snapshot_set_hash(&temporary);
    if(status != MC_OK){ mc_building_snapshot_set_destroy(&temporary); return status; }
    mc_building_snapshot_set_destroy(set);
    *set = temporary;
    return MC_OK;
}

const McBuildingSnapshot *mc_building_snapshot_set_find(const McBuildingSnapshotSet *set, McEntityId entity_id){
    if(set == NULL || entity_id == MC_ENTITY_NONE) return NULL;
    for(size_t i = 0; i < set->count; i++){
        if(set->snapshots[i].initialized && set->snapshots[i].state.entity_id == entity_id) return &set->snapshots[i];
    }
    return NULL;
}

McStatus mc_building_snapshot_set_restore(const McBuildingSnapshotSet *set, McBuildingStore *store){
    if(set == NULL || store == NULL) return MC_INVALID_ARGUMENT;
    for(size_t i = store->count; i > 0; i--){
        McEntityId id = store->states[i - 1].entity_id;
        if(mc_building_snapshot_set_find(set, id) == NULL){
            McStatus status = mc_building_store_remove(store, id);
            if(status != MC_OK) return status;
        }
    }
    for(size_t i = 0; i < set->count; i++){
        const McBuildingState *saved = &set->snapshots[i].state;
        McBuildingState *current = mc_building_store_find(store, saved->entity_id);
        if(current == NULL){
            current = mc_building_store_add(store, saved->entity_id, saved->block, saved->team, saved->tile_x, saved->tile_y);
            if(current == NULL) return MC_CAPACITY_EXCEEDED;
        }
        replace_state(current, saved);
    }
    store->tick = set->tick;
    /* A snapshot is a transactional restore: network IDs are state, not a
       derived cosmetic value. The next deterministic store tick may rebuild
       them after all links have been restored by the caller. */
    return MC_OK;
}

uint64_t mc_building_snapshot_set_hash(const McBuildingSnapshotSet *set){
    if(set == NULL) return 0;
    uint64_t hash = snapshot_hash_bytes(UINT64_C(1469598103934665603), &set->tick, sizeof(set->tick));
    for(size_t i = 0; i < set->count; i++){
        uint64_t value = set->snapshots[i].hash;
        hash = snapshot_hash_bytes(hash, &value, sizeof(value));
    }
    return hash;
}

bool mc_building_snapshot_set_equal(const McBuildingSnapshotSet *left, const McBuildingSnapshotSet *right){
    if(left == NULL || right == NULL) return left == right;
    if(left->count != right->count || left->tick != right->tick || mc_building_snapshot_set_hash(left) != mc_building_snapshot_set_hash(right)) return false;
    for(size_t i = 0; i < left->count; i++){
        const McBuildingSnapshot *other = mc_building_snapshot_set_find(right, left->snapshots[i].state.entity_id);
        if(other == NULL || !mc_building_snapshot_equal(&left->snapshots[i], other)) return false;
    }
    return true;
}

static McStatus snapshot_write_u64(McBuffer *output, uint64_t value){
    for(int shift = 56; shift >= 0; shift -= 8){
        McStatus status = mc_buffer_write_u8(output, (uint8_t)(value >> shift));
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

static McStatus snapshot_read_u64(McBuffer *input, uint64_t *value){
    if(input == NULL || value == NULL || input->position > input->size || input->size - input->position < 8) return MC_FORMAT_ERROR;
    uint64_t result = 0;
    for(size_t i = 0; i < 8; i++) result = (result << 8) | input->data[input->position++];
    *value = result;
    return MC_OK;
}

McStatus mc_building_snapshot_set_write(const McBuildingSnapshotSet *set, McBuffer *output){
    if(set == NULL || output == NULL || set->count > UINT32_MAX) return MC_INVALID_ARGUMENT;
    McBuildingStore temporary;
    mc_building_store_init(&temporary);
    temporary.tick = set->tick;
    for(size_t i = 0; i < set->count; i++){
        const McBuildingState *saved = &set->snapshots[i].state;
        McBuildingState *state = mc_building_store_add(&temporary, saved->entity_id, saved->block, saved->team, saved->tile_x, saved->tile_y);
        if(state == NULL){ mc_building_store_destroy(&temporary); return MC_CAPACITY_EXCEEDED; }
        replace_state(state, saved);
    }
    McBuffer store_payload;
    mc_buffer_init(&store_payload);
    McStatus status = mc_building_store_write(&temporary, &store_payload);
    if(status == MC_OK) mc_buffer_clear(output);
    if(status == MC_OK) status = mc_buffer_write_bytes(output, (uint8_t[]){MC_BUILDING_SNAPSHOT_MAGIC_0,
        MC_BUILDING_SNAPSHOT_MAGIC_1, MC_BUILDING_SNAPSHOT_MAGIC_2, MC_BUILDING_SNAPSHOT_MAGIC_3}, 4);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, MC_BUILDING_SNAPSHOT_VERSION);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, 0);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)set->count);
    if(status == MC_OK) status = snapshot_write_u64(output, set->tick);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)store_payload.size);
    if(status == MC_OK) status = mc_buffer_write_bytes(output, store_payload.data, store_payload.size);
    mc_buffer_destroy(&store_payload);
    mc_building_store_destroy(&temporary);
    return status;
}

McStatus mc_building_snapshot_set_read(const uint8_t *data, size_t size, McBuildingSnapshotSet *set){
    if(data == NULL || set == NULL || size < 4 + 2 + 2 + 4 + 8 + 4) return MC_INVALID_ARGUMENT;
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint8_t magic[4] = {0};
    uint16_t version = 0, reserved = 0;
    uint32_t count = 0, payload_size = 0;
    uint64_t tick = 0;
    McStatus status = mc_buffer_read_bytes(&input, magic, 4);
    if(status == MC_OK && (magic[0] != MC_BUILDING_SNAPSHOT_MAGIC_0 || magic[1] != MC_BUILDING_SNAPSHOT_MAGIC_1 ||
                           magic[2] != MC_BUILDING_SNAPSHOT_MAGIC_2 || magic[3] != MC_BUILDING_SNAPSHOT_MAGIC_3)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &version);
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &reserved);
    if(status == MC_OK && (version != MC_BUILDING_SNAPSHOT_VERSION || reserved != 0)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &count);
    if(status == MC_OK && count > MC_BUILDING_SNAPSHOT_MAX) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = snapshot_read_u64(&input, &tick);
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &payload_size);
    if(status == MC_OK && payload_size > input.size - input.position) status = MC_FORMAT_ERROR;
    McBuildingStore temporary_store;
    mc_building_store_init(&temporary_store);
    if(status == MC_OK) status = mc_building_store_read(input.data + input.position, payload_size, &temporary_store);
    if(status == MC_OK) input.position += payload_size;
    if(status == MC_OK && input.position != input.size) status = MC_FORMAT_ERROR;
    McBuildingSnapshotSet temporary;
    mc_building_snapshot_set_init(&temporary);
    if(status == MC_OK && temporary_store.count != count) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_building_snapshot_set_capture(&temporary, &temporary_store);
    if(status == MC_OK && temporary.tick != tick) status = MC_FORMAT_ERROR;
    if(status == MC_OK){
        mc_building_snapshot_set_destroy(set);
        *set = temporary;
    }else{
        mc_building_snapshot_set_destroy(&temporary);
    }
    mc_building_store_destroy(&temporary_store);
    return status;
}
