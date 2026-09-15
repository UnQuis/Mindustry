#ifndef MINDUSTRY_NATIVE_BUILDING_SNAPSHOT_H
#define MINDUSTRY_NATIVE_BUILDING_SNAPSHOT_H

#include "mindustry_building.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/* Snapshot objects provide transactional Building mutations without exposing
   pointers into McBuildingStore. They are also useful for deterministic replay
   checkpoints and differential tests. */
typedef struct McBuildingSnapshot{
    McBuildingState state;
    uint64_t hash;
    bool initialized;
} McBuildingSnapshot;

typedef enum McBuildingDeltaField{
    MC_BUILDING_DELTA_NONE       = 0u,
    MC_BUILDING_DELTA_GEOMETRY   = 1u << 0,
    MC_BUILDING_DELTA_HEALTH     = 1u << 1,
    MC_BUILDING_DELTA_RUNTIME    = 1u << 2,
    MC_BUILDING_DELTA_INVENTORY  = 1u << 3,
    MC_BUILDING_DELTA_LIQUIDS    = 1u << 4,
    MC_BUILDING_DELTA_POWER      = 1u << 5,
    MC_BUILDING_DELTA_PRODUCTION = 1u << 6,
    MC_BUILDING_DELTA_DRILL      = 1u << 7,
    MC_BUILDING_DELTA_TURRET     = 1u << 8,
    MC_BUILDING_DELTA_CONVEYOR   = 1u << 9,
    MC_BUILDING_DELTA_FACTORY    = 1u << 10,
    MC_BUILDING_DELTA_CORE       = 1u << 11,
    MC_BUILDING_DELTA_PROCESSOR  = 1u << 12,
    MC_BUILDING_DELTA_CONFIG     = 1u << 13,
    MC_BUILDING_DELTA_ALL        = 0x3fffu
} McBuildingDeltaField;

typedef struct McBuildingDelta{
    uint32_t fields;
    McEntityId entity_id;
    uint64_t from_tick;
    uint64_t to_tick;
    McBuildingState state;
    bool initialized;
} McBuildingDelta;

typedef struct McBuildingSnapshotSet{
    McBuildingSnapshot *snapshots;
    size_t count;
    size_t capacity;
    uint64_t tick;
    uint64_t hash;
} McBuildingSnapshotSet;

MC_API void mc_building_snapshot_init(McBuildingSnapshot *snapshot);
MC_API void mc_building_snapshot_destroy(McBuildingSnapshot *snapshot);
MC_API McStatus mc_building_snapshot_capture(McBuildingSnapshot *snapshot, const McBuildingState *state);
MC_API McStatus mc_building_snapshot_copy(McBuildingSnapshot *destination, const McBuildingSnapshot *source);
MC_API bool mc_building_snapshot_equal(const McBuildingSnapshot *left, const McBuildingSnapshot *right);
MC_API uint64_t mc_building_snapshot_hash(const McBuildingSnapshot *snapshot);
MC_API const McBuildingState *mc_building_snapshot_state(const McBuildingSnapshot *snapshot);

MC_API void mc_building_delta_init(McBuildingDelta *delta);
MC_API void mc_building_delta_destroy(McBuildingDelta *delta);
MC_API McStatus mc_building_delta_compute(McBuildingDelta *delta, const McBuildingSnapshot *before,
                                          const McBuildingSnapshot *after);
MC_API McStatus mc_building_delta_apply(McBuildingState *state, const McBuildingDelta *delta);
MC_API bool mc_building_delta_has(const McBuildingDelta *delta, McBuildingDeltaField field);
MC_API size_t mc_building_delta_field_count(const McBuildingDelta *delta);

MC_API void mc_building_snapshot_set_init(McBuildingSnapshotSet *set);
MC_API void mc_building_snapshot_set_destroy(McBuildingSnapshotSet *set);
MC_API McStatus mc_building_snapshot_set_capture(McBuildingSnapshotSet *set, const McBuildingStore *store);
MC_API McStatus mc_building_snapshot_set_restore(const McBuildingSnapshotSet *set, McBuildingStore *store);
MC_API const McBuildingSnapshot *mc_building_snapshot_set_find(const McBuildingSnapshotSet *set,
                                                                McEntityId entity_id);
MC_API uint64_t mc_building_snapshot_set_hash(const McBuildingSnapshotSet *set);
MC_API bool mc_building_snapshot_set_equal(const McBuildingSnapshotSet *left,
                                           const McBuildingSnapshotSet *right);
MC_API McStatus mc_building_snapshot_set_write(const McBuildingSnapshotSet *set, McBuffer *output);
MC_API McStatus mc_building_snapshot_set_read(const uint8_t *data, size_t size,
                                              McBuildingSnapshotSet *set);

#endif
