#ifndef MINDUSTRY_NATIVE_CONTENT_REGISTRY_H
#define MINDUSTRY_NATIVE_CONTENT_REGISTRY_H

#include "mindustry_format.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/*
 * Built-in content is a serialized identity table, not a collection of
 * renderer objects. IDs are assigned independently per ContentType, in the
 * same registration order as the Java content loaders. The old compact
 * McItemId/McBlockId values remain available for the simulation compatibility
 * API; legacy_id records their mapping into this complete registry.
 * Every generated entry is marked CANONICAL for identity/order. The current
 * native metadata model is deliberately marked METADATA_PARTIAL until each
 * Java block/unit/bullet behavior has a differential fixture. Callers must
 * not treat heuristic health/size fields as 1:1 gameplay parity.
 */
typedef enum McContentRegistryType{
    MC_REGISTRY_ITEM = 0,
    MC_REGISTRY_BLOCK,
    MC_REGISTRY_LIQUID,
    MC_REGISTRY_STATUS,
    MC_REGISTRY_UNIT,
    MC_REGISTRY_BULLET,
    MC_REGISTRY_WEATHER,
    MC_REGISTRY_PLANET,
    MC_REGISTRY_SECTOR,
    MC_REGISTRY_TEAM,
    MC_REGISTRY_TYPE_COUNT
} McContentRegistryType;

typedef enum McContentRegistryFlag{
    MC_REGISTRY_FLAG_NONE       = 0u,
    MC_REGISTRY_FLAG_LEGACY     = 1u << 0,
    MC_REGISTRY_FLAG_HIDDEN     = 1u << 1,
    MC_REGISTRY_FLAG_ENVIRONMENT= 1u << 2,
    MC_REGISTRY_FLAG_BUILDABLE  = 1u << 3,
    MC_REGISTRY_FLAG_LIQUID     = 1u << 4,
    MC_REGISTRY_FLAG_GAS        = 1u << 5,
    MC_REGISTRY_FLAG_CAMPAIGN   = 1u << 6,
    MC_REGISTRY_FLAG_PRODUCTION = 1u << 7,
    MC_REGISTRY_FLAG_DEFENSE    = 1u << 8,
    MC_REGISTRY_FLAG_DISTRIBUTION= 1u << 9,
    MC_REGISTRY_FLAG_POWER      = 1u << 10,
    MC_REGISTRY_FLAG_UNIT       = 1u << 11,
    MC_REGISTRY_FLAG_LOGIC      = 1u << 12,
    MC_REGISTRY_FLAG_PAYLOAD    = 1u << 13,
    MC_REGISTRY_FLAG_STORAGE    = 1u << 14,
    MC_REGISTRY_FLAG_TURRET     = 1u << 15,
    MC_REGISTRY_FLAG_EDITOR     = 1u << 16,
    /* Identity data is copied from Java; simulation fields are not complete. */
    MC_REGISTRY_FLAG_CANONICAL  = 1u << 17,
    MC_REGISTRY_FLAG_METADATA_PARTIAL = 1u << 18
} McContentRegistryFlag;

typedef struct McContentEntry{
    McContentRegistryType type;
    uint16_t id;
    int16_t legacy_id;
    const char *name;
    const char *java_field;
    const char *java_class;
    uint32_t flags;
    uint16_t size;
    uint32_t health;
    uint16_t hardness;
    float cost;
    float heat_capacity;
    float flammability;
    float radioactivity;
    bool always_unlocked;
} McContentEntry;

typedef struct McContentRegistryView{
    const McContentEntry *entries;
    size_t count;
    uint32_t version;
    bool valid;
} McContentRegistryView;

typedef struct McContentRegistryValidation{
    size_t entries;
    size_t types;
    size_t duplicate_ids;
    size_t duplicate_names;
    size_t missing_ids;
    size_t legacy_mappings;
    bool ordered;
    bool valid;
} McContentRegistryValidation;

/* Version 15 includes legacy_id, Java identity fields and identity flags. */
#define MC_CONTENT_REGISTRY_VERSION 15u
#define MC_CONTENT_REGISTRY_ITEM_COUNT 22u
#define MC_CONTENT_REGISTRY_BLOCK_COUNT 427u
#define MC_CONTENT_REGISTRY_LIQUID_COUNT 11u
#define MC_CONTENT_REGISTRY_STATUS_COUNT 23u
#define MC_CONTENT_REGISTRY_UNIT_COUNT 63u
#define MC_CONTENT_REGISTRY_BULLET_COUNT 6u
#define MC_CONTENT_REGISTRY_WEATHER_COUNT 6u
#define MC_CONTENT_REGISTRY_PLANET_COUNT 7u
#define MC_CONTENT_REGISTRY_SECTOR_COUNT 46u
#define MC_CONTENT_REGISTRY_TEAM_COUNT 7u
#define MC_CONTENT_REGISTRY_TOTAL_COUNT (MC_CONTENT_REGISTRY_ITEM_COUNT + MC_CONTENT_REGISTRY_BLOCK_COUNT + MC_CONTENT_REGISTRY_LIQUID_COUNT + MC_CONTENT_REGISTRY_STATUS_COUNT + MC_CONTENT_REGISTRY_UNIT_COUNT + MC_CONTENT_REGISTRY_BULLET_COUNT + MC_CONTENT_REGISTRY_WEATHER_COUNT + MC_CONTENT_REGISTRY_PLANET_COUNT + MC_CONTENT_REGISTRY_SECTOR_COUNT + MC_CONTENT_REGISTRY_TEAM_COUNT)

MC_API McContentRegistryView mc_content_registry_view(void);
MC_API const McContentEntry *mc_content_registry_entries(size_t *count);
MC_API size_t mc_content_registry_count(McContentRegistryType type);
MC_API const McContentEntry *mc_content_registry_find(McContentRegistryType type, const char *name);
MC_API const McContentEntry *mc_content_registry_find_id(McContentRegistryType type, uint16_t id);
MC_API const McContentEntry *mc_content_registry_at(McContentRegistryType type, size_t ordinal);
MC_API const char *mc_content_registry_type_name(McContentRegistryType type);
MC_API const char *mc_content_registry_name(McContentRegistryType type, uint16_t id);
MC_API int32_t mc_content_registry_legacy_id(McContentRegistryType type, const char *name);
MC_API const McContentEntry *mc_content_registry_find_legacy_id(McContentRegistryType type, int32_t legacy_id);
MC_API bool mc_content_registry_has_flag(const McContentEntry *entry, McContentRegistryFlag flag);
MC_API McStatus mc_content_registry_validate(McContentRegistryValidation *validation);
MC_API McStatus mc_content_registry_validate_type(McContentRegistryType type,
                                                   McContentRegistryValidation *validation);
MC_API uint64_t mc_content_registry_hash(void);
/*
 * Manifest wire format: MCRG, version, count, then for each ordered entry
 * type/id/legacy_id/name/java_field/java_class/flags. Strings are u16-length UTF-8 and
 * integers are big-endian. It is intentionally independent from MSAV's raw
 * content header so a loader can reject an identity mismatch before parsing.
 */
MC_API McStatus mc_content_registry_write_manifest(McBuffer *output);
MC_API McStatus mc_content_registry_read_manifest(const uint8_t *data, size_t size,
                                                  McContentRegistryValidation *validation);

#endif
