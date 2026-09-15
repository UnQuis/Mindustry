#ifndef MINDUSTRY_NATIVE_BUILDING_EVENTS_H
#define MINDUSTRY_NATIVE_BUILDING_EVENTS_H

#include "mindustry_building.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/*
 * The event journal is an optional deterministic audit trail for Building
 * state. It is deliberately external to McBuildingStore so a server can keep
 * a short ring buffer, while a replay tool can keep every event. Events carry
 * values rather than pointers and can therefore be copied between threads or
 * written to a replay stream without borrowing mutable Building memory.
 */
typedef enum McBuildingEventKind{
    MC_BUILDING_EVENT_NONE = 0,
    MC_BUILDING_EVENT_PLACED,
    MC_BUILDING_EVENT_REMOVED,
    MC_BUILDING_EVENT_DAMAGED,
    MC_BUILDING_EVENT_HEALED,
    MC_BUILDING_EVENT_ITEM_ADDED,
    MC_BUILDING_EVENT_ITEM_REMOVED,
    MC_BUILDING_EVENT_ITEM_TRANSFER,
    MC_BUILDING_EVENT_LIQUID_ADDED,
    MC_BUILDING_EVENT_LIQUID_REMOVED,
    MC_BUILDING_EVENT_LIQUID_TRANSFER,
    MC_BUILDING_EVENT_POWER_RECEIVED,
    MC_BUILDING_EVENT_POWER_CONSUMED,
    MC_BUILDING_EVENT_POWER_TRANSFER,
    MC_BUILDING_EVENT_PRODUCTION_STARTED,
    MC_BUILDING_EVENT_PRODUCTION_FINISHED,
    MC_BUILDING_EVENT_DRILL_OUTPUT,
    MC_BUILDING_EVENT_TURRET_SHOT,
    MC_BUILDING_EVENT_PROCESSOR_STEP,
    MC_BUILDING_EVENT_CONFIG_CHANGED,
    MC_BUILDING_EVENT_ENABLED_CHANGED,
    MC_BUILDING_EVENT_NETWORK_CHANGED,
    MC_BUILDING_EVENT_CUSTOM
} McBuildingEventKind;

typedef struct McBuildingEvent{
    uint64_t sequence;
    uint64_t tick;
    McEntityId entity_id;
    McEntityId related_id;
    McBuildingEventKind kind;
    McTeam team;
    McItemId item;
    McLiquidId liquid;
    uint32_t amount;
    uint32_t secondary_amount;
    float value;
    float secondary_value;
    uint64_t state_hash;
    uint32_t flags;
} McBuildingEvent;

typedef struct McBuildingEventLog{
    McBuildingEvent *events;
    size_t count;
    size_t capacity;
    size_t max_events;
    uint64_t next_sequence;
    uint64_t dropped;
    uint64_t last_tick;
} McBuildingEventLog;

typedef struct McBuildingEventSummary{
    uint64_t first_sequence;
    uint64_t last_sequence;
    uint64_t first_tick;
    uint64_t last_tick;
    size_t count;
    size_t dropped;
    uint64_t amount;
    double value;
} McBuildingEventSummary;

MC_API void mc_building_event_log_init(McBuildingEventLog *log, size_t max_events);
MC_API void mc_building_event_log_destroy(McBuildingEventLog *log);
MC_API McStatus mc_building_event_log_clear(McBuildingEventLog *log);
MC_API McStatus mc_building_event_log_set_limit(McBuildingEventLog *log, size_t max_events);
MC_API McStatus mc_building_event_log_append(McBuildingEventLog *log, const McBuildingEvent *event);
MC_API McStatus mc_building_event_log_append_simple(McBuildingEventLog *log, uint64_t tick,
                                                    McEntityId entity_id, McBuildingEventKind kind);
MC_API McStatus mc_building_event_log_append_item(McBuildingEventLog *log, uint64_t tick,
                                                  McEntityId entity_id, McEntityId related_id,
                                                  McBuildingEventKind kind, McItemId item, uint32_t amount);
MC_API McStatus mc_building_event_log_append_liquid(McBuildingEventLog *log, uint64_t tick,
                                                    McEntityId entity_id, McEntityId related_id,
                                                    McBuildingEventKind kind, McLiquidId liquid, float amount);
MC_API McStatus mc_building_event_log_append_power(McBuildingEventLog *log, uint64_t tick,
                                                   McEntityId entity_id, McEntityId related_id,
                                                   McBuildingEventKind kind, float amount);
MC_API McStatus mc_building_event_log_append_health(McBuildingEventLog *log, uint64_t tick,
                                                    McEntityId entity_id, McBuildingEventKind kind,
                                                    float amount, float health);
MC_API McStatus mc_building_event_log_append_production(McBuildingEventLog *log, uint64_t tick,
                                                        uint64_t state_hash, McEntityId entity_id,
                                                        uint16_t recipe, uint32_t amount,
                                                        McBuildingEventKind kind);
MC_API McStatus mc_building_event_log_append_network(McBuildingEventLog *log, uint64_t tick,
                                                     McEntityId entity_id, McEntityId related_id,
                                                     McBuildingLinkKind kind, uint32_t network_id);
MC_API McStatus mc_building_event_log_append_custom(McBuildingEventLog *log, const McBuildingEvent *event,
                                                    uint32_t flags);

MC_API size_t mc_building_event_log_count_since(const McBuildingEventLog *log, uint64_t sequence);
MC_API const McBuildingEvent *mc_building_event_log_get(const McBuildingEventLog *log, size_t index);
MC_API McStatus mc_building_event_log_copy_range(const McBuildingEventLog *log, uint64_t first_sequence,
                                                  uint64_t last_sequence, McBuildingEvent *events,
                                                  size_t capacity, size_t *written);
MC_API void mc_building_event_log_summary(const McBuildingEventLog *log, McBuildingEventKind kind,
                                          McBuildingEventSummary *summary);
MC_API uint64_t mc_building_event_log_hash(const McBuildingEventLog *log);
MC_API bool mc_building_event_is_resource(const McBuildingEvent *event);
MC_API bool mc_building_event_is_terminal(const McBuildingEvent *event);
MC_API const char *mc_building_event_name(McBuildingEventKind kind);
MC_API McStatus mc_building_event_validate(const McBuildingEvent *event);

/* Replay payload codec. The format is big-endian, fixed-width, and independent
   of the MSAV custom chunk codec. */
MC_API McStatus mc_building_event_log_write(const McBuildingEventLog *log, McBuffer *output);
MC_API McStatus mc_building_event_log_read(const uint8_t *data, size_t size, McBuildingEventLog *log);

#endif
