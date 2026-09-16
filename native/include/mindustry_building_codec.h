#ifndef MINDUSTRY_NATIVE_BUILDING_CODEC_H
#define MINDUSTRY_NATIVE_BUILDING_CODEC_H

#include "mindustry_building.h"
#include "mindustry_content_registry.h"
#include "mindustry_save.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

/*
 * Java's SaveVersion writes a tile Building as:
 *
 *   entity revision byte, BuildingComp.writeBase(), Building.write()
 *
 * This codec models that stream directly. It is deliberately separate from
 * McBuildingStore's native custom chunk: a Java .msav payload can be decoded,
 * inspected, and written back without passing through a guessed native format.
 */
#define MC_JAVA_BUILDING_MAX_POWER_LINKS 4096u
#define MC_JAVA_BUILDING_MAX_BUFFER_SLOTS 255u
#define MC_JAVA_BUILDING_MAX_INCOMING 255u
#define MC_JAVA_BUILDING_MAX_AMMO 255u
#define MC_JAVA_BUILDING_MAX_PAYLOADS 255u
#define MC_JAVA_BUILDING_MAX_PLANS 255u

typedef enum McJavaBuildingFamily{
    MC_JAVA_BUILDING_NONE = 0,
    MC_JAVA_BUILDING_ITEM_TRANSPORT,
    MC_JAVA_BUILDING_CRAFTER,
    MC_JAVA_BUILDING_DRILL,
    MC_JAVA_BUILDING_FACTORY,
    MC_JAVA_BUILDING_TURRET,
    MC_JAVA_BUILDING_COMBAT_SUPPORT,
    MC_JAVA_BUILDING_OTHER
} McJavaBuildingFamily;

typedef enum McJavaBuildingVariant{
    MC_JAVA_BUILDING_VARIANT_NONE = 0,
    MC_JAVA_BUILDING_VARIANT_CONVEYOR,
    MC_JAVA_BUILDING_VARIANT_ITEM_BUFFER,
    MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_BUFFER,
    MC_JAVA_BUILDING_VARIANT_ITEM_BRIDGE,
    MC_JAVA_BUILDING_VARIANT_BUFFERED_ITEM_BRIDGE,
    MC_JAVA_BUILDING_VARIANT_SORTER,
    MC_JAVA_BUILDING_VARIANT_DUCT,
    MC_JAVA_BUILDING_VARIANT_DUCT_JUNCTION,
    MC_JAVA_BUILDING_VARIANT_DUCT_ROUTER,
    MC_JAVA_BUILDING_VARIANT_MASS_DRIVER,
    MC_JAVA_BUILDING_VARIANT_STACK_CONVEYOR,
    MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_UNLOADER,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_CONVEYOR,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_MASS_DRIVER,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_LOADER,
    MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_LOADER,
    MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_UNLOAD_POINT,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_SOURCE,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_VOID,
    MC_JAVA_BUILDING_VARIANT_UNIT_ASSEMBLER_MODULE,
    MC_JAVA_BUILDING_VARIANT_CRAFTER,
    MC_JAVA_BUILDING_VARIANT_SEPARATOR,
    MC_JAVA_BUILDING_VARIANT_DRILL,
    MC_JAVA_BUILDING_VARIANT_BEAM_DRILL,
    MC_JAVA_BUILDING_VARIANT_FACTORY,
    MC_JAVA_BUILDING_VARIANT_RECONSTRUCTOR,
    MC_JAVA_BUILDING_VARIANT_ASSEMBLER,
    MC_JAVA_BUILDING_VARIANT_CONSTRUCTOR,
    MC_JAVA_BUILDING_VARIANT_BLOCK_PRODUCER,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_DECONSTRUCTOR,
    MC_JAVA_BUILDING_VARIANT_TURRET,
    MC_JAVA_BUILDING_VARIANT_ITEM_TURRET,
    MC_JAVA_BUILDING_VARIANT_CONTINUOUS_TURRET,
    MC_JAVA_BUILDING_VARIANT_ROTATING_TURRET,
    MC_JAVA_BUILDING_VARIANT_BUILD_TURRET,
    MC_JAVA_BUILDING_VARIANT_PAYLOAD_TURRET
} McJavaBuildingVariant;

typedef struct McJavaConveyorItem{
    int16_t item_id;
    int8_t x;
    int8_t y;
} McJavaConveyorItem;

typedef struct McJavaItemBuffer{
    int16_t index;
    int16_t encoded_capacity;
    uint64_t values[MC_JAVA_BUILDING_MAX_BUFFER_SLOTS];
} McJavaItemBuffer;

typedef struct McJavaDirectionalItemBuffer{
    McJavaItemBuffer sides[4];
} McJavaDirectionalItemBuffer;

typedef struct McJavaPlan{
    bool breaking;
    int32_t packed_position;
    uint16_t block_id;
    uint8_t rotation;
    uint8_t *config;
    size_t config_size;
} McJavaPlan;

typedef struct McJavaPayloadEntry{
    uint8_t content_type;
    int16_t content_id;
    int32_t amount;
} McJavaPayloadEntry;

typedef struct McJavaPayloadPrefix{
    bool present;
    uint8_t type;
    int16_t block_id;
    uint8_t revision;
    float x;
    float y;
    float rotation;
} McJavaPayloadPrefix;

typedef struct McJavaBuildingExtension{
    McJavaBuildingVariant variant;
    bool raw_exact;
    uint8_t *raw;
    size_t raw_size;

    union{
        struct{
            uint32_t count;
            McJavaConveyorItem items[MC_JAVA_BUILDING_MAX_BUFFER_SLOTS];
            McJavaPayloadPrefix payload;
            float progress;
            float item_rotation;
            int16_t sort_item;
            uint8_t sort_type;
            uint8_t rec_dir;
        } conveyor;
        struct{
            McJavaItemBuffer buffer;
        } item_buffer;
        struct{
            McJavaDirectionalItemBuffer buffer;
        } directional_buffer;
        struct{
            int32_t link;
            float warmup;
            uint32_t incoming_count;
            int32_t incoming[MC_JAVA_BUILDING_MAX_INCOMING];
            bool moved;
        } item_bridge;
        struct{
            int32_t link;
            float warmup;
            uint32_t incoming_count;
            int32_t incoming[MC_JAVA_BUILDING_MAX_INCOMING];
            bool moved;
            McJavaItemBuffer buffer;
        } buffered_item_bridge;
        struct{
            int16_t sort_item;
            int16_t offset;
            uint8_t rec_dir;
        } item_filter;
        struct{
            float times[4];
            int16_t items[4];
        } duct_junction;
        struct{
            McJavaPayloadPrefix payload;
            int32_t link;
            float rotation;
            uint8_t state;
            float reload;
            float charge;
            bool loaded;
            bool charging;
        } mass_driver;
        struct{
            int32_t link;
            float cooldown;
        } stack_conveyor;
        struct{
            float progress;
            float warmup;
            int32_t seed;
        } crafter;
        struct{
            float time;
            float warmup;
        } beam_drill;
        struct{
            float progress;
            float warmup;
        } drill;
        struct{
            McJavaPayloadPrefix payload;
            float progress;
            int16_t plan;
            bool command_position;
            float command_x;
            float command_y;
            uint8_t command;
        } factory;
        struct{
            McJavaPayloadPrefix payload;
            float progress;
            uint8_t unit_count;
            int32_t units[MC_JAVA_BUILDING_MAX_PLANS];
            uint16_t payload_count;
            McJavaPayloadEntry payloads[MC_JAVA_BUILDING_MAX_PAYLOADS];
            bool command_position;
            float command_x;
            float command_y;
        } assembler;
        struct{
            McJavaPayloadPrefix payload;
            int16_t recipe;
        } constructor;
        struct{
            McJavaPayloadPrefix payload;
            float progress;
            uint16_t accumulator_count;
            float accumulators[MC_JAVA_BUILDING_MAX_PAYLOADS];
            bool payload_present;
        } payload_deconstructor;
        struct{
            float reload;
            float rotation;
            float last_length;
            uint8_t ammo_count;
            struct{
                int16_t item_id;
                int16_t amount;
            } ammo[MC_JAVA_BUILDING_MAX_AMMO];
        } turret;
        struct{
            float rotation;
        } rotating_turret;
        struct{
            float rotation;
            uint16_t plan_count;
            McJavaPlan plans[MC_JAVA_BUILDING_MAX_PLANS];
        } build_turret;
        struct{
            McJavaPayloadPrefix payload;
            bool exporting;
        } payload_loader;
        struct{
            int32_t unit_id;
        } unit_cargo_loader;
        struct{
            int16_t item_id;
            bool stale;
        } unit_cargo_unload_point;
        struct{
            McJavaPayloadPrefix payload;
            int16_t unit_id;
            int16_t block_id;
            bool command_position;
            float command_x;
            float command_y;
        } payload_source;
        struct{
            McJavaPayloadPrefix payload;
            float progress;
        } payload_producer;
        struct{
            McJavaPayloadPrefix payload;
        } payload_only;
        struct{
            float reload;
            float rotation;
            uint16_t payload_count;
            McJavaPayloadEntry payloads[MC_JAVA_BUILDING_MAX_PAYLOADS];
        } payload_turret;
    } data;
} McJavaBuildingExtension;

typedef struct McJavaBuildingRecord{
    uint32_t init_magic;
    uint16_t block_id;
    uint8_t revision;
    uint8_t base_version;
    uint8_t rotation;
    uint8_t team;
    uint8_t module_bits;
    bool legacy_base;
    bool enabled;
    float health;
    float time_scale;
    float time_scale_duration;
    int32_t last_disabler;
    uint8_t efficiency;
    uint8_t optional_efficiency;
    uint64_t visible_flags;

    int32_t items[MC_CONTENT_REGISTRY_ITEM_COUNT];
    int32_t power_links[MC_JAVA_BUILDING_MAX_POWER_LINKS];
    uint16_t power_link_count;
    float power_status;
    float liquids[MC_CONTENT_REGISTRY_LIQUID_COUNT];

    McJavaBuildingFamily family;
    McJavaBuildingExtension extension;
} McJavaBuildingRecord;

MC_API void mc_java_building_record_init(McJavaBuildingRecord *record, uint16_t block_id,
                                         uint8_t revision);
MC_API void mc_java_building_record_destroy(McJavaBuildingRecord *record);
MC_API McJavaBuildingFamily mc_java_building_family_for_block(uint16_t block_id);
MC_API McJavaBuildingVariant mc_java_building_variant_for_block(uint16_t block_id);
MC_API const char *mc_java_building_family_name(McJavaBuildingFamily family);
MC_API const char *mc_java_building_variant_name(McJavaBuildingVariant variant);

/* Decode/encode the payload after the entity revision byte. */
MC_API McStatus mc_java_building_read(uint16_t block_id, uint8_t revision,
                                      const uint8_t *data, size_t size,
                                      McJavaBuildingRecord *record);
MC_API McStatus mc_java_building_read_remap(uint16_t block_id, uint8_t revision,
                                            const uint8_t *data, size_t size,
                                            const McContentRemap *remap,
                                            McJavaBuildingRecord *record);
MC_API McStatus mc_java_building_write(const McJavaBuildingRecord *record, McBuffer *output);

/* Exact SaveVersion tile entity wrappers, including the revision byte. */
MC_API McStatus mc_java_building_read_chunk(uint16_t block_id, const uint8_t *data, size_t size,
                                            McJavaBuildingRecord *record);
MC_API McStatus mc_java_building_read_chunk_remap(uint16_t block_id, const uint8_t *data, size_t size,
                                                  const McContentRemap *remap,
                                                  McJavaBuildingRecord *record);
MC_API McStatus mc_java_building_write_chunk(const McJavaBuildingRecord *record, McBuffer *output);

/* Conversion for the semantic native store. It intentionally returns the
   compact legacy block ID only when a corresponding compatibility ID exists. */
MC_API McStatus mc_java_building_to_native(const McJavaBuildingRecord *record,
                                           McBuildingState *state);
MC_API McStatus mc_java_building_from_native(const McBuildingState *state,
                                             uint16_t block_id, uint8_t revision,
                                             McJavaBuildingRecord *record);

#endif
