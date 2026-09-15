#ifndef MINDUSTRY_NATIVE_ENTITIES_H
#define MINDUSTRY_NATIVE_ENTITIES_H

#include "mindustry_typeio.h"
#include "mindustry_save.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

typedef struct McEntityMapping{
    uint16_t id;
    char *name;
} McEntityMapping;

typedef struct McTeamPlan{
    int32_t team;
    int16_t x;
    int16_t y;
    int16_t rotation;
    uint16_t block;
    uint8_t *config;
    size_t config_size;
} McTeamPlan;

typedef struct McEntityRecord{
    /* The generated entity header inside SaveVersion.writeChunk. */
    uint8_t class_id;
    uint32_t id;
    /* Exact payload, including class_id and id, excluding its uint32 length. */
    uint8_t *data;
    size_t size;
} McEntityRecord;

typedef struct McEntitiesRegion{
    McEntityMapping *mapping;
    size_t mapping_count;
    McTeamPlan *plans;
    size_t plan_count;
    McEntityRecord *records;
    size_t record_count;
} McEntitiesRegion;

MC_API McStatus mc_entities_read(const uint8_t *data, size_t size, McEntitiesRegion *entities);
MC_API McStatus mc_entities_write(const McEntitiesRegion *entities, McBuffer *output);
MC_API void mc_entities_destroy(McEntitiesRegion *entities);

#endif
