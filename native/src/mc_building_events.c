#include "mindustry_building_events.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MC_BUILDING_EVENT_MAGIC_0 ((uint8_t)'M')
#define MC_BUILDING_EVENT_MAGIC_1 ((uint8_t)'C')
#define MC_BUILDING_EVENT_MAGIC_2 ((uint8_t)'B')
#define MC_BUILDING_EVENT_MAGIC_3 ((uint8_t)'E')
#define MC_BUILDING_EVENT_VERSION 1u
#define MC_BUILDING_EVENT_MAX_RECORDS ((size_t)1000000)

static McStatus event_write_u64(McBuffer *output, uint64_t value){
    McStatus status = MC_OK;
    for(int shift = 56; shift >= 0 && status == MC_OK; shift -= 8){
        status = mc_buffer_write_u8(output, (uint8_t)(value >> shift));
    }
    return status;
}

static McStatus event_read_u64(McBuffer *input, uint64_t *value){
    if(input == NULL || value == NULL || input->position > input->size || input->size - input->position < 8) return MC_FORMAT_ERROR;
    uint64_t result = 0;
    for(size_t i = 0; i < 8; i++) result = (result << 8) | input->data[input->position++];
    *value = result;
    return MC_OK;
}

static McStatus event_write_f32(McBuffer *output, float value){
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return mc_buffer_write_u32_be(output, bits);
}

static McStatus event_read_f32(McBuffer *input, float *value){
    uint32_t bits = 0;
    McStatus status = mc_buffer_read_u32_be(input, &bits);
    if(status != MC_OK) return status;
    memcpy(value, &bits, sizeof(bits));
    return MC_OK;
}

static McStatus reserve_events(McBuildingEventLog *log, size_t needed){
    if(log == NULL || needed < log->count) return MC_INVALID_ARGUMENT;
    if(needed <= log->capacity) return MC_OK;
    size_t capacity = log->capacity == 0 ? 32 : log->capacity;
    while(capacity < needed){
        if(capacity > SIZE_MAX / 2){ capacity = needed; break; }
        capacity *= 2;
    }
    if(capacity > SIZE_MAX / sizeof(*log->events)) return MC_CAPACITY_EXCEEDED;
    McBuildingEvent *events = realloc(log->events, capacity * sizeof(*events));
    if(events == NULL) return MC_OUT_OF_MEMORY;
    log->events = events;
    log->capacity = capacity;
    return MC_OK;
}

void mc_building_event_log_init(McBuildingEventLog *log, size_t max_events){
    if(log == NULL) return;
    *log = (McBuildingEventLog){.max_events = max_events, .next_sequence = 1};
}

void mc_building_event_log_destroy(McBuildingEventLog *log){
    if(log == NULL) return;
    free(log->events);
    *log = (McBuildingEventLog){0};
}

McStatus mc_building_event_log_clear(McBuildingEventLog *log){
    if(log == NULL) return MC_INVALID_ARGUMENT;
    log->count = 0;
    log->next_sequence = 1;
    log->dropped = 0;
    log->last_tick = 0;
    return MC_OK;
}

McStatus mc_building_event_log_set_limit(McBuildingEventLog *log, size_t max_events){
    if(log == NULL) return MC_INVALID_ARGUMENT;
    log->max_events = max_events;
    if(max_events != 0 && log->count > max_events){
        size_t removed = log->count - max_events;
        memmove(log->events, log->events + removed, max_events * sizeof(*log->events));
        log->count = max_events;
        log->dropped += removed;
    }
    return MC_OK;
}

static McStatus event_validate_internal(const McBuildingEvent *event){
    if(event == NULL || event->kind == MC_BUILDING_EVENT_NONE || event->kind > MC_BUILDING_EVENT_CUSTOM ||
       event->entity_id == MC_ENTITY_NONE || event->team >= MC_TEAM_COUNT ||
       !isfinite(event->value) || !isfinite(event->secondary_value)) return MC_INVALID_ARGUMENT;
    if(event->item >= MC_ITEM_COUNT || event->liquid >= MC_BUILDING_LIQUID_COUNT) return MC_INVALID_ARGUMENT;
    if(event->kind == MC_BUILDING_EVENT_PLACED && event->related_id != MC_ENTITY_NONE) return MC_INVALID_ARGUMENT;
    return MC_OK;
}

McStatus mc_building_event_validate(const McBuildingEvent *event){
    return event_validate_internal(event);
}

McStatus mc_building_event_log_append(McBuildingEventLog *log, const McBuildingEvent *event){
    if(log == NULL || event == NULL) return MC_INVALID_ARGUMENT;
    McStatus status = event_validate_internal(event);
    if(status != MC_OK) return status;
    if(log->max_events != 0 && log->max_events < 1) return MC_CAPACITY_EXCEEDED;
    McBuildingEvent copy = *event;
    if(log->next_sequence == 0) log->next_sequence = 1;
    copy.sequence = log->next_sequence++;
    if(copy.tick < log->last_tick) return MC_INVALID_ARGUMENT;
    if(log->max_events != 0 && log->count == log->max_events){
        if(log->count > 1) memmove(log->events, log->events + 1, (log->count - 1) * sizeof(*log->events));
        log->count--;
        log->dropped++;
    }
    status = reserve_events(log, log->count + 1);
    if(status != MC_OK) return status;
    log->events[log->count++] = copy;
    log->last_tick = copy.tick;
    return MC_OK;
}

McStatus mc_building_event_log_append_simple(McBuildingEventLog *log, uint64_t tick,
                                             McEntityId entity_id, McBuildingEventKind kind){
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .kind = kind,
        .team = MC_TEAM_SHARDED, .item = MC_ITEM_SCRAP, .liquid = MC_LIQUID_WATER
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_item(McBuildingEventLog *log, uint64_t tick,
                                           McEntityId entity_id, McEntityId related_id,
                                           McBuildingEventKind kind, McItemId item, uint32_t amount){
    if(item >= MC_ITEM_COUNT) return MC_INVALID_ARGUMENT;
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .related_id = related_id, .kind = kind,
        .team = MC_TEAM_SHARDED, .item = item, .liquid = MC_LIQUID_WATER, .amount = amount
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_liquid(McBuildingEventLog *log, uint64_t tick,
                                             McEntityId entity_id, McEntityId related_id,
                                             McBuildingEventKind kind, McLiquidId liquid, float amount){
    if(liquid >= MC_BUILDING_LIQUID_COUNT || !isfinite(amount) || amount < 0.0f) return MC_INVALID_ARGUMENT;
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .related_id = related_id, .kind = kind,
        .team = MC_TEAM_SHARDED, .item = MC_ITEM_SCRAP, .liquid = liquid, .value = amount
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_power(McBuildingEventLog *log, uint64_t tick,
                                            McEntityId entity_id, McEntityId related_id,
                                            McBuildingEventKind kind, float amount){
    if(!isfinite(amount) || amount < 0.0f) return MC_INVALID_ARGUMENT;
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .related_id = related_id, .kind = kind,
        .team = MC_TEAM_SHARDED, .item = MC_ITEM_SCRAP, .liquid = MC_LIQUID_WATER, .value = amount
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_health(McBuildingEventLog *log, uint64_t tick,
                                             McEntityId entity_id, McBuildingEventKind kind,
                                             float amount, float health){
    if(!isfinite(amount) || amount < 0.0f || !isfinite(health) || health < 0.0f) return MC_INVALID_ARGUMENT;
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .kind = kind,
        .team = MC_TEAM_SHARDED, .item = MC_ITEM_SCRAP, .liquid = MC_LIQUID_WATER,
        .value = amount, .secondary_value = health
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_production(McBuildingEventLog *log, uint64_t tick,
                                                 uint64_t state_hash, McEntityId entity_id,
                                                 uint16_t recipe, uint32_t amount,
                                                 McBuildingEventKind kind){
    if(recipe >= MC_BUILDING_MAX_RECIPES || (kind != MC_BUILDING_EVENT_PRODUCTION_STARTED &&
       kind != MC_BUILDING_EVENT_PRODUCTION_FINISHED)) return MC_INVALID_ARGUMENT;
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .kind = kind, .team = MC_TEAM_SHARDED,
        .item = MC_ITEM_SCRAP, .liquid = MC_LIQUID_WATER, .amount = amount,
        .secondary_amount = recipe, .state_hash = state_hash
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_network(McBuildingEventLog *log, uint64_t tick,
                                              McEntityId entity_id, McEntityId related_id,
                                              McBuildingLinkKind kind, uint32_t network_id){
    if(kind > MC_BUILDING_LINK_PAYLOAD || network_id == 0) return MC_INVALID_ARGUMENT;
    McBuildingEvent event = {
        .tick = tick, .entity_id = entity_id, .related_id = related_id,
        .kind = MC_BUILDING_EVENT_NETWORK_CHANGED, .team = MC_TEAM_SHARDED,
        .item = MC_ITEM_SCRAP, .liquid = MC_LIQUID_WATER, .amount = network_id,
        .secondary_amount = kind
    };
    return mc_building_event_log_append(log, &event);
}

McStatus mc_building_event_log_append_custom(McBuildingEventLog *log, const McBuildingEvent *event,
                                             uint32_t flags){
    if(log == NULL || event == NULL) return MC_INVALID_ARGUMENT;
    McBuildingEvent copy = *event;
    copy.kind = MC_BUILDING_EVENT_CUSTOM;
    copy.flags = flags;
    if(copy.team >= MC_TEAM_COUNT) copy.team = MC_TEAM_SHARDED;
    if(copy.item >= MC_ITEM_COUNT) copy.item = MC_ITEM_SCRAP;
    if(copy.liquid >= MC_BUILDING_LIQUID_COUNT) copy.liquid = MC_LIQUID_WATER;
    return mc_building_event_log_append(log, &copy);
}

size_t mc_building_event_log_count_since(const McBuildingEventLog *log, uint64_t sequence){
    if(log == NULL) return 0;
    size_t count = 0;
    for(size_t i = 0; i < log->count; i++) if(log->events[i].sequence > sequence) count++;
    return count;
}

const McBuildingEvent *mc_building_event_log_get(const McBuildingEventLog *log, size_t index){
    if(log == NULL || index >= log->count) return NULL;
    return &log->events[index];
}

McStatus mc_building_event_log_copy_range(const McBuildingEventLog *log, uint64_t first_sequence,
                                          uint64_t last_sequence, McBuildingEvent *events,
                                          size_t capacity, size_t *written){
    if(log == NULL || written == NULL || first_sequence > last_sequence || (events == NULL && capacity != 0)) return MC_INVALID_ARGUMENT;
    *written = 0;
    for(size_t i = 0; i < log->count; i++){
        const McBuildingEvent *event = &log->events[i];
        if(event->sequence < first_sequence || event->sequence > last_sequence) continue;
        if(*written >= capacity) return MC_CAPACITY_EXCEEDED;
        events[(*written)++] = *event;
    }
    return MC_OK;
}

void mc_building_event_log_summary(const McBuildingEventLog *log, McBuildingEventKind kind,
                                   McBuildingEventSummary *summary){
    if(summary == NULL) return;
    *summary = (McBuildingEventSummary){0};
    if(log == NULL) return;
    summary->dropped = log->dropped > SIZE_MAX ? SIZE_MAX : (size_t)log->dropped;
    for(size_t i = 0; i < log->count; i++){
        const McBuildingEvent *event = &log->events[i];
        if(kind != MC_BUILDING_EVENT_NONE && event->kind != kind) continue;
        if(summary->count == 0){
            summary->first_sequence = event->sequence;
            summary->first_tick = event->tick;
        }
        summary->last_sequence = event->sequence;
        summary->last_tick = event->tick;
        summary->count++;
        summary->amount += event->amount;
        summary->value += event->value;
    }
}

static uint64_t event_hash_bytes(uint64_t hash, const void *data, size_t size){
    const uint8_t *bytes = data;
    for(size_t i = 0; i < size; i++){
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

uint64_t mc_building_event_log_hash(const McBuildingEventLog *log){
    if(log == NULL) return 0;
    McBuffer output;
    mc_buffer_init(&output);
    if(mc_building_event_log_write(log, &output) != MC_OK){ mc_buffer_destroy(&output); return 0; }
    uint64_t hash = event_hash_bytes(UINT64_C(1469598103934665603), output.data, output.size);
    mc_buffer_destroy(&output);
    return hash;
}

bool mc_building_event_is_resource(const McBuildingEvent *event){
    if(event == NULL) return false;
    switch(event->kind){
        case MC_BUILDING_EVENT_ITEM_ADDED:
        case MC_BUILDING_EVENT_ITEM_REMOVED:
        case MC_BUILDING_EVENT_ITEM_TRANSFER:
        case MC_BUILDING_EVENT_LIQUID_ADDED:
        case MC_BUILDING_EVENT_LIQUID_REMOVED:
        case MC_BUILDING_EVENT_LIQUID_TRANSFER:
        case MC_BUILDING_EVENT_POWER_RECEIVED:
        case MC_BUILDING_EVENT_POWER_CONSUMED:
        case MC_BUILDING_EVENT_POWER_TRANSFER:
        case MC_BUILDING_EVENT_PRODUCTION_FINISHED:
        case MC_BUILDING_EVENT_DRILL_OUTPUT:
            return true;
        default: return false;
    }
}

bool mc_building_event_is_terminal(const McBuildingEvent *event){
    if(event == NULL) return false;
    return event->kind == MC_BUILDING_EVENT_REMOVED || event->kind == MC_BUILDING_EVENT_DAMAGED;
}

const char *mc_building_event_name(McBuildingEventKind kind){
    static const char *names[] = {
        "none", "placed", "removed", "damaged", "healed", "item-added", "item-removed",
        "item-transfer", "liquid-added", "liquid-removed", "liquid-transfer", "power-received",
        "power-consumed", "power-transfer", "production-started", "production-finished", "drill-output",
        "turret-shot", "processor-step", "config-changed", "enabled-changed", "network-changed", "custom"
    };
    return kind <= MC_BUILDING_EVENT_CUSTOM ? names[kind] : "unknown";
}

static McStatus event_write_record(const McBuildingEvent *event, McBuffer *output){
    McStatus status = event_write_u64(output, event->sequence);
    if(status == MC_OK) status = event_write_u64(output, event->tick);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, event->entity_id);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, event->related_id);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)event->kind);
    if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)event->team);
    if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)event->item);
    if(status == MC_OK) status = mc_buffer_write_u8(output, (uint8_t)event->liquid);
    if(status == MC_OK) status = mc_buffer_write_u8(output, 0);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, event->amount);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, event->secondary_amount);
    if(status == MC_OK) status = event_write_f32(output, event->value);
    if(status == MC_OK) status = event_write_f32(output, event->secondary_value);
    if(status == MC_OK) status = event_write_u64(output, event->state_hash);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, event->flags);
    return status;
}

static McStatus event_read_record(McBuffer *input, McBuildingEvent *event){
    *event = (McBuildingEvent){0};
    uint16_t kind = 0;
    uint8_t team = 0, item = 0, liquid = 0, reserved = 0;
    McStatus status = event_read_u64(input, &event->sequence);
    if(status == MC_OK) status = event_read_u64(input, &event->tick);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &event->entity_id);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &event->related_id);
    if(status == MC_OK) status = mc_buffer_read_u16_be(input, &kind);
    if(status == MC_OK) status = mc_buffer_read_u8(input, &team);
    if(status == MC_OK) status = mc_buffer_read_u8(input, &item);
    if(status == MC_OK) status = mc_buffer_read_u8(input, &liquid);
    if(status == MC_OK) status = mc_buffer_read_u8(input, &reserved);
    if(status == MC_OK && (reserved != 0 || kind > MC_BUILDING_EVENT_CUSTOM || team >= MC_TEAM_COUNT || item >= MC_ITEM_COUNT || liquid >= MC_BUILDING_LIQUID_COUNT)) status = MC_FORMAT_ERROR;
    event->kind = (McBuildingEventKind)kind;
    event->team = (McTeam)team;
    event->item = (McItemId)item;
    event->liquid = (McLiquidId)liquid;
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &event->amount);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &event->secondary_amount);
    if(status == MC_OK) status = event_read_f32(input, &event->value);
    if(status == MC_OK) status = event_read_f32(input, &event->secondary_value);
    if(status == MC_OK) status = event_read_u64(input, &event->state_hash);
    if(status == MC_OK) status = mc_buffer_read_u32_be(input, &event->flags);
    if(status == MC_OK) status = event_validate_internal(event);
    return status;
}

McStatus mc_building_event_log_write(const McBuildingEventLog *log, McBuffer *output){
    if(log == NULL || output == NULL || log->count > UINT32_MAX || log->max_events > UINT32_MAX) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_bytes(output, (uint8_t[]){MC_BUILDING_EVENT_MAGIC_0, MC_BUILDING_EVENT_MAGIC_1,
        MC_BUILDING_EVENT_MAGIC_2, MC_BUILDING_EVENT_MAGIC_3}, 4);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, MC_BUILDING_EVENT_VERSION);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, 0);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)log->max_events);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, (uint32_t)log->count);
    if(status == MC_OK) status = event_write_u64(output, log->next_sequence);
    if(status == MC_OK) status = event_write_u64(output, log->dropped);
    if(status == MC_OK) status = event_write_u64(output, log->last_tick);
    for(size_t i = 0; status == MC_OK && i < log->count; i++) status = event_write_record(&log->events[i], output);
    return status;
}

McStatus mc_building_event_log_read(const uint8_t *data, size_t size, McBuildingEventLog *log){
    if(data == NULL || log == NULL || size < 4 + 2 + 2 + 4 + 4 + 8 + 8 + 8) return MC_INVALID_ARGUMENT;
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint8_t magic[4] = {0};
    uint16_t version = 0, reserved = 0;
    uint32_t max_events = 0, count = 0;
    McBuildingEventLog decoded;
    mc_building_event_log_init(&decoded, log->max_events);
    McStatus status = mc_buffer_read_bytes(&input, magic, 4);
    if(status == MC_OK && (magic[0] != MC_BUILDING_EVENT_MAGIC_0 || magic[1] != MC_BUILDING_EVENT_MAGIC_1 ||
                           magic[2] != MC_BUILDING_EVENT_MAGIC_2 || magic[3] != MC_BUILDING_EVENT_MAGIC_3)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &version);
    if(status == MC_OK) status = mc_buffer_read_u16_be(&input, &reserved);
    if(status == MC_OK && (version != MC_BUILDING_EVENT_VERSION || reserved != 0)) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &max_events);
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &count);
    if(status == MC_OK && count > MC_BUILDING_EVENT_MAX_RECORDS) status = MC_FORMAT_ERROR;
    if(status == MC_OK) status = event_read_u64(&input, &decoded.next_sequence);
    if(status == MC_OK) status = event_read_u64(&input, &decoded.dropped);
    if(status == MC_OK) status = event_read_u64(&input, &decoded.last_tick);
    if(status == MC_OK){
        decoded.max_events = max_events;
        if(count != 0){
            status = reserve_events(&decoded, count);
            if(status == MC_OK) decoded.count = count;
        }
    }
    for(size_t i = 0; status == MC_OK && i < decoded.count; i++) status = event_read_record(&input, &decoded.events[i]);
    for(size_t i = 1; status == MC_OK && i < decoded.count; i++){
        if(decoded.events[i].sequence <= decoded.events[i - 1].sequence || decoded.events[i].tick < decoded.events[i - 1].tick) status = MC_FORMAT_ERROR;
    }
    if(status == MC_OK && decoded.count != 0 && decoded.last_tick != decoded.events[decoded.count - 1].tick) status = MC_FORMAT_ERROR;
    if(status == MC_OK && input.position != input.size) status = MC_FORMAT_ERROR;
    if(status == MC_OK && decoded.next_sequence == 0) status = MC_FORMAT_ERROR;
    if(status != MC_OK){ mc_building_event_log_destroy(&decoded); return status; }
    mc_building_event_log_destroy(log);
    *log = decoded;
    return MC_OK;
}
