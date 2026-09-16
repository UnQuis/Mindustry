#ifndef MINDUSTRY_NATIVE_BUILDING_RULES_H
#define MINDUSTRY_NATIVE_BUILDING_RULES_H

#include "mindustry_building.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/*
 * Building rules are intentionally separate from the mutable Building state.
 * A rule is the immutable part of a block definition; the state contains only
 * world-owned values. Keeping the two apart makes save validation, previews,
 * UI queries, and deterministic tests use the same rules without mutating a
 * live building.
 */
typedef enum McBuildingCapability{
    MC_BUILDING_CAP_NONE       = 0u,
    MC_BUILDING_CAP_INVENTORY  = 1u << 0,
    MC_BUILDING_CAP_LIQUID     = 1u << 1,
    MC_BUILDING_CAP_POWER      = 1u << 2,
    MC_BUILDING_CAP_PRODUCTION = 1u << 3,
    MC_BUILDING_CAP_DRILL      = 1u << 4,
    MC_BUILDING_CAP_TURRET     = 1u << 5,
    MC_BUILDING_CAP_CONVEYOR   = 1u << 6,
    MC_BUILDING_CAP_CORE       = 1u << 7,
    MC_BUILDING_CAP_PROCESSOR  = 1u << 8,
    MC_BUILDING_CAP_CONFIG     = 1u << 9,
    MC_BUILDING_CAP_LINK       = 1u << 10,
    MC_BUILDING_CAP_DAMAGE     = 1u << 11,
    MC_BUILDING_CAP_TEAM       = 1u << 12
} McBuildingCapability;

typedef struct McBuildingRule{
    McBlockId block;
    McBuildingKind kind;
    const char *name;
    uint16_t size;
    uint32_t health;
    uint32_t inventory_capacity;
    float liquid_capacity;
    float power_capacity;
    float power_generation;
    float power_consumption;
    float build_cost;
    float placement_range;
    uint32_t capabilities;
    bool rotatable;
    bool team_owned;
    bool destructible;
    bool solid;
} McBuildingRule;

typedef enum McBuildingValidationCode{
    MC_BUILDING_VALID = 0,
    MC_BUILDING_INVALID_NULL,
    MC_BUILDING_INVALID_ID,
    MC_BUILDING_INVALID_BLOCK,
    MC_BUILDING_INVALID_TEAM,
    MC_BUILDING_INVALID_GEOMETRY,
    MC_BUILDING_INVALID_HEALTH,
    MC_BUILDING_INVALID_PROGRESS,
    MC_BUILDING_INVALID_TIME_SCALE,
    MC_BUILDING_INVALID_INVENTORY,
    MC_BUILDING_INVALID_LIQUID,
    MC_BUILDING_INVALID_POWER,
    MC_BUILDING_INVALID_SUBSYSTEM,
    MC_BUILDING_INVALID_CONFIG,
    MC_BUILDING_INVALID_LINK,
    MC_BUILDING_INVALID_DUPLICATE,
    MC_BUILDING_INVALID_OVERLAP,
    MC_BUILDING_INVALID_NETWORK
} McBuildingValidationCode;

typedef struct McBuildingValidation{
    McBuildingValidationCode code;
    McEntityId entity_id;
    McEntityId related_id;
    size_t state_index;
    size_t link_index;
    size_t checked_states;
    size_t checked_links;
    size_t error_count;
    bool repaired;
} McBuildingValidation;

typedef struct McBuildingNetworkReport{
    uint32_t network_id;
    McBuildingLinkKind kind;
    size_t member_count;
    size_t link_count;
    float capacity;
    float stored;
    float demand;
    float production;
    float liquid_volume;
    uint32_t item_count[MC_ITEM_COUNT];
    bool connected;
    bool saturated;
} McBuildingNetworkReport;

typedef struct McBuildingStateMetrics{
    uint64_t ticks;
    uint64_t item_units;
    uint64_t liquid_milliunits;
    uint64_t power_milliunits;
    uint64_t produced_units;
    uint64_t consumed_units;
    uint64_t drilled_units;
    uint64_t turret_shots;
    uint64_t processor_instructions;
    float utilization;
    float health_ratio;
    float inventory_ratio;
    float liquid_ratio;
    float power_ratio;
    bool stalled;
    bool healthy;
} McBuildingStateMetrics;

typedef struct McBuildingStoreMetrics{
    uint64_t tick;
    size_t state_count;
    size_t active_count;
    size_t dead_count;
    size_t link_count;
    size_t connected_link_count;
    uint64_t item_units;
    uint64_t produced_units;
    uint64_t consumed_units;
    uint64_t drilled_units;
    uint64_t turret_shots;
    uint64_t processor_instructions;
    float total_liquid;
    float total_power;
    float total_power_capacity;
    float average_efficiency;
    bool valid;
} McBuildingStoreMetrics;

typedef struct McBuildingTransferRequest{
    McEntityId from;
    McEntityId to;
    McBuildingLinkKind kind;
    McItemId item;
    McLiquidId liquid;
    uint32_t item_amount;
    float liquid_amount;
    float power_amount;
    bool all_available;
} McBuildingTransferRequest;

typedef struct McBuildingTransferResult{
    McStatus status;
    McEntityId from;
    McEntityId to;
    McBuildingLinkKind kind;
    uint32_t item_moved;
    float liquid_moved;
    float power_moved;
    float link_capacity;
    bool complete;
} McBuildingTransferResult;

typedef struct McBuildingConfigView{
    const uint8_t *data;
    size_t size;
    size_t position;
    bool valid;
} McBuildingConfigView;

typedef struct McBuildingConfigWriter{
    McBuffer *output;
    size_t fields;
    bool valid;
} McBuildingConfigWriter;

/* Immutable definitions and capability queries. */
MC_API const McBuildingRule *mc_building_rule(McBlockId block);
MC_API const McBuildingRule *mc_building_rule_for_kind(McBuildingKind kind, size_t ordinal);
MC_API size_t mc_building_rule_count(void);
MC_API uint32_t mc_building_capabilities(McBlockId block);
MC_API bool mc_building_has_capability(McBlockId block, McBuildingCapability capability);
MC_API bool mc_building_can_rotate(McBlockId block);
MC_API bool mc_building_is_solid(McBlockId block);
MC_API bool mc_building_is_team_owned(McBlockId block);
MC_API float mc_building_rule_power_balance(McBlockId block);
MC_API float mc_building_rule_build_cost(McBlockId block);
MC_API bool mc_building_rule_accepts_item(McBlockId block, McItemId item);
MC_API bool mc_building_rule_accepts_liquid(McBlockId block, McLiquidId liquid);
MC_API bool mc_building_rule_accepts_power(McBlockId block);

/* State normalization and standalone validation. */
MC_API McStatus mc_building_state_normalize(McBuildingState *state, bool repair);
MC_API McBuildingValidationCode mc_building_state_validate(const McBuildingState *state,
                                                            McBuildingValidation *validation);
MC_API bool mc_building_state_is_ready(const McBuildingState *state);
MC_API bool mc_building_state_is_stalled(const McBuildingState *state);
MC_API float mc_building_state_health_ratio(const McBuildingState *state);
MC_API float mc_building_state_inventory_ratio(const McBuildingState *state);
MC_API float mc_building_state_liquid_ratio(const McBuildingState *state);
MC_API float mc_building_state_power_ratio(const McBuildingState *state);
MC_API void mc_building_state_metrics(const McBuildingState *state, McBuildingStateMetrics *metrics);
MC_API McStatus mc_building_state_set_flag(McBuildingState *state, uint32_t flag, bool enabled);
MC_API bool mc_building_state_has_flag(const McBuildingState *state, uint32_t flag);
MC_API McStatus mc_building_state_set_position(McBuildingState *state, uint16_t x, uint16_t y);
MC_API McStatus mc_building_state_set_size(McBuildingState *state, uint16_t size);
MC_API McStatus mc_building_state_set_heat(McBuildingState *state, float heat);
MC_API McStatus mc_building_state_set_efficiency(McBuildingState *state, float efficiency);
MC_API McStatus mc_building_state_set_active(McBuildingState *state, bool active);
MC_API McStatus mc_building_state_set_liquid_capacity(McBuildingState *state, float capacity);
MC_API McStatus mc_building_state_set_power_demand(McBuildingState *state, float demand);
MC_API McStatus mc_building_state_set_power_generation(McBuildingState *state, float generation);

/* Store validation, spatial queries, deterministic selection and metrics. */
MC_API McBuildingValidationCode mc_building_store_validate(const McBuildingStore *store,
                                                            McBuildingValidation *validation);
MC_API McStatus mc_building_store_normalize(McBuildingStore *store, bool repair,
                                             McBuildingValidation *validation);
MC_API size_t mc_building_store_collect_ids(const McBuildingStore *store, McBuildingKind kind,
                                            McTeam team, McEntityId *ids, size_t capacity);
MC_API size_t mc_building_store_collect_capability(const McBuildingStore *store, McBuildingCapability capability,
                                                   McEntityId *ids, size_t capacity);
MC_API McBuildingState *mc_building_store_find_nearest(McBuildingStore *store, uint16_t x, uint16_t y,
                                                        McTeam team, uint32_t capabilities);
MC_API const McBuildingState *mc_building_store_find_nearest_const(const McBuildingStore *store,
                                                                    uint16_t x, uint16_t y, McTeam team,
                                                                    uint32_t capabilities);
MC_API size_t mc_building_store_count_kind(const McBuildingStore *store, McBuildingKind kind);
MC_API size_t mc_building_store_count_team(const McBuildingStore *store, McTeam team);
MC_API void mc_building_store_metrics(const McBuildingStore *store, McBuildingStoreMetrics *metrics);
MC_API McStatus mc_building_store_network_report(const McBuildingStore *store, uint32_t network_id,
                                                  McBuildingLinkKind kind, McBuildingNetworkReport *report);
MC_API size_t mc_building_store_network_ids(const McBuildingStore *store, McBuildingLinkKind kind,
                                             uint32_t *ids, size_t capacity);
MC_API bool mc_building_store_has_overlap(const McBuildingStore *store, uint16_t x, uint16_t y,
                                          uint16_t size, McEntityId ignore);
MC_API McStatus mc_building_store_validate_placement(const McBuildingStore *store, McBlockId block,
                                                      McTeam team, uint16_t x, uint16_t y);

/* Explicit transfer batches make logistics deterministic and inspectable. */
MC_API McBuildingTransferResult mc_building_store_transfer(const McBuildingStore *store,
                                                           const McBuildingTransferRequest *request);
MC_API McStatus mc_building_store_transfer_apply(McBuildingStore *store,
                                                 const McBuildingTransferRequest *request,
                                                 McBuildingTransferResult *result);
MC_API size_t mc_building_store_transfer_all(McBuildingStore *store, McBuildingLinkKind kind,
                                              McBuildingTransferResult *results, size_t capacity);
MC_API void mc_building_store_reset_link_counters(McBuildingStore *store);
MC_API float mc_building_store_total_liquid(const McBuildingStore *store, McLiquidId liquid);
MC_API float mc_building_store_total_power(const McBuildingStore *store);
MC_API uint64_t mc_building_store_total_items(const McBuildingStore *store, McItemId item);

/* Small, endian-stable typed config helpers for native configuration payloads. */
MC_API void mc_building_config_view_init(McBuildingConfigView *view, const McBuildingState *state);
MC_API bool mc_building_config_read_u8(McBuildingConfigView *view, uint8_t *value);
MC_API bool mc_building_config_read_u16(McBuildingConfigView *view, uint16_t *value);
MC_API bool mc_building_config_read_u32(McBuildingConfigView *view, uint32_t *value);
MC_API bool mc_building_config_read_f32(McBuildingConfigView *view, float *value);
MC_API bool mc_building_config_read_bool(McBuildingConfigView *view, bool *value);
MC_API bool mc_building_config_read_bytes(McBuildingConfigView *view, uint8_t *data, size_t size);
MC_API void mc_building_config_writer_init(McBuildingConfigWriter *writer, McBuffer *output);
MC_API bool mc_building_config_write_u8(McBuildingConfigWriter *writer, uint8_t value);
MC_API bool mc_building_config_write_u16(McBuildingConfigWriter *writer, uint16_t value);
MC_API bool mc_building_config_write_u32(McBuildingConfigWriter *writer, uint32_t value);
MC_API bool mc_building_config_write_f32(McBuildingConfigWriter *writer, float value);
MC_API bool mc_building_config_write_bool(McBuildingConfigWriter *writer, bool value);
MC_API bool mc_building_config_write_bytes(McBuildingConfigWriter *writer, const uint8_t *data, size_t size);
MC_API McStatus mc_building_state_copy_config_from_buffer(McBuildingState *state, const McBuffer *buffer);
MC_API bool mc_building_config_view_at_end(const McBuildingConfigView *view);

#endif
