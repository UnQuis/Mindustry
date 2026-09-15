#include "mindustry_building_inspection.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MC_BUILDING_PATH_MAGIC_0 ((uint8_t)'M')
#define MC_BUILDING_PATH_MAGIC_1 ((uint8_t)'C')
#define MC_BUILDING_PATH_MAGIC_2 ((uint8_t)'P')
#define MC_BUILDING_PATH_MAGIC_3 ((uint8_t)'H')
#define MC_BUILDING_PATH_VERSION 1u
#define MC_BUILDING_PATH_MAX ((size_t)1000000)

static float inspection_clamp(float value){
    if(!isfinite(value) || value < 0.0f) return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

const char *mc_building_warning_name(McBuildingWarning warning){
    switch(warning){
        case MC_BUILDING_WARNING_DEAD: return "dead";
        case MC_BUILDING_WARNING_DISABLED: return "disabled";
        case MC_BUILDING_WARNING_UNBUILT: return "unbuilt";
        case MC_BUILDING_WARNING_LOW_HEALTH: return "low-health";
        case MC_BUILDING_WARNING_LOW_POWER: return "low-power";
        case MC_BUILDING_WARNING_FULL_INVENTORY: return "full-inventory";
        case MC_BUILDING_WARNING_EMPTY_INPUT: return "empty-input";
        case MC_BUILDING_WARNING_OUTPUT_BLOCKED: return "output-blocked";
        case MC_BUILDING_WARNING_NO_TARGET: return "no-target";
        case MC_BUILDING_WARNING_NO_NETWORK: return "no-network";
        case MC_BUILDING_WARNING_HEATED: return "heated";
        case MC_BUILDING_WARNING_INVALID: return "invalid";
        case MC_BUILDING_WARNING_NONE: return "none";
        default: return "unknown";
    }
}

void mc_building_inspection_reset(McBuildingInspection *inspection){
    if(inspection == NULL) return;
    *inspection = (McBuildingInspection){0};
}

uint32_t mc_building_state_warnings(const McBuildingState *state){
    if(state == NULL) return MC_BUILDING_WARNING_INVALID;
    uint32_t warnings = 0;
    if(state->dead) warnings |= MC_BUILDING_WARNING_DEAD;
    if(!state->enabled || !state->active) warnings |= MC_BUILDING_WARNING_DISABLED;
    if(state->build_progress < 1.0f) warnings |= MC_BUILDING_WARNING_UNBUILT;
    if(mc_building_state_health_ratio(state) < 0.25f && !state->dead) warnings |= MC_BUILDING_WARNING_LOW_HEALTH;
    if(state->power.capacity > 0.0f && mc_building_state_power_ratio(state) < 0.1f) warnings |= MC_BUILDING_WARNING_LOW_POWER;
    if(state->inventory.capacity > 0 && mc_building_item_space(state) == 0) warnings |= MC_BUILDING_WARNING_FULL_INVENTORY;
    if(state->kind == MC_BUILDING_FACTORY){
        if(state->factory.input_stalls != 0) warnings |= MC_BUILDING_WARNING_EMPTY_INPUT;
        if(state->factory.output_blocked || state->production.output_blocked) warnings |= MC_BUILDING_WARNING_OUTPUT_BLOCKED;
    }
    if(state->kind == MC_BUILDING_TURRET && state->turret.shooting && state->turret.target == MC_ENTITY_NONE){
        warnings |= MC_BUILDING_WARNING_NO_TARGET;
    }
    if(state->kind == MC_BUILDING_POWER && state->power.network_id == 0) warnings |= MC_BUILDING_WARNING_NO_NETWORK;
    if(state->heat > 0.8f) warnings |= MC_BUILDING_WARNING_HEATED;
    McBuildingValidation validation;
    if(mc_building_state_validate(state, &validation) != MC_BUILDING_VALID) warnings |= MC_BUILDING_WARNING_INVALID;
    return warnings;
}

float mc_building_state_priority(const McBuildingState *state){
    if(state == NULL) return 0.0f;
    float priority = (float)mc_building_kind_priority(state->kind);
    uint32_t warnings = mc_building_state_warnings(state);
    priority += (float)mc_building_warning_priority(warnings) * 10.0f;
    priority += (1.0f - mc_building_state_health_ratio(state)) * 4.0f;
    priority += state->kind == MC_BUILDING_CORE ? 100.0f : 0.0f;
    return priority;
}

size_t mc_building_state_warning_count(uint32_t warnings){
    size_t count = 0;
    while(warnings != 0){ count += warnings & 1u; warnings >>= 1; }
    return count;
}

bool mc_building_state_has_warning(uint32_t warnings, McBuildingWarning warning){
    return warning != MC_BUILDING_WARNING_NONE && (warnings & (uint32_t)warning) != 0;
}

McStatus mc_building_state_inspect(const McBuildingState *state, McBuildingInspection *inspection){
    if(inspection == NULL) return MC_INVALID_ARGUMENT;
    mc_building_inspection_reset(inspection);
    if(state == NULL) return MC_INVALID_ARGUMENT;
    inspection->entity_id = state->entity_id;
    inspection->kind = state->kind;
    inspection->team = state->team;
    inspection->warnings = mc_building_state_warnings(state);
    inspection->capabilities = mc_building_capabilities(state->block);
    inspection->power_network = state->power.network_id;
    inspection->liquid_network = state->liquids.network_id;
    inspection->network_count = (size_t)(state->power.network_id != 0) + (size_t)(state->liquids.network_id != 0);
    inspection->item_count = mc_inventory_total(&state->inventory);
    inspection->item_space = mc_building_item_space(state);
    inspection->liquid_volume = mc_building_liquid_amount(state, (McLiquidId)state->liquids.dominant);
    inspection->liquid_space = mc_building_liquid_space(state, (McLiquidId)state->liquids.dominant);
    inspection->power_stored = state->power.stored;
    inspection->power_capacity = state->power.capacity;
    inspection->health_ratio = mc_building_state_health_ratio(state);
    inspection->efficiency = state->efficiency;
    inspection->priority = mc_building_state_priority(state);
    inspection->ready = mc_building_state_is_ready(state);
    inspection->stalled = mc_building_state_is_stalled(state);
    inspection->valid = !mc_building_state_has_warning(inspection->warnings, MC_BUILDING_WARNING_INVALID);
    return MC_OK;
}

void mc_building_store_inspection_reset(McBuildingStoreInspection *inspection){
    if(inspection == NULL) return;
    *inspection = (McBuildingStoreInspection){0};
}

McStatus mc_building_store_inspect(const McBuildingStore *store, McBuildingStoreInspection *inspection){
    if(inspection == NULL || store == NULL) return MC_INVALID_ARGUMENT;
    mc_building_store_inspection_reset(inspection);
    mc_building_store_metrics(store, &inspection->metrics);
    uint32_t network_ids[MC_BUILDING_MAX_LINKS < 256 ? MC_BUILDING_MAX_LINKS : 256];
    inspection->network_count = mc_building_store_network_ids(store, MC_BUILDING_LINK_POWER, network_ids, 256);
    for(size_t i = 0; i < store->count; i++){
        McBuildingInspection state_inspection;
        if(mc_building_state_inspect(&store->states[i], &state_inspection) != MC_OK) continue;
        inspection->warnings |= state_inspection.warnings;
        inspection->warning_count += mc_building_state_warning_count(state_inspection.warnings);
        if(!state_inspection.valid) inspection->invalid_count++;
        inspection->health_score += state_inspection.health_ratio;
        inspection->power_score += state_inspection.power_capacity <= 0.0f ? 1.0f : state_inspection.power_stored / state_inspection.power_capacity;
        inspection->storage_score += state_inspection.item_space == 0 && state_inspection.item_count != 0 ? 1.0f : 0.5f;
    }
    if(store->count != 0){
        inspection->health_score /= (float)store->count;
        inspection->power_score = inspection->power_score / (float)store->count;
        inspection->storage_score = inspection->storage_score / (float)store->count;
    }
    inspection->throughput_score = mc_building_store_throughput_score(store);
    inspection->valid = inspection->metrics.valid && inspection->invalid_count == 0;
    return inspection->valid ? MC_OK : MC_FORMAT_ERROR;
}

static uint64_t inspection_state_hash(const McBuildingState *state){
    McBuildingState copy = *state;
    copy.config = NULL;
    uint64_t hash = UINT64_C(1469598103934665603);
    const uint8_t *bytes = (const uint8_t *)&copy;
    for(size_t i = 0; i < sizeof(copy); i++){ hash ^= bytes[i]; hash *= UINT64_C(1099511628211); }
    for(size_t i = 0; i < state->config_size; i++){ hash ^= state->config[i]; hash *= UINT64_C(1099511628211); }
    return hash;
}

McStatus mc_building_store_compare(const McBuildingStore *left, const McBuildingStore *right,
                                   McBuildingStoreDifference *difference){
    if(left == NULL || right == NULL || difference == NULL) return MC_INVALID_ARGUMENT;
    *difference = (McBuildingStoreDifference){.same_tick = left->tick == right->tick};
    for(size_t i = 0; i < left->count; i++){
        const McBuildingState *other = mc_building_store_find_const(right, left->states[i].entity_id);
        if(other == NULL) difference->removed++;
        else if(inspection_state_hash(&left->states[i]) != inspection_state_hash(other)) difference->changed++;
        else difference->unchanged++;
    }
    for(size_t i = 0; i < right->count; i++) if(mc_building_store_find_const(left, right->states[i].entity_id) == NULL) difference->added++;
    for(size_t i = 0; i < left->link_count; i++){
        const McBuildingLink *link = &left->links[i];
        McBuildingLink *other = mc_building_store_find_link((McBuildingStore *)right, link->from, link->to, link->kind);
        if(other == NULL || other->capacity != link->capacity || other->active != link->active) difference->link_changes++;
    }
    for(size_t i = 0; i < right->link_count; i++){
        const McBuildingLink *link = &right->links[i];
        if(mc_building_store_find_link((McBuildingStore *)left, link->from, link->to, link->kind) == NULL) difference->link_changes++;
    }
    difference->item_delta = mc_building_store_total_items(right, MC_ITEM_COPPER) - mc_building_store_total_items(left, MC_ITEM_COPPER);
    difference->liquid_delta = mc_building_store_total_liquid(right, MC_LIQUID_WATER) - mc_building_store_total_liquid(left, MC_LIQUID_WATER);
    difference->power_delta = mc_building_store_total_power(right) - mc_building_store_total_power(left);
    return MC_OK;
}

float mc_building_store_capacity_score(const McBuildingStore *store){
    if(store == NULL || store->count == 0) return 0.0f;
    float score = 0.0f;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state->inventory.capacity > 0) score += (float)mc_inventory_total(&state->inventory) / (float)state->inventory.capacity;
        if(state->liquids.capacity > 0.0f) score += mc_building_state_liquid_ratio(state);
        if(state->power.capacity > 0.0f) score += mc_building_state_power_ratio(state);
    }
    return inspection_clamp(score / (float)store->count);
}

float mc_building_store_throughput_score(const McBuildingStore *store){
    if(store == NULL || store->count == 0) return 0.0f;
    float score = 0.0f;
    for(size_t i = 0; i < store->count; i++){
        const McBuildingState *state = &store->states[i];
        if(state->kind == MC_BUILDING_CONVEYOR) score += state->conveyor.speed * (state->conveyor.moved == 0 ? 0.25f : 1.0f);
        if(state->kind == MC_BUILDING_DRILL) score += state->drill.mined == 0 ? 0.1f : 1.0f;
        if(state->kind == MC_BUILDING_FACTORY) score += state->factory.crafts == 0 ? 0.1f : 1.0f;
        if(state->kind == MC_BUILDING_TURRET) score += state->turret.shots == 0 ? 0.1f : 1.0f;
    }
    return inspection_clamp(score / (float)store->count);
}

float mc_building_store_power_score(const McBuildingStore *store){
    if(store == NULL || store->count == 0) return 0.0f;
    float supply = 0.0f;
    float demand = 0.0f;
    for(size_t i = 0; i < store->count; i++){
        supply += store->states[i].power.production + store->states[i].power.stored;
        demand += store->states[i].power.consumption + store->states[i].power.capacity;
    }
    return demand <= 0.0f ? 1.0f : inspection_clamp(supply / demand);
}

float mc_building_store_storage_score(const McBuildingStore *store){
    if(store == NULL || store->count == 0) return 0.0f;
    float full = 0.0f;
    float capacity = 0.0f;
    for(size_t i = 0; i < store->count; i++){
        capacity += (float)store->states[i].inventory.capacity;
        full += (float)mc_inventory_total(&store->states[i].inventory);
    }
    return capacity <= 0.0f ? 0.0f : inspection_clamp(full / capacity);
}

void mc_building_path_init(McBuildingPath *path, McBuildingLinkKind kind){
    if(path == NULL) return;
    *path = (McBuildingPath){.kind = kind};
}

void mc_building_path_destroy(McBuildingPath *path){
    if(path == NULL) return;
    free(path->ids);
    *path = (McBuildingPath){0};
}

static McStatus path_reserve(McBuildingPath *path, size_t needed){
    if(needed <= path->capacity) return MC_OK;
    size_t capacity = path->capacity == 0 ? 8 : path->capacity;
    while(capacity < needed){
        if(capacity > SIZE_MAX / 2){ capacity = needed; break; }
        capacity *= 2;
    }
    McEntityId *ids = realloc(path->ids, capacity * sizeof(*ids));
    if(ids == NULL) return MC_OUT_OF_MEMORY;
    path->ids = ids;
    path->capacity = capacity;
    return MC_OK;
}

static bool path_edge_matches(const McBuildingLink *link, McEntityId current, McBuildingLinkKind kind,
                              McEntityId *next){
    if(!link->active || link->kind != kind) return false;
    if(link->from == current){ *next = link->to; return true; }
    if((kind == MC_BUILDING_LINK_POWER || kind == MC_BUILDING_LINK_LIQUID) && link->to == current){
        *next = link->from;
        return true;
    }
    return false;
}

McStatus mc_building_store_find_path(const McBuildingStore *store, McEntityId from, McEntityId to,
                                     McBuildingLinkKind kind, McBuildingPath *path){
    if(store == NULL || path == NULL || from == MC_ENTITY_NONE || to == MC_ENTITY_NONE || kind > MC_BUILDING_LINK_PAYLOAD){
        return MC_INVALID_ARGUMENT;
    }
    mc_building_path_destroy(path);
    mc_building_path_init(path, kind);
    size_t start = SIZE_MAX;
    size_t target = SIZE_MAX;
    for(size_t i = 0; i < store->count; i++){
        if(store->states[i].entity_id == from) start = i;
        if(store->states[i].entity_id == to) target = i;
    }
    if(start == SIZE_MAX || target == SIZE_MAX) return MC_NOT_FOUND;
    size_t *queue = malloc(store->count * sizeof(*queue));
    size_t *previous = malloc(store->count * sizeof(*previous));
    bool *visited = calloc(store->count, sizeof(*visited));
    if((store->count != 0) && (queue == NULL || previous == NULL || visited == NULL)){
        free(queue); free(previous); free(visited); return MC_OUT_OF_MEMORY;
    }
    for(size_t i = 0; i < store->count; i++) previous[i] = SIZE_MAX;
    size_t head = 0, tail = 0;
    queue[tail++] = start;
    visited[start] = true;
    while(head < tail){
        size_t current_index = queue[head++];
        if(current_index == target) break;
        McEntityId current = store->states[current_index].entity_id;
        for(size_t link_index = 0; link_index < store->link_count; link_index++){
            McEntityId next_id = MC_ENTITY_NONE;
            if(!path_edge_matches(&store->links[link_index], current, kind, &next_id)) continue;
            size_t next_index = SIZE_MAX;
            for(size_t i = 0; i < store->count; i++) if(store->states[i].entity_id == next_id) next_index = i;
            if(next_index == SIZE_MAX || visited[next_index]) continue;
            visited[next_index] = true;
            previous[next_index] = current_index;
            queue[tail++] = next_index;
        }
    }
    McStatus status = MC_NOT_FOUND;
    if(visited[target]){
        size_t count = 0;
        for(size_t index = target; index != SIZE_MAX; index = previous[index]) count++;
        status = path_reserve(path, count);
        if(status == MC_OK){
            path->count = count;
            size_t write_index = count;
            for(size_t index = target; index != SIZE_MAX; index = previous[index]) path->ids[--write_index] = store->states[index].entity_id;
            path->found = true;
        }
    }
    free(queue); free(previous); free(visited);
    return status;
}

size_t mc_building_store_reachable(const McBuildingStore *store, McEntityId from,
                                   McBuildingLinkKind kind, McEntityId *ids, size_t capacity){
    if(store == NULL || from == MC_ENTITY_NONE || kind > MC_BUILDING_LINK_PAYLOAD) return 0;
    size_t count = 0;
    for(size_t i = 0; i < store->count; i++){
        McBuildingPath path;
        mc_building_path_init(&path, kind);
        if(mc_building_store_find_path(store, from, store->states[i].entity_id, kind, &path) == MC_OK){
            if(ids != NULL && count < capacity) ids[count] = store->states[i].entity_id;
            count++;
        }
        mc_building_path_destroy(&path);
    }
    return count;
}

bool mc_building_store_connected(const McBuildingStore *store, McEntityId from,
                                 McEntityId to, McBuildingLinkKind kind){
    McBuildingPath path;
    mc_building_path_init(&path, kind);
    bool connected = mc_building_store_find_path(store, from, to, kind, &path) == MC_OK;
    mc_building_path_destroy(&path);
    return connected;
}

size_t mc_building_path_length(const McBuildingPath *path){
    return path == NULL ? 0 : path->count;
}

McEntityId mc_building_path_at(const McBuildingPath *path, size_t index){
    return path == NULL || index >= path->count ? MC_ENTITY_NONE : path->ids[index];
}

static McStatus path_write_u64(McBuffer *output, uint64_t value){
    for(int shift = 56; shift >= 0; shift -= 8){
        McStatus status = mc_buffer_write_u8(output, (uint8_t)(value >> shift));
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

static McStatus path_read_u64(McBuffer *input, uint64_t *value){
    if(input == NULL || value == NULL || input->position > input->size || input->size - input->position < 8) return MC_FORMAT_ERROR;
    uint64_t result = 0;
    for(size_t i = 0; i < 8; i++) result = (result << 8) | input->data[input->position++];
    *value = result;
    return MC_OK;
}

McStatus mc_building_path_write(const McBuildingPath *path, McBuffer *output){
    if(path == NULL || output == NULL || path->kind > MC_BUILDING_LINK_PAYLOAD || path->count > UINT32_MAX ||
       (path->ids == NULL && path->count != 0)) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_bytes(output, (uint8_t[]){MC_BUILDING_PATH_MAGIC_0, MC_BUILDING_PATH_MAGIC_1,
        MC_BUILDING_PATH_MAGIC_2, MC_BUILDING_PATH_MAGIC_3}, 4);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, MC_BUILDING_PATH_VERSION);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)path->kind);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)path->count);
    if(status == MC_OK) status = mc_buffer_write_u8(output, path->found ? 1 : 0);
    if(status == MC_OK) status = mc_buffer_write_u8(output, 0);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, 0);
    for(size_t i = 0; status == MC_OK && i < path->count; i++) status = path_write_u64(output, path->ids[i]);
    return status;
}

McStatus mc_building_path_read(const uint8_t *data, size_t size, McBuildingPath *path){
    if(data == NULL || path == NULL || size < 4 + 2 + 2 + 4 + 4) return MC_INVALID_ARGUMENT;
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint8_t magic[4] = {0}, found = 0, reserved8 = 0;
    uint16_t version = 0, kind = 0, reserved16 = 0;
    uint32_t count = 0;
    McStatus status = mc_buffer_read_bytes(&input, magic, 4);
    if(status == MC_OK && (magic[0] != MC_BUILDING_PATH_MAGIC_0 || magic[1] != MC_BUILDING_PATH_MAGIC_1 || magic[2] != MC_BUILDING_PATH_MAGIC_2 || magic[3] != MC_BUILDING_PATH_MAGIC_3)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &version);
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &kind);
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &count);
    if(status == MC_OK) status = mc_buffer_read_u8(&input, &found);
    if(status == MC_OK) status = mc_buffer_read_u8(&input, &reserved8);
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &reserved16);
    if(status == MC_OK && (version != MC_BUILDING_PATH_VERSION || kind > MC_BUILDING_LINK_PAYLOAD || count > MC_BUILDING_PATH_MAX || found > 1 || reserved8 != 0 || reserved16 != 0)) status = MC_FORMAT_ERROR;
    McBuildingPath temporary;
    mc_building_path_init(&temporary, (McBuildingLinkKind)kind);
    if(status == MC_OK && count != 0) status = path_reserve(&temporary, count);
    if(status == MC_OK){
        temporary.count = count;
        temporary.found = found != 0;
        for(size_t i = 0; i < count && status == MC_OK; i++){
            uint64_t id = 0;
            status = path_read_u64(&input, &id);
            if(status == MC_OK && (id == MC_ENTITY_NONE || id > UINT32_MAX)) status = MC_FORMAT_ERROR;
            if(status == MC_OK) temporary.ids[i] = (McEntityId)id;
        }
    }
    if(status == MC_OK && input.position != input.size) status = MC_FORMAT_ERROR;
    if(status == MC_OK){ mc_building_path_destroy(path); *path = temporary; }
    else mc_building_path_destroy(&temporary);
    return status;
}

int mc_building_kind_priority(McBuildingKind kind){
    switch(kind){
        case MC_BUILDING_CORE: return 10;
        case MC_BUILDING_TURRET: return 8;
        case MC_BUILDING_FACTORY: return 7;
        case MC_BUILDING_DRILL: return 6;
        case MC_BUILDING_POWER: return 5;
        case MC_BUILDING_LIQUID: return 4;
        case MC_BUILDING_PROCESSOR: return 3;
        case MC_BUILDING_CONVEYOR: return 2;
        case MC_BUILDING_STORAGE: return 1;
        default: return 0;
    }
}

int mc_building_warning_priority(uint32_t warnings){
    int priority = 0;
    if(warnings & MC_BUILDING_WARNING_INVALID) priority += 100;
    if(warnings & MC_BUILDING_WARNING_DEAD) priority += 90;
    if(warnings & MC_BUILDING_WARNING_LOW_HEALTH) priority += 50;
    if(warnings & MC_BUILDING_WARNING_NO_NETWORK) priority += 30;
    if(warnings & MC_BUILDING_WARNING_LOW_POWER) priority += 20;
    if(warnings & MC_BUILDING_WARNING_OUTPUT_BLOCKED) priority += 15;
    if(warnings & MC_BUILDING_WARNING_EMPTY_INPUT) priority += 10;
    return priority;
}

McBuildingState *mc_building_store_find_highest_priority(McBuildingStore *store,
                                                          McBuildingWarning required_warning){
    if(store == NULL) return NULL;
    McBuildingState *best = NULL;
    float best_priority = -1.0f;
    for(size_t i = 0; i < store->count; i++){
        McBuildingState *state = &store->states[i];
        if(required_warning != MC_BUILDING_WARNING_NONE && !mc_building_state_has_warning(mc_building_state_warnings(state), required_warning)) continue;
        float priority = mc_building_state_priority(state);
        if(best == NULL || priority > best_priority || (priority == best_priority && state->entity_id < best->entity_id)){
            best = state;
            best_priority = priority;
        }
    }
    return best;
}
