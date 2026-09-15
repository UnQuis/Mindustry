#ifndef MINDUSTRY_NATIVE_BUILDING_INSPECTION_H
#define MINDUSTRY_NATIVE_BUILDING_INSPECTION_H

#include "mindustry_building_rules.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef enum McBuildingWarning{
    MC_BUILDING_WARNING_NONE             = 0u,
    MC_BUILDING_WARNING_DEAD             = 1u << 0,
    MC_BUILDING_WARNING_DISABLED         = 1u << 1,
    MC_BUILDING_WARNING_UNBUILT          = 1u << 2,
    MC_BUILDING_WARNING_LOW_HEALTH       = 1u << 3,
    MC_BUILDING_WARNING_LOW_POWER        = 1u << 4,
    MC_BUILDING_WARNING_FULL_INVENTORY   = 1u << 5,
    MC_BUILDING_WARNING_EMPTY_INPUT      = 1u << 6,
    MC_BUILDING_WARNING_OUTPUT_BLOCKED   = 1u << 7,
    MC_BUILDING_WARNING_NO_TARGET        = 1u << 8,
    MC_BUILDING_WARNING_NO_NETWORK       = 1u << 9,
    MC_BUILDING_WARNING_HEATED           = 1u << 10,
    MC_BUILDING_WARNING_INVALID          = 1u << 11
} McBuildingWarning;

typedef struct McBuildingInspection{
    McEntityId entity_id;
    McBuildingKind kind;
    McTeam team;
    uint32_t warnings;
    uint32_t capabilities;
    size_t network_count;
    uint32_t power_network;
    uint32_t liquid_network;
    uint32_t item_count;
    uint32_t item_space;
    float liquid_volume;
    float liquid_space;
    float power_stored;
    float power_capacity;
    float health_ratio;
    float efficiency;
    float priority;
    bool ready;
    bool stalled;
    bool valid;
} McBuildingInspection;

typedef struct McBuildingStoreInspection{
    McBuildingStoreMetrics metrics;
    size_t warning_count;
    size_t invalid_count;
    size_t network_count;
    uint32_t warnings;
    float health_score;
    float throughput_score;
    float power_score;
    float storage_score;
    bool valid;
} McBuildingStoreInspection;

typedef struct McBuildingPath{
    McEntityId *ids;
    size_t count;
    size_t capacity;
    McBuildingLinkKind kind;
    bool found;
} McBuildingPath;

typedef struct McBuildingStoreDifference{
    size_t added;
    size_t removed;
    size_t changed;
    size_t unchanged;
    size_t link_changes;
    uint64_t item_delta;
    float liquid_delta;
    float power_delta;
    bool same_tick;
} McBuildingStoreDifference;

MC_API const char *mc_building_warning_name(McBuildingWarning warning);
MC_API void mc_building_inspection_reset(McBuildingInspection *inspection);
MC_API McStatus mc_building_state_inspect(const McBuildingState *state,
                                          McBuildingInspection *inspection);
MC_API uint32_t mc_building_state_warnings(const McBuildingState *state);
MC_API float mc_building_state_priority(const McBuildingState *state);
MC_API size_t mc_building_state_warning_count(uint32_t warnings);
MC_API bool mc_building_state_has_warning(uint32_t warnings, McBuildingWarning warning);

MC_API void mc_building_store_inspection_reset(McBuildingStoreInspection *inspection);
MC_API McStatus mc_building_store_inspect(const McBuildingStore *store,
                                          McBuildingStoreInspection *inspection);
MC_API McStatus mc_building_store_compare(const McBuildingStore *left, const McBuildingStore *right,
                                          McBuildingStoreDifference *difference);
MC_API float mc_building_store_capacity_score(const McBuildingStore *store);
MC_API float mc_building_store_throughput_score(const McBuildingStore *store);
MC_API float mc_building_store_power_score(const McBuildingStore *store);
MC_API float mc_building_store_storage_score(const McBuildingStore *store);

MC_API void mc_building_path_init(McBuildingPath *path, McBuildingLinkKind kind);
MC_API void mc_building_path_destroy(McBuildingPath *path);
MC_API McStatus mc_building_store_find_path(const McBuildingStore *store, McEntityId from,
                                            McEntityId to, McBuildingLinkKind kind,
                                            McBuildingPath *path);
MC_API size_t mc_building_store_reachable(const McBuildingStore *store, McEntityId from,
                                          McBuildingLinkKind kind, McEntityId *ids, size_t capacity);
MC_API bool mc_building_store_connected(const McBuildingStore *store, McEntityId from,
                                        McEntityId to, McBuildingLinkKind kind);
MC_API size_t mc_building_path_length(const McBuildingPath *path);
MC_API McEntityId mc_building_path_at(const McBuildingPath *path, size_t index);
MC_API McStatus mc_building_path_write(const McBuildingPath *path, McBuffer *output);
MC_API McStatus mc_building_path_read(const uint8_t *data, size_t size, McBuildingPath *path);

MC_API int mc_building_kind_priority(McBuildingKind kind);
MC_API int mc_building_warning_priority(uint32_t warnings);
MC_API McBuildingState *mc_building_store_find_highest_priority(McBuildingStore *store,
                                                                 McBuildingWarning required_warning);

#endif
