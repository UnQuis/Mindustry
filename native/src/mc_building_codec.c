#include "mindustry_building_codec.h"

#include "mindustry_typeio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BASE_ITEMS 1u
#define BASE_POWER (1u << 1)
#define BASE_LIQUIDS (1u << 2)
#define BASE_CONSUME (1u << 3)
#define BASE_TIMESCALE (1u << 4)
#define BASE_DISABLER (1u << 5)
#define MC_JAVA_BUILDING_RECORD_MAGIC UINT32_C(0x4d425243)

static McStatus write_u64_be(McBuffer *out, uint64_t value){
    McStatus status = MC_OK;
    for(int i = 7; i >= 0 && status == MC_OK; i--) status = mc_buffer_write_u8(out, (uint8_t)(value >> (i * 8)));
    return status;
}

static McStatus read_u64_be(McBuffer *in, uint64_t *value){
    if(in == NULL || value == NULL || in->position > in->size || in->size - in->position < 8) return MC_FORMAT_ERROR;
    uint64_t result = 0;
    for(size_t i = 0; i < 8; i++) result = (result << 8) | in->data[in->position++];
    *value = result;
    return MC_OK;
}

static McStatus write_f32(McBuffer *out, float value){
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return mc_buffer_write_u32_be(out, bits);
}

static McStatus read_f32(McBuffer *in, float *value){
    uint32_t bits = 0;
    McStatus status = mc_buffer_read_u32_be(in, &bits);
    if(status != MC_OK) return status;
    memcpy(value, &bits, sizeof(bits));
    return MC_OK;
}

static McStatus write_i16(McBuffer *out, int16_t value){
    return mc_buffer_write_u16_be(out, (uint16_t)value);
}

static McStatus read_i16(McBuffer *in, int16_t *value){
    uint16_t encoded = 0;
    McStatus status = mc_buffer_read_u16_be(in, &encoded);
    if(status == MC_OK) *value = (int16_t)encoded;
    return status;
}

static McStatus write_i32(McBuffer *out, int32_t value){
    return mc_buffer_write_u32_be(out, (uint32_t)value);
}

static McStatus read_i32(McBuffer *in, int32_t *value){
    uint32_t encoded = 0;
    McStatus status = mc_buffer_read_u32_be(in, &encoded);
    if(status == MC_OK) *value = (int32_t)encoded;
    return status;
}

static McStatus write_bool8(McBuffer *out, bool value){
    return mc_buffer_write_u8(out, value ? 1u : 0u);
}

static McStatus read_bool8(McBuffer *in, bool *value){
    uint8_t encoded = 0;
    McStatus status = mc_buffer_read_u8(in, &encoded);
    if(status != MC_OK) return status;
    if(encoded > 1) return MC_FORMAT_ERROR;
    *value = encoded != 0;
    return MC_OK;
}

static McStatus read_count_u8(McBuffer *in, size_t maximum, size_t *count){
    uint8_t encoded = 0;
    if(mc_buffer_read_u8(in, &encoded) != MC_OK || (size_t)encoded > maximum) return MC_FORMAT_ERROR;
    *count = encoded;
    return MC_OK;
}

static McStatus read_count_signed_byte(McBuffer *in, size_t maximum, size_t *count){
    uint8_t encoded = 0;
    if(mc_buffer_read_u8(in, &encoded) != MC_OK) return MC_FORMAT_ERROR;
    int8_t signed_value = (int8_t)encoded;
    if(signed_value < 0){
        *count = 0;
        return MC_OK;
    }
    if((size_t)signed_value > maximum) return MC_FORMAT_ERROR;
    *count = (size_t)signed_value;
    return MC_OK;
}

static McStatus read_count_i32(McBuffer *in, size_t maximum, size_t *count){
    int32_t encoded = 0;
    if(read_i32(in, &encoded) != MC_OK || encoded < 0 || (uint32_t)encoded > maximum) return MC_FORMAT_ERROR;
    *count = (size_t)encoded;
    return MC_OK;
}

static McStatus read_count_i16(McBuffer *in, size_t maximum, bool *negative, size_t *count){
    int16_t encoded = 0;
    if(read_i16(in, &encoded) != MC_OK) return MC_FORMAT_ERROR;
    *negative = encoded < 0;
    size_t result = encoded < 0 ? (size_t)-(int32_t)encoded : (size_t)encoded;
    if(result > maximum) return MC_FORMAT_ERROR;
    *count = result;
    return MC_OK;
}

static McStatus write_count_i16(McBuffer *out, size_t count){
    if(count > INT16_MAX) return MC_CAPACITY_EXCEEDED;
    return write_i16(out, (int16_t)count);
}

static const McContentEntry *block_entry(uint16_t block_id){
    return mc_content_registry_find_id(MC_REGISTRY_BLOCK, block_id);
}

static bool class_is(const McContentEntry *entry, const char *name){
    return entry != NULL && strcmp(entry->java_class, name) == 0;
}

static bool class_in(const McContentEntry *entry, const char *const *names, size_t count){
    if(entry == NULL) return false;
    for(size_t i = 0; i < count; i++) if(strcmp(entry->java_class, names[i]) == 0) return true;
    return false;
}

McJavaBuildingVariant mc_java_building_variant_for_block(uint16_t block_id){
    const McContentEntry *entry = block_entry(block_id);
    if(entry == NULL) return MC_JAVA_BUILDING_VARIANT_NONE;

    if(class_is(entry, "Conveyor") || class_is(entry, "ArmoredConveyor")) return MC_JAVA_BUILDING_VARIANT_CONVEYOR;
    if(class_is(entry, "BufferedItemBridge")) return MC_JAVA_BUILDING_VARIANT_BUFFERED_ITEM_BRIDGE;
    if(class_is(entry, "Junction")) return MC_JAVA_BUILDING_VARIANT_ITEM_BUFFER;
    if(class_is(entry, "ItemBridge") || class_is(entry, "LiquidBridge")) return MC_JAVA_BUILDING_VARIANT_ITEM_BRIDGE;
    if(class_is(entry, "Sorter") || class_is(entry, "Unloader")) return MC_JAVA_BUILDING_VARIANT_SORTER;
    if(class_is(entry, "DirectionalUnloader")) return MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_UNLOADER;
    if(class_is(entry, "Duct")) return MC_JAVA_BUILDING_VARIANT_DUCT;
    if(class_is(entry, "DuctJunction")) return MC_JAVA_BUILDING_VARIANT_DUCT_JUNCTION;
    if(class_is(entry, "DuctRouter")) return MC_JAVA_BUILDING_VARIANT_DUCT_ROUTER;
    if(class_is(entry, "MassDriver")) return MC_JAVA_BUILDING_VARIANT_MASS_DRIVER;
    if(class_is(entry, "StackConveyor")) return MC_JAVA_BUILDING_VARIANT_STACK_CONVEYOR;

    if(class_is(entry, "Separator")) return MC_JAVA_BUILDING_VARIANT_SEPARATOR;
    if(class_is(entry, "BeamDrill")) return MC_JAVA_BUILDING_VARIANT_BEAM_DRILL;
    if(class_is(entry, "Drill") || class_is(entry, "BurstDrill")) return MC_JAVA_BUILDING_VARIANT_DRILL;
    {
        static const char *const crafter_classes[] = {
            "GenericCrafter", "AttributeCrafter", "HeatCrafter", "HeatProducer"
        };
        if(class_in(entry, crafter_classes, sizeof(crafter_classes) / sizeof(crafter_classes[0])))
            return MC_JAVA_BUILDING_VARIANT_CRAFTER;
    }

    if(class_is(entry, "UnitFactory")) return MC_JAVA_BUILDING_VARIANT_FACTORY;
    if(class_is(entry, "Reconstructor")) return MC_JAVA_BUILDING_VARIANT_RECONSTRUCTOR;
    if(class_is(entry, "UnitAssembler")) return MC_JAVA_BUILDING_VARIANT_ASSEMBLER;
    if(class_is(entry, "Constructor")) return MC_JAVA_BUILDING_VARIANT_CONSTRUCTOR;
    if(class_is(entry, "BlockProducer")) return MC_JAVA_BUILDING_VARIANT_BLOCK_PRODUCER;
    if(class_is(entry, "PayloadDeconstructor")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_DECONSTRUCTOR;
    if(class_is(entry, "PayloadConveyor")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_CONVEYOR;
    if(class_is(entry, "PayloadRouter")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER;
    if(class_is(entry, "PayloadMassDriver")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_MASS_DRIVER;
    if(class_is(entry, "PayloadLoader") || class_is(entry, "PayloadUnloader")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_LOADER;
    if(class_is(entry, "UnitCargoLoader")) return MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_LOADER;
    if(class_is(entry, "UnitCargoUnloadPoint")) return MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_UNLOAD_POINT;
    if(class_is(entry, "PayloadSource")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_SOURCE;
    if(class_is(entry, "PayloadVoid")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_VOID;
    if(class_is(entry, "UnitAssemblerModule")) return MC_JAVA_BUILDING_VARIANT_UNIT_ASSEMBLER_MODULE;

    if(class_is(entry, "ItemTurret")) return MC_JAVA_BUILDING_VARIANT_ITEM_TURRET;
    if(class_is(entry, "ContinuousTurret") || class_is(entry, "ContinuousLiquidTurret")) return MC_JAVA_BUILDING_VARIANT_CONTINUOUS_TURRET;
    if(class_is(entry, "Turret") || class_is(entry, "PowerTurret") || class_is(entry, "LiquidTurret") || class_is(entry, "LaserTurret"))
        return MC_JAVA_BUILDING_VARIANT_TURRET;
    if(class_is(entry, "BuildTurret")) return MC_JAVA_BUILDING_VARIANT_BUILD_TURRET;
    if(class_is(entry, "PayloadAmmoTurret")) return MC_JAVA_BUILDING_VARIANT_PAYLOAD_TURRET;
    if(class_is(entry, "RepairTurret") || class_is(entry, "PointDefenseTurret") || class_is(entry, "TractorBeamTurret"))
        return MC_JAVA_BUILDING_VARIANT_ROTATING_TURRET;
    return MC_JAVA_BUILDING_VARIANT_NONE;
}

McJavaBuildingFamily mc_java_building_family_for_block(uint16_t block_id){
    McJavaBuildingVariant variant = mc_java_building_variant_for_block(block_id);
    switch(variant){
        case MC_JAVA_BUILDING_VARIANT_CONVEYOR:
        case MC_JAVA_BUILDING_VARIANT_ITEM_BUFFER:
        case MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_BUFFER:
        case MC_JAVA_BUILDING_VARIANT_ITEM_BRIDGE:
        case MC_JAVA_BUILDING_VARIANT_BUFFERED_ITEM_BRIDGE:
        case MC_JAVA_BUILDING_VARIANT_SORTER:
        case MC_JAVA_BUILDING_VARIANT_DUCT:
        case MC_JAVA_BUILDING_VARIANT_DUCT_JUNCTION:
        case MC_JAVA_BUILDING_VARIANT_DUCT_ROUTER:
        case MC_JAVA_BUILDING_VARIANT_MASS_DRIVER:
        case MC_JAVA_BUILDING_VARIANT_STACK_CONVEYOR:
        case MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_UNLOADER:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_CONVEYOR:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_MASS_DRIVER:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_LOADER:
        case MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_LOADER:
        case MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_UNLOAD_POINT:
            return MC_JAVA_BUILDING_ITEM_TRANSPORT;
        case MC_JAVA_BUILDING_VARIANT_CRAFTER:
        case MC_JAVA_BUILDING_VARIANT_SEPARATOR:
            return MC_JAVA_BUILDING_CRAFTER;
        case MC_JAVA_BUILDING_VARIANT_DRILL:
        case MC_JAVA_BUILDING_VARIANT_BEAM_DRILL:
            return MC_JAVA_BUILDING_DRILL;
        case MC_JAVA_BUILDING_VARIANT_FACTORY:
        case MC_JAVA_BUILDING_VARIANT_RECONSTRUCTOR:
        case MC_JAVA_BUILDING_VARIANT_ASSEMBLER:
        case MC_JAVA_BUILDING_VARIANT_CONSTRUCTOR:
        case MC_JAVA_BUILDING_VARIANT_BLOCK_PRODUCER:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_DECONSTRUCTOR:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_SOURCE:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_VOID:
        case MC_JAVA_BUILDING_VARIANT_UNIT_ASSEMBLER_MODULE:
            return MC_JAVA_BUILDING_FACTORY;
        case MC_JAVA_BUILDING_VARIANT_TURRET:
        case MC_JAVA_BUILDING_VARIANT_ITEM_TURRET:
        case MC_JAVA_BUILDING_VARIANT_CONTINUOUS_TURRET:
        case MC_JAVA_BUILDING_VARIANT_ROTATING_TURRET:
        case MC_JAVA_BUILDING_VARIANT_BUILD_TURRET:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_TURRET:
            return MC_JAVA_BUILDING_TURRET;
        default:
            break;
    }

    /* Several distribution and production blocks intentionally inherit the
       base Building implementation and therefore have no subtype bytes. They
       still own the item/liquid/power modules, which matters for old entity
       revisions whose module mask was implicit. */
    {
        static const char *const item_transport_base[] = {
            "Router", "OverflowGate", "OverflowDuct", "DirectionBridge", "DirectionLiquidBridge", "DuctBridge"
        };
        if(class_in(block_entry(block_id), item_transport_base,
                   sizeof(item_transport_base) / sizeof(item_transport_base[0])))
            return MC_JAVA_BUILDING_ITEM_TRANSPORT;
    }
    {
        static const char *const production_base[] = {"WallCrafter", "Incinerator", "ItemIncinerator"};
        if(class_in(block_entry(block_id), production_base,
                   sizeof(production_base) / sizeof(production_base[0])))
            return MC_JAVA_BUILDING_CRAFTER;
    }
    return MC_JAVA_BUILDING_OTHER;
}

const char *mc_java_building_family_name(McJavaBuildingFamily family){
    static const char *const names[] = {"none", "item-transport", "crafter", "drill", "factory", "turret", "combat-support", "other"};
    return family <= MC_JAVA_BUILDING_OTHER ? names[family] : "unknown";
}

const char *mc_java_building_variant_name(McJavaBuildingVariant variant){
    static const char *const names[] = {
        "none", "conveyor", "item-buffer", "directional-buffer", "item-bridge", "buffered-item-bridge", "sorter",
        "duct", "duct-junction", "duct-router", "mass-driver", "stack-conveyor", "directional-unloader",
        "payload-conveyor", "payload-router", "payload-mass-driver", "payload-loader", "unit-cargo-loader", "unit-cargo-unload-point", "payload-source", "payload-void", "unit-assembler-module",
        "crafter", "separator", "drill", "beam-drill", "factory", "reconstructor", "assembler",
        "constructor", "block-producer", "payload-deconstructor", "turret", "item-turret",
        "continuous-turret", "rotating-turret", "build-turret", "payload-turret"
    };
    return variant <= MC_JAVA_BUILDING_VARIANT_PAYLOAD_TURRET ? names[variant] : "unknown";
}

void mc_java_building_record_init(McJavaBuildingRecord *record, uint16_t block_id, uint8_t revision){
    if(record == NULL) return;
    memset(record, 0, sizeof(*record));
    record->init_magic = MC_JAVA_BUILDING_RECORD_MAGIC;
    record->block_id = block_id;
    record->revision = revision;
    record->base_version = 3;
    record->rotation = 0;
    record->team = MC_TEAM_SHARDED;
    record->module_bits = BASE_CONSUME;
    record->enabled = true;
    record->health = mc_building_default_health(MC_BLOCK_AIR);
    record->time_scale = 1.0f;
    record->efficiency = 255;
    record->optional_efficiency = 255;
    record->family = mc_java_building_family_for_block(block_id);
    record->extension.variant = mc_java_building_variant_for_block(block_id);
}

void mc_java_building_record_destroy(McJavaBuildingRecord *record){
    if(record == NULL) return;
    if(record->init_magic != MC_JAVA_BUILDING_RECORD_MAGIC){
        memset(record, 0, sizeof(*record));
        return;
    }
    free(record->extension.raw);
    if(record->extension.variant == MC_JAVA_BUILDING_VARIANT_BUILD_TURRET){
        for(size_t i = 0; i < MC_JAVA_BUILDING_MAX_PLANS; i++) free(record->extension.data.build_turret.plans[i].config);
    }
    record->extension.raw = NULL;
    record->extension.raw_size = 0;
    record->init_magic = 0;
}

static uint8_t default_module_bits(const McJavaBuildingRecord *record){
    uint8_t bits = BASE_CONSUME;
    switch(record->family){
        case MC_JAVA_BUILDING_ITEM_TRANSPORT: bits |= BASE_ITEMS; break;
        case MC_JAVA_BUILDING_CRAFTER: bits |= BASE_ITEMS | BASE_LIQUIDS | BASE_POWER; break;
        case MC_JAVA_BUILDING_DRILL: bits |= BASE_ITEMS | BASE_LIQUIDS | BASE_POWER; break;
        case MC_JAVA_BUILDING_FACTORY: bits |= BASE_ITEMS | BASE_POWER; break;
        case MC_JAVA_BUILDING_TURRET: bits |= BASE_ITEMS | BASE_LIQUIDS | BASE_POWER; break;
        default: break;
    }
    return bits;
}

static int32_t remap_content_id(const McContentRemap *remap, uint8_t type, int32_t saved_id){
    if(saved_id < 0) return -1;
    if(remap == NULL) return saved_id;
    return mc_content_remap_find(remap, type, (uint16_t)saved_id);
}

static McStatus read_item_module(McBuffer *in, bool legacy, const McContentRemap *remap, int32_t *items){
    size_t count = 0;
    if(legacy){
        if(read_count_u8(in, UINT8_MAX, &count) != MC_OK) return MC_FORMAT_ERROR;
    }else{
        bool negative = false;
        if(read_count_i16(in, INT16_MAX, &negative, &count) != MC_OK || negative) return MC_FORMAT_ERROR;
    }
    for(size_t i = 0; i < count; i++){
        int32_t saved_id = 0;
        if(legacy){
            uint8_t old_id = 0;
            if(mc_buffer_read_u8(in, &old_id) != MC_OK) return MC_FORMAT_ERROR;
            saved_id = old_id;
        }else{
            int16_t encoded = 0;
            if(read_i16(in, &encoded) != MC_OK) return MC_FORMAT_ERROR;
            saved_id = encoded;
        }
        int32_t amount = 0;
        if(read_i32(in, &amount) != MC_OK || amount < 0) return MC_FORMAT_ERROR;
        int32_t id = remap_content_id(remap, MC_CONTENT_ITEM, saved_id);
        if(id >= 0 && (uint32_t)id < MC_CONTENT_REGISTRY_ITEM_COUNT) items[id] = amount;
    }
    return MC_OK;
}

static McStatus write_item_module(McBuffer *out, bool legacy, const int32_t *items){
    size_t count = 0;
    for(size_t i = 0; i < MC_CONTENT_REGISTRY_ITEM_COUNT; i++) if(items[i] > 0) count++;
    if(legacy){
        if(count > UINT8_MAX) return MC_CAPACITY_EXCEEDED;
        McStatus status = mc_buffer_write_u8(out, (uint8_t)count);
        for(size_t i = 0; status == MC_OK && i < MC_CONTENT_REGISTRY_ITEM_COUNT; i++) if(items[i] > 0){
            status = mc_buffer_write_u8(out, (uint8_t)i);
            if(status == MC_OK) status = write_i32(out, items[i]);
        }
        return status;
    }
    McStatus status = write_count_i16(out, count);
    for(size_t i = 0; status == MC_OK && i < MC_CONTENT_REGISTRY_ITEM_COUNT; i++) if(items[i] > 0){
        status = write_i16(out, (int16_t)i);
        if(status == MC_OK) status = write_i32(out, items[i]);
    }
    return status;
}

static McStatus read_power_module(McBuffer *in, McJavaBuildingRecord *record){
    int16_t encoded = 0;
    if(read_i16(in, &encoded) != MC_OK || encoded < 0) return MC_FORMAT_ERROR;
    record->power_link_count = (uint16_t)((size_t)encoded > MC_JAVA_BUILDING_MAX_POWER_LINKS ? MC_JAVA_BUILDING_MAX_POWER_LINKS : (size_t)encoded);
    for(int32_t i = 0; i < encoded; i++){
        int32_t link = 0;
        if(read_i32(in, &link) != MC_OK) return MC_FORMAT_ERROR;
        if(i < (int32_t)MC_JAVA_BUILDING_MAX_POWER_LINKS) record->power_links[i] = link;
    }
    if(read_f32(in, &record->power_status) != MC_OK) return MC_FORMAT_ERROR;
    if(!isfinite(record->power_status)) record->power_status = 0.0f;
    return MC_OK;
}

static McStatus write_power_module(McBuffer *out, const McJavaBuildingRecord *record){
    if(record->power_link_count > MC_JAVA_BUILDING_MAX_POWER_LINKS) return MC_CAPACITY_EXCEEDED;
    McStatus status = write_i16(out, (int16_t)record->power_link_count);
    for(size_t i = 0; status == MC_OK && i < record->power_link_count; i++) status = write_i32(out, record->power_links[i]);
    if(status == MC_OK) status = write_f32(out, record->power_status);
    return status;
}

static McStatus read_liquid_module(McBuffer *in, bool legacy, const McContentRemap *remap, float *liquids){
    size_t count = 0;
    if(legacy){
        if(read_count_u8(in, UINT8_MAX, &count) != MC_OK) return MC_FORMAT_ERROR;
    }else{
        bool negative = false;
        if(read_count_i16(in, INT16_MAX, &negative, &count) != MC_OK || negative) return MC_FORMAT_ERROR;
    }
    for(size_t i = 0; i < count; i++){
        int32_t saved_id = 0;
        if(legacy){
            uint8_t value = 0;
            if(mc_buffer_read_u8(in, &value) != MC_OK) return MC_FORMAT_ERROR;
            saved_id = value;
        }else{
            int16_t encoded = 0;
            if(read_i16(in, &encoded) != MC_OK) return MC_FORMAT_ERROR;
            saved_id = encoded;
        }
        float amount = 0.0f;
        if(read_f32(in, &amount) != MC_OK || !isfinite(amount)) return MC_FORMAT_ERROR;
        int32_t id = remap_content_id(remap, MC_CONTENT_LIQUID, saved_id);
        if(id >= 0 && (uint32_t)id < MC_CONTENT_REGISTRY_LIQUID_COUNT) liquids[id] = amount;
    }
    return MC_OK;
}

static McStatus write_liquid_module(McBuffer *out, bool legacy, const float *liquids){
    size_t count = 0;
    for(size_t i = 0; i < MC_CONTENT_REGISTRY_LIQUID_COUNT; i++) if(liquids[i] > 0.0f) count++;
    McStatus status = legacy ? mc_buffer_write_u8(out, (uint8_t)count) : write_count_i16(out, count);
    for(size_t i = 0; status == MC_OK && i < MC_CONTENT_REGISTRY_LIQUID_COUNT; i++) if(liquids[i] > 0.0f){
        status = legacy ? mc_buffer_write_u8(out, (uint8_t)i) : write_i16(out, (int16_t)i);
        if(status == MC_OK) status = write_f32(out, liquids[i]);
    }
    return status;
}

static McStatus read_item_buffer(McBuffer *in, McJavaItemBuffer *buffer, bool legacy){
    uint8_t raw_index = 0, raw_length = 0;
    if(mc_buffer_read_u8(in, &raw_index) != MC_OK || mc_buffer_read_u8(in, &raw_length) != MC_OK) return MC_FORMAT_ERROR;
    int16_t index = (int16_t)(int8_t)raw_index;
    int16_t length = (int16_t)(int8_t)raw_length;
    buffer->index = index;
    buffer->encoded_capacity = length;
    for(int16_t i = 0; i < length; i++){
        uint64_t value = 0;
        if(read_u64_be(in, &value) != MC_OK) return MC_FORMAT_ERROR;
        if(legacy){
            uint64_t item = value & UINT64_C(0xff);
            uint64_t time = (value >> 8) & UINT64_C(0xffffffff);
            value = (time << 16) | item;
        }
        if((uint16_t)i < MC_JAVA_BUILDING_MAX_BUFFER_SLOTS) buffer->values[i] = value;
    }
    /* ItemBuffer.read() deliberately clamps against the encoded length, not
       the native array capacity. Keep its signed-byte behavior observable in
       the semantic record while retaining all bytes through raw_exact. */
    int32_t clamped = index < (int32_t)length - 1 ? index : (int32_t)length - 1;
    buffer->index = (int16_t)clamped;
    return MC_OK;
}

static McStatus write_item_buffer(McBuffer *out, const McJavaItemBuffer *buffer){
    if(buffer->encoded_capacity < 0 || buffer->encoded_capacity > UINT8_MAX ||
       (uint32_t)buffer->encoded_capacity > MC_JAVA_BUILDING_MAX_BUFFER_SLOTS) return MC_CAPACITY_EXCEEDED;
    McStatus status = mc_buffer_write_u8(out, (uint8_t)(int8_t)buffer->index);
    if(status == MC_OK) status = mc_buffer_write_u8(out, (uint8_t)(int8_t)buffer->encoded_capacity);
    for(size_t i = 0; status == MC_OK && i < (size_t)buffer->encoded_capacity; i++) status = write_u64_be(out, buffer->values[i]);
    return status;
}

static McStatus read_directional_buffer(McBuffer *in, McJavaDirectionalItemBuffer *buffer, bool legacy){
    for(size_t i = 0; i < 4; i++) if(read_item_buffer(in, &buffer->sides[i], legacy) != MC_OK) return MC_FORMAT_ERROR;
    return MC_OK;
}

static McStatus write_directional_buffer(McBuffer *out, const McJavaDirectionalItemBuffer *buffer){
    for(size_t i = 0; i < 4; i++) if(write_item_buffer(out, &buffer->sides[i]) != MC_OK) return MC_FORMAT_ERROR;
    return MC_OK;
}

static McStatus read_nullable_vec(McBuffer *in, bool *present, float *x, float *y){
    if(read_f32(in, x) != MC_OK || read_f32(in, y) != MC_OK) return MC_FORMAT_ERROR;
    *present = !isnan(*x) && !isnan(*y);
    return MC_OK;
}

static McStatus write_nullable_vec(McBuffer *out, bool present, float x, float y){
    if(!present){
        x = NAN;
        y = NAN;
    }
    McStatus status = write_f32(out, x);
    if(status == MC_OK) status = write_f32(out, y);
    return status;
}

static McStatus read_payload_seq(McBuffer *in, const McContentRemap *remap, McJavaPayloadEntry *entries, uint16_t *entry_count){
    int16_t encoded = 0;
    if(read_i16(in, &encoded) != MC_OK || encoded == INT16_MIN) return MC_FORMAT_ERROR;
    bool old_blocks = encoded >= 0;
    size_t count = old_blocks ? (size_t)encoded : (size_t)-(int32_t)encoded;
    if(count > MC_JAVA_BUILDING_MAX_PAYLOADS) return MC_FORMAT_ERROR;
    for(size_t i = 0; i < count; i++){
        uint8_t type = MC_CONTENT_BLOCK;
        int16_t saved_id = 0;
        if(!old_blocks && mc_buffer_read_u8(in, &type) != MC_OK) return MC_FORMAT_ERROR;
        if(read_i16(in, &saved_id) != MC_OK || read_i32(in, &entries[i].amount) != MC_OK) return MC_FORMAT_ERROR;
        int32_t mapped_id = remap_content_id(remap, type, saved_id);
        entries[i].content_type = type;
        entries[i].content_id = mapped_id >= INT16_MIN && mapped_id <= INT16_MAX ? (int16_t)mapped_id : -1;
    }
    *entry_count = (uint16_t)count;
    return MC_OK;
}

static McStatus write_payload_seq(McBuffer *out, const McJavaPayloadEntry *entries, uint16_t count){
    if(count > (int16_t)MC_JAVA_BUILDING_MAX_PAYLOADS) return MC_CAPACITY_EXCEEDED;
    McStatus status = write_i16(out, (int16_t)-count);
    for(size_t i = 0; status == MC_OK && i < count; i++){
        status = mc_buffer_write_u8(out, entries[i].content_type);
        if(status == MC_OK) status = write_i16(out, entries[i].content_id);
        if(status == MC_OK) status = write_i32(out, entries[i].amount);
    }
    return status;
}

static McStatus read_plan(McBuffer *in, const McContentRemap *remap, McJavaPlan *plan){
    uint8_t breaking = 0, has_config = 0;
    if(mc_buffer_read_u8(in, &breaking) != MC_OK || read_i32(in, &plan->packed_position) != MC_OK) return MC_FORMAT_ERROR;
    plan->breaking = breaking != 0;
    if(plan->breaking) return MC_OK;
    int16_t saved_block = 0;
    if(read_i16(in, &saved_block) != MC_OK || mc_buffer_read_u8(in, &plan->rotation) != MC_OK ||
       mc_buffer_read_u8(in, &has_config) != MC_OK) return MC_FORMAT_ERROR;
    int32_t mapped_block = remap_content_id(remap, MC_CONTENT_BLOCK, saved_block);
    plan->block_id = mapped_block >= 0 && mapped_block <= UINT16_MAX ? (uint16_t)mapped_block : UINT16_MAX;
    size_t start = in->position, config_size = 0;
    if(mc_typeio_skip(in, &config_size) != MC_OK) return MC_FORMAT_ERROR;
    if(has_config == 0){
        plan->config = NULL;
        plan->config_size = 0;
        return MC_OK;
    }
    plan->config = malloc(config_size);
    if(config_size != 0 && plan->config == NULL) return MC_OUT_OF_MEMORY;
    if(config_size != 0) memcpy(plan->config, in->data + start, config_size);
    plan->config_size = config_size;
    return MC_OK;
}

static McStatus write_plan(McBuffer *out, const McJavaPlan *plan){
    McStatus status = mc_buffer_write_u8(out, plan->breaking ? 1u : 0u);
    if(status == MC_OK) status = write_i32(out, plan->packed_position);
    if(plan->breaking || status != MC_OK) return status;
    status = write_i16(out, (int16_t)plan->block_id);
    if(status == MC_OK) status = mc_buffer_write_u8(out, plan->rotation);
    if(status == MC_OK) status = mc_buffer_write_u8(out, 1u);
    if(status == MC_OK){
        if(plan->config_size == 0) status = mc_buffer_write_u8(out, 0u);
        else status = mc_buffer_write_bytes(out, plan->config, plan->config_size);
    }
    return status;
}

static McStatus read_payload_value(McBuffer *in, McJavaPayloadPrefix *payload, bool block_prefix, const McContentRemap *remap){
    memset(payload, 0, sizeof(*payload));
    if(block_prefix){
        if(read_f32(in, &payload->x) != MC_OK || read_f32(in, &payload->y) != MC_OK ||
           read_f32(in, &payload->rotation) != MC_OK) return MC_FORMAT_ERROR;
    }
    bool present = false;
    if(read_bool8(in, &present) != MC_OK) return MC_FORMAT_ERROR;
    payload->present = present;
    if(!payload->present) return MC_OK;
    if(mc_buffer_read_u8(in, &payload->type) != MC_OK) return MC_FORMAT_ERROR;
    if(payload->type == 1u){
        int16_t saved_block = 0;
        if(read_i16(in, &saved_block) != MC_OK || mc_buffer_read_u8(in, &payload->revision) != MC_OK) return MC_FORMAT_ERROR;
        int32_t mapped_block = remap_content_id(remap, MC_CONTENT_BLOCK, saved_block);
        payload->block_id = mapped_block >= INT16_MIN && mapped_block <= INT16_MAX ? (int16_t)mapped_block : -1;
    }else if(payload->type == 0u){
        if(mc_buffer_read_u8(in, &payload->revision) != MC_OK) return MC_FORMAT_ERROR;
    }else{
        return MC_FORMAT_ERROR;
    }
    return MC_OK;
}

static McStatus write_payload_value(McBuffer *out, const McJavaPayloadPrefix *payload, bool block_prefix){
    McStatus status = MC_OK;
    if(block_prefix){
        status = write_f32(out, payload->x);
        if(status == MC_OK) status = write_f32(out, payload->y);
        if(status == MC_OK) status = write_f32(out, payload->rotation);
    }
    if(status != MC_OK) return status;
    if(payload->present) return MC_INVALID_ARGUMENT;
    return write_bool8(out, false);
}

static bool payload_is_opaque(const McJavaPayloadPrefix *payload){
    return payload->present;
}

static McStatus read_extension(McBuffer *in, McJavaBuildingRecord *record, const McContentRemap *remap){
    McJavaBuildingExtension *ext = &record->extension;
    McJavaBuildingVariant variant = ext->variant;
    McStatus status = MC_OK;
    if(variant == MC_JAVA_BUILDING_VARIANT_NONE){
        /* A building class absent from the compiled registry has no safe way
           to determine its subclass fields. The enclosing map chunk gives us
           an exact boundary, so retain the bytes rather than desynchronizing
           the rest of the MSAV stream. */
        in->position = in->size;
        return MC_OK;
    }

    switch(variant){
        case MC_JAVA_BUILDING_VARIANT_CONVEYOR:{
            size_t count = 0;
            status = read_count_i32(in, MC_JAVA_BUILDING_MAX_BUFFER_SLOTS, &count);
            ext->data.conveyor.count = (uint32_t)count;
            for(size_t i = 0; status == MC_OK && i < count; i++){
                if(record->revision == 0){
                    int32_t packed = 0;
                    status = read_i32(in, &packed);
                    int32_t saved_item = (int32_t)((uint32_t)packed >> 24);
                    int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
                    ext->data.conveyor.items[i].item_id = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
                    ext->data.conveyor.items[i].x = (int8_t)((uint32_t)packed >> 16);
                    ext->data.conveyor.items[i].y = (int8_t)((uint32_t)packed >> 8);
                }else{
                    int16_t saved_item = 0;
                    status = read_i16(in, &saved_item);
                    int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
                    ext->data.conveyor.items[i].item_id = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
                    if(status == MC_OK) status = (McStatus)mc_buffer_read_u8(in, (uint8_t *)&ext->data.conveyor.items[i].x);
                    if(status == MC_OK) status = (McStatus)mc_buffer_read_u8(in, (uint8_t *)&ext->data.conveyor.items[i].y);
                }
            }
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_CONVEYOR:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER:
            status = read_f32(in, &ext->data.conveyor.progress);
            if(status == MC_OK) status = read_f32(in, &ext->data.conveyor.item_rotation);
            if(status == MC_OK) status = read_payload_value(in, &ext->data.conveyor.payload, false, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.conveyor.payload)) in->position = in->size;
            if(status == MC_OK && variant == MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER && !payload_is_opaque(&ext->data.conveyor.payload)){
                int16_t saved_item = 0;
                status = mc_buffer_read_u8(in, &ext->data.conveyor.sort_type);
                if(status == MC_OK) status = read_i16(in, &saved_item);
                int32_t mapped_item = remap_content_id(remap, ext->data.conveyor.sort_type, saved_item);
                ext->data.conveyor.sort_item = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
                if(status == MC_OK) status = mc_buffer_read_u8(in, &ext->data.conveyor.rec_dir);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_ITEM_BUFFER:
            status = class_is(block_entry(record->block_id), "Junction") ? read_directional_buffer(in, &ext->data.directional_buffer.buffer, record->revision == 0) : read_item_buffer(in, &ext->data.item_buffer.buffer, false);
            break;
        case MC_JAVA_BUILDING_VARIANT_ITEM_BRIDGE:{
            status = read_i32(in, &ext->data.item_bridge.link);
            if(status == MC_OK) status = read_f32(in, &ext->data.item_bridge.warmup);
            size_t count = 0;
            if(status == MC_OK) status = read_count_signed_byte(in, MC_JAVA_BUILDING_MAX_INCOMING, &count);
            ext->data.item_bridge.incoming_count = (uint32_t)count;
            for(size_t i = 0; status == MC_OK && i < count; i++) status = read_i32(in, &ext->data.item_bridge.incoming[i]);
            if(status == MC_OK && record->revision >= 1) status = read_bool8(in, &ext->data.item_bridge.moved);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_BUFFERED_ITEM_BRIDGE:{
            status = read_i32(in, &ext->data.buffered_item_bridge.link);
            if(status == MC_OK) status = read_f32(in, &ext->data.buffered_item_bridge.warmup);
            size_t count = 0;
            if(status == MC_OK) status = read_count_signed_byte(in, MC_JAVA_BUILDING_MAX_INCOMING, &count);
            ext->data.buffered_item_bridge.incoming_count = (uint32_t)count;
            for(size_t i = 0; status == MC_OK && i < count; i++) status = read_i32(in, &ext->data.buffered_item_bridge.incoming[i]);
            if(status == MC_OK && record->revision >= 1) status = read_bool8(in, &ext->data.buffered_item_bridge.moved);
            if(status == MC_OK) status = read_item_buffer(in, &ext->data.buffered_item_bridge.buffer, false);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_SORTER:{
            int16_t saved_item = -1;
            if(class_is(block_entry(record->block_id), "Unloader") && record->revision != 1){
                uint8_t legacy_item = 0;
                status = mc_buffer_read_u8(in, &legacy_item);
                saved_item = (int8_t)legacy_item;
            }else{
                status = read_i16(in, &saved_item);
            }
            int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
            ext->data.item_filter.sort_item = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
            if(status == MC_OK && record->revision == 1 && !class_is(block_entry(record->block_id), "Unloader"))
                status = read_directional_buffer(in, &ext->data.directional_buffer.buffer, false);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_UNLOADER:{
            int16_t saved_item = 0;
            status = read_i16(in, &saved_item);
            int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
            ext->data.item_filter.sort_item = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
            if(status == MC_OK) status = read_i16(in, &ext->data.item_filter.offset);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_DUCT:
            if(record->revision >= 1) status = mc_buffer_read_u8(in, &ext->data.item_filter.rec_dir);
            break;
        case MC_JAVA_BUILDING_VARIANT_DUCT_JUNCTION:
            for(size_t i = 0; status == MC_OK && i < 4; i++){
                int16_t saved_item = 0;
                status = read_f32(in, &ext->data.duct_junction.times[i]);
                if(status == MC_OK) status = read_i16(in, &saved_item);
                int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
                ext->data.duct_junction.items[i] = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_DUCT_ROUTER:{
            int16_t saved_item = 0;
            if(record->revision == 0){
                status = MC_OK;
                break;
            }
            status = read_i16(in, &saved_item);
            int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
            ext->data.item_filter.sort_item = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_MASS_DRIVER:
            status = read_i32(in, &ext->data.mass_driver.link);
            if(status == MC_OK) status = read_f32(in, &ext->data.mass_driver.rotation);
            if(status == MC_OK) status = mc_buffer_read_u8(in, &ext->data.mass_driver.state);
            if(status == MC_OK && ext->data.mass_driver.state > 2u) status = MC_FORMAT_ERROR;
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_MASS_DRIVER:
            status = read_payload_value(in, &ext->data.mass_driver.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.mass_driver.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK) status = read_i32(in, &ext->data.mass_driver.link);
            if(status == MC_OK) status = read_f32(in, &ext->data.mass_driver.rotation);
            if(status == MC_OK) status = mc_buffer_read_u8(in, &ext->data.mass_driver.state);
            if(status == MC_OK && ext->data.mass_driver.state > 2u) status = MC_FORMAT_ERROR;
            if(status == MC_OK && record->revision >= 1) status = read_f32(in, &ext->data.mass_driver.reload);
            if(status == MC_OK && record->revision >= 1) status = read_f32(in, &ext->data.mass_driver.charge);
            if(status == MC_OK && record->revision >= 1) status = read_bool8(in, &ext->data.mass_driver.loaded);
            if(status == MC_OK && record->revision >= 1) status = read_bool8(in, &ext->data.mass_driver.charging);
            break;
        case MC_JAVA_BUILDING_VARIANT_STACK_CONVEYOR:
            status = read_i32(in, &ext->data.stack_conveyor.link);
            if(status == MC_OK) status = read_f32(in, &ext->data.stack_conveyor.cooldown);
            break;
        case MC_JAVA_BUILDING_VARIANT_CRAFTER:
            status = read_f32(in, &ext->data.crafter.progress);
            if(status == MC_OK) status = read_f32(in, &ext->data.crafter.warmup);
            if(status == MC_OK && strcmp(block_entry(record->block_id)->name, "cultivator") == 0){
                float ignored_legacy_warmup = 0.0f;
                status = read_f32(in, &ignored_legacy_warmup);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_SEPARATOR:
            status = read_f32(in, &ext->data.crafter.progress);
            if(status == MC_OK) status = read_f32(in, &ext->data.crafter.warmup);
            if(status == MC_OK && record->revision == 1) status = read_i32(in, &ext->data.crafter.seed);
            break;
        case MC_JAVA_BUILDING_VARIANT_DRILL:
            if(record->revision >= 1){
                status = read_f32(in, &ext->data.drill.progress);
                if(status == MC_OK) status = read_f32(in, &ext->data.drill.warmup);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_BEAM_DRILL:
            if(record->revision >= 1){
                status = read_f32(in, &ext->data.beam_drill.time);
                if(status == MC_OK) status = read_f32(in, &ext->data.beam_drill.warmup);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_FACTORY:
        case MC_JAVA_BUILDING_VARIANT_RECONSTRUCTOR:
            status = read_payload_value(in, &ext->data.factory.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.factory.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK && (variant == MC_JAVA_BUILDING_VARIANT_FACTORY || record->revision >= 1)) status = read_f32(in, &ext->data.factory.progress);
            if(status == MC_OK && variant == MC_JAVA_BUILDING_VARIANT_FACTORY) status = read_i16(in, &ext->data.factory.plan);
            if(status == MC_OK && record->revision >= 2) status = read_nullable_vec(in, &ext->data.factory.command_position, &ext->data.factory.command_x, &ext->data.factory.command_y);
            if(status == MC_OK && record->revision >= 3) status = mc_buffer_read_u8(in, &ext->data.factory.command);
            break;
        case MC_JAVA_BUILDING_VARIANT_ASSEMBLER:{
            status = read_payload_value(in, &ext->data.assembler.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.assembler.payload)){
                in->position = in->size;
                break;
            }
            status = read_f32(in, &ext->data.assembler.progress);
            size_t count = 0;
            if(status == MC_OK) status = read_count_u8(in, MC_JAVA_BUILDING_MAX_PLANS, &count);
            ext->data.assembler.unit_count = (uint8_t)count;
            for(size_t i = 0; status == MC_OK && i < count; i++) status = read_i32(in, &ext->data.assembler.units[i]);
            if(status == MC_OK){
                status = read_payload_seq(in, remap, ext->data.assembler.payloads, &ext->data.assembler.payload_count);
            }
            if(status == MC_OK && record->revision >= 1) status = read_nullable_vec(in, &ext->data.assembler.command_position, &ext->data.assembler.command_x, &ext->data.assembler.command_y);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_CONSTRUCTOR:
            status = read_payload_value(in, &ext->data.constructor.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.constructor.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK) status = read_f32(in, &ext->data.crafter.progress);
            if(status == MC_OK){
                int16_t saved_recipe = 0;
                status = read_i16(in, &saved_recipe);
                int32_t mapped_recipe = remap_content_id(remap, MC_CONTENT_BLOCK, saved_recipe);
                ext->data.constructor.recipe = mapped_recipe >= INT16_MIN && mapped_recipe <= INT16_MAX ? (int16_t)mapped_recipe : -1;
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_BLOCK_PRODUCER:
            status = read_payload_value(in, &ext->data.payload_producer.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.payload_producer.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK) status = read_f32(in, &ext->data.payload_producer.progress);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_DECONSTRUCTOR:{
            status = read_payload_value(in, &ext->data.payload_deconstructor.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.payload_deconstructor.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK) status = read_f32(in, &ext->data.payload_deconstructor.progress);
            int16_t count = 0;
            if(status == MC_OK) status = read_i16(in, &count);
            if(status == MC_OK && (count < 0 || count > (int16_t)MC_JAVA_BUILDING_MAX_PAYLOADS)) status = MC_FORMAT_ERROR;
            ext->data.payload_deconstructor.accumulator_count = (uint16_t)(count < 0 ? 0 : count);
            for(size_t i = 0; status == MC_OK && i < ext->data.payload_deconstructor.accumulator_count; i++) status = read_f32(in, &ext->data.payload_deconstructor.accumulators[i]);
            if(status == MC_OK){
                McJavaPayloadPrefix nested;
                status = read_payload_value(in, &nested, false, remap);
                ext->data.payload_deconstructor.payload_present = nested.present;
                if(status == MC_OK && nested.present) in->position = in->size;
            }
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_LOADER:
            status = read_payload_value(in, &ext->data.payload_loader.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.payload_loader.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK && record->revision >= 1) status = read_bool8(in, &ext->data.payload_loader.exporting);
            break;
        case MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_LOADER:{
            int32_t saved_unit = -1;
            status = read_i32(in, &saved_unit);
            int32_t mapped_unit = remap_content_id(remap, MC_CONTENT_UNIT, saved_unit);
            ext->data.unit_cargo_loader.unit_id = mapped_unit;
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_UNLOAD_POINT:{
            int16_t saved_item = 0;
            status = read_i16(in, &saved_item);
            int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
            ext->data.unit_cargo_unload_point.item_id = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
            if(status == MC_OK) status = read_bool8(in, &ext->data.unit_cargo_unload_point.stale);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_SOURCE:
            status = read_payload_value(in, &ext->data.payload_source.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.payload_source.payload)){
                in->position = in->size;
                break;
            }
            if(status == MC_OK){
                int16_t saved_unit = 0;
                status = read_i16(in, &saved_unit);
                int32_t mapped_unit = remap_content_id(remap, MC_CONTENT_UNIT, saved_unit);
                ext->data.payload_source.unit_id = mapped_unit >= INT16_MIN && mapped_unit <= INT16_MAX ? (int16_t)mapped_unit : -1;
            }
            if(status == MC_OK){
                int16_t saved_block = 0;
                status = read_i16(in, &saved_block);
                int32_t mapped_block = remap_content_id(remap, MC_CONTENT_BLOCK, saved_block);
                ext->data.payload_source.block_id = mapped_block >= INT16_MIN && mapped_block <= INT16_MAX ? (int16_t)mapped_block : -1;
            }
            if(status == MC_OK && record->revision >= 1) status = read_nullable_vec(in, &ext->data.payload_source.command_position, &ext->data.payload_source.command_x, &ext->data.payload_source.command_y);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_VOID:
        case MC_JAVA_BUILDING_VARIANT_UNIT_ASSEMBLER_MODULE:
            status = read_payload_value(in, &ext->data.payload_only.payload, true, remap);
            if(status == MC_OK && payload_is_opaque(&ext->data.payload_only.payload)) in->position = in->size;
            break;
        case MC_JAVA_BUILDING_VARIANT_TURRET:
            if(record->revision >= 1){
                status = read_f32(in, &ext->data.turret.reload);
                if(status == MC_OK) status = read_f32(in, &ext->data.turret.rotation);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_ITEM_TURRET:{
            if(record->revision >= 1){
                status = read_f32(in, &ext->data.turret.reload);
                if(status == MC_OK) status = read_f32(in, &ext->data.turret.rotation);
            }
            size_t count = 0;
            if(status == MC_OK) status = read_count_u8(in, MC_JAVA_BUILDING_MAX_AMMO, &count);
            ext->data.turret.ammo_count = (uint8_t)count;
            for(size_t i = 0; status == MC_OK && i < count; i++){
                int32_t saved_item = -1;
                if(record->revision < 2){
                    uint8_t old_item = 0;
                    status = mc_buffer_read_u8(in, &old_item);
                    saved_item = (int32_t)old_item;
                }else{
                    int16_t item = 0;
                    status = read_i16(in, &item);
                    saved_item = (int32_t)item;
                }
                int32_t mapped_item = remap_content_id(remap, MC_CONTENT_ITEM, saved_item);
                ext->data.turret.ammo[i].item_id = mapped_item >= INT16_MIN && mapped_item <= INT16_MAX ? (int16_t)mapped_item : -1;
                if(status == MC_OK) status = read_i16(in, &ext->data.turret.ammo[i].amount);
            }
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_CONTINUOUS_TURRET:
            if(record->revision >= 1){
                status = read_f32(in, &ext->data.turret.reload);
                if(status == MC_OK) status = read_f32(in, &ext->data.turret.rotation);
            }
            if(status == MC_OK && record->revision >= 3) status = read_f32(in, &ext->data.turret.last_length);
            break;
        case MC_JAVA_BUILDING_VARIANT_ROTATING_TURRET:
            status = read_f32(in, &ext->data.rotating_turret.rotation);
            break;
        case MC_JAVA_BUILDING_VARIANT_BUILD_TURRET:{
            status = read_f32(in, &ext->data.build_turret.rotation);
            int16_t count = 0;
            if(status == MC_OK) status = read_i16(in, &count);
            if(status == MC_OK && (count < -1 || count > (int16_t)MC_JAVA_BUILDING_MAX_PLANS)) status = MC_FORMAT_ERROR;
            ext->data.build_turret.plan_count = (uint16_t)(count < 0 ? 0 : count);
            for(size_t i = 0; status == MC_OK && i < ext->data.build_turret.plan_count; i++) status = read_plan(in, remap, &ext->data.build_turret.plans[i]);
            break;
        }
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_TURRET:
            if(record->revision >= 1){
                status = read_f32(in, &ext->data.payload_turret.reload);
                if(status == MC_OK) status = read_f32(in, &ext->data.payload_turret.rotation);
            }
            if(status == MC_OK) status = read_payload_seq(in, remap, ext->data.payload_turret.payloads, &ext->data.payload_turret.payload_count);
            break;
        default: status = MC_FORMAT_ERROR; break;
    }
    return status;
}

static McStatus write_extension(McBuffer *out, const McJavaBuildingRecord *record){
    const McJavaBuildingExtension *ext = &record->extension;
    if(ext->raw_exact) return mc_buffer_write_bytes(out, ext->raw, ext->raw_size);
    McStatus status = MC_OK;
    switch(ext->variant){
        case MC_JAVA_BUILDING_VARIANT_CONVEYOR:
            status = write_i32(out, (int32_t)ext->data.conveyor.count);
            for(size_t i = 0; status == MC_OK && i < ext->data.conveyor.count; i++){
                if(record->revision == 0){
                    uint32_t packed = ((uint32_t)(uint8_t)ext->data.conveyor.items[i].item_id << 24) |
                        ((uint32_t)(uint8_t)ext->data.conveyor.items[i].x << 16) |
                        ((uint32_t)(uint8_t)ext->data.conveyor.items[i].y << 8);
                    status = mc_buffer_write_u32_be(out, packed);
                }else{
                    status = write_i16(out, ext->data.conveyor.items[i].item_id);
                    if(status == MC_OK) status = mc_buffer_write_u8(out, (uint8_t)ext->data.conveyor.items[i].x);
                    if(status == MC_OK) status = mc_buffer_write_u8(out, (uint8_t)ext->data.conveyor.items[i].y);
                }
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_CONVEYOR:
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER:
            status = write_f32(out, ext->data.conveyor.progress);
            if(status == MC_OK) status = write_f32(out, ext->data.conveyor.item_rotation);
            if(status == MC_OK) status = write_payload_value(out, &ext->data.conveyor.payload, false);
            if(status == MC_OK && ext->variant == MC_JAVA_BUILDING_VARIANT_PAYLOAD_ROUTER){
                status = mc_buffer_write_u8(out, ext->data.conveyor.sort_type);
                if(status == MC_OK) status = write_i16(out, ext->data.conveyor.sort_item);
                if(status == MC_OK) status = mc_buffer_write_u8(out, ext->data.conveyor.rec_dir);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_ITEM_BUFFER:
            status = class_is(block_entry(record->block_id), "Junction") ? write_directional_buffer(out, &ext->data.directional_buffer.buffer) : write_item_buffer(out, &ext->data.item_buffer.buffer);
            break;
        case MC_JAVA_BUILDING_VARIANT_ITEM_BRIDGE:
            status = write_i32(out, ext->data.item_bridge.link);
            if(status == MC_OK) status = write_f32(out, ext->data.item_bridge.warmup);
            if(status == MC_OK && ext->data.item_bridge.incoming_count > INT8_MAX) return MC_CAPACITY_EXCEEDED;
            if(status == MC_OK) status = mc_buffer_write_u8(out, (uint8_t)ext->data.item_bridge.incoming_count);
            for(size_t i = 0; status == MC_OK && i < ext->data.item_bridge.incoming_count; i++) status = write_i32(out, ext->data.item_bridge.incoming[i]);
            if(status == MC_OK) status = write_bool8(out, ext->data.item_bridge.moved);
            break;
        case MC_JAVA_BUILDING_VARIANT_BUFFERED_ITEM_BRIDGE:
            status = write_i32(out, ext->data.buffered_item_bridge.link);
            if(status == MC_OK) status = write_f32(out, ext->data.buffered_item_bridge.warmup);
            if(status == MC_OK && ext->data.buffered_item_bridge.incoming_count > INT8_MAX) return MC_CAPACITY_EXCEEDED;
            if(status == MC_OK) status = mc_buffer_write_u8(out, (uint8_t)ext->data.buffered_item_bridge.incoming_count);
            for(size_t i = 0; status == MC_OK && i < ext->data.buffered_item_bridge.incoming_count; i++) status = write_i32(out, ext->data.buffered_item_bridge.incoming[i]);
            if(status == MC_OK) status = write_bool8(out, ext->data.buffered_item_bridge.moved);
            if(status == MC_OK) status = write_item_buffer(out, &ext->data.buffered_item_bridge.buffer);
            break;
        case MC_JAVA_BUILDING_VARIANT_SORTER:
            if(class_is(block_entry(record->block_id), "Unloader") && record->revision != 1){
                status = mc_buffer_write_u8(out, (uint8_t)(int8_t)ext->data.item_filter.sort_item);
            }else{
                status = write_i16(out, ext->data.item_filter.sort_item);
                if(status == MC_OK && record->revision == 1)
                    status = write_directional_buffer(out, &ext->data.directional_buffer.buffer);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_DIRECTIONAL_UNLOADER:
            status = write_i16(out, ext->data.item_filter.sort_item);
            if(status == MC_OK) status = write_i16(out, ext->data.item_filter.offset);
            break;
        case MC_JAVA_BUILDING_VARIANT_DUCT:
            if(record->revision >= 1) status = mc_buffer_write_u8(out, ext->data.item_filter.rec_dir);
            break;
        case MC_JAVA_BUILDING_VARIANT_DUCT_JUNCTION:
            for(size_t i = 0; status == MC_OK && i < 4; i++){
                status = write_f32(out, ext->data.duct_junction.times[i]);
                if(status == MC_OK) status = write_i16(out, ext->data.duct_junction.items[i]);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_DUCT_ROUTER:
            if(record->revision >= 1) status = write_i16(out, ext->data.item_filter.sort_item);
            break;
        case MC_JAVA_BUILDING_VARIANT_MASS_DRIVER:
            status = write_i32(out, ext->data.mass_driver.link);
            if(status == MC_OK) status = write_f32(out, ext->data.mass_driver.rotation);
            if(status == MC_OK) status = mc_buffer_write_u8(out, ext->data.mass_driver.state);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_MASS_DRIVER:
            status = write_payload_value(out, &ext->data.mass_driver.payload, true);
            if(status == MC_OK) status = write_i32(out, ext->data.mass_driver.link);
            if(status == MC_OK) status = write_f32(out, ext->data.mass_driver.rotation);
            if(status == MC_OK) status = mc_buffer_write_u8(out, ext->data.mass_driver.state);
            if(status == MC_OK) status = write_f32(out, ext->data.mass_driver.reload);
            if(status == MC_OK) status = write_f32(out, ext->data.mass_driver.charge);
            if(status == MC_OK) status = write_bool8(out, ext->data.mass_driver.loaded);
            if(status == MC_OK) status = write_bool8(out, ext->data.mass_driver.charging);
            break;
        case MC_JAVA_BUILDING_VARIANT_STACK_CONVEYOR:
            status = write_i32(out, ext->data.stack_conveyor.link);
            if(status == MC_OK) status = write_f32(out, ext->data.stack_conveyor.cooldown);
            break;
        case MC_JAVA_BUILDING_VARIANT_CRAFTER:
            status = write_f32(out, ext->data.crafter.progress);
            if(status == MC_OK) status = write_f32(out, ext->data.crafter.warmup);
            if(status == MC_OK && strcmp(block_entry(record->block_id)->name, "cultivator") == 0) status = write_f32(out, 0.0f);
            break;
        case MC_JAVA_BUILDING_VARIANT_SEPARATOR:
            status = write_f32(out, ext->data.crafter.progress);
            if(status == MC_OK) status = write_f32(out, ext->data.crafter.warmup);
            if(status == MC_OK && record->revision == 1) status = write_i32(out, ext->data.crafter.seed);
            break;
        case MC_JAVA_BUILDING_VARIANT_DRILL:
            status = write_f32(out, ext->data.drill.progress);
            if(status == MC_OK) status = write_f32(out, ext->data.drill.warmup);
            break;
        case MC_JAVA_BUILDING_VARIANT_BEAM_DRILL:
            status = write_f32(out, ext->data.beam_drill.time);
            if(status == MC_OK) status = write_f32(out, ext->data.beam_drill.warmup);
            break;
        case MC_JAVA_BUILDING_VARIANT_FACTORY:
        case MC_JAVA_BUILDING_VARIANT_RECONSTRUCTOR:
            status = write_payload_value(out, &ext->data.factory.payload, true);
            if(status == MC_OK && (ext->variant == MC_JAVA_BUILDING_VARIANT_FACTORY || record->revision >= 1))
                status = write_f32(out, ext->data.factory.progress);
            if(status == MC_OK && ext->variant == MC_JAVA_BUILDING_VARIANT_FACTORY) status = write_i16(out, ext->data.factory.plan);
            if(status == MC_OK && record->revision >= 2) status = write_nullable_vec(out, ext->data.factory.command_position, ext->data.factory.command_x, ext->data.factory.command_y);
            if(status == MC_OK && record->revision >= 3) status = mc_buffer_write_u8(out, ext->data.factory.command);
            break;
        case MC_JAVA_BUILDING_VARIANT_ASSEMBLER:
            status = write_payload_value(out, &ext->data.assembler.payload, true);
            if(status == MC_OK) status = write_f32(out, ext->data.assembler.progress);
            if(status == MC_OK) status = mc_buffer_write_u8(out, ext->data.assembler.unit_count);
            for(size_t i = 0; status == MC_OK && i < ext->data.assembler.unit_count; i++) status = write_i32(out, ext->data.assembler.units[i]);
            if(status == MC_OK) status = write_payload_seq(out, ext->data.assembler.payloads, ext->data.assembler.payload_count);
            if(status == MC_OK && record->revision >= 1) status = write_nullable_vec(out, ext->data.assembler.command_position, ext->data.assembler.command_x, ext->data.assembler.command_y);
            break;
        case MC_JAVA_BUILDING_VARIANT_CONSTRUCTOR:
            status = write_payload_value(out, &ext->data.constructor.payload, true);
            if(status == MC_OK) status = write_f32(out, ext->data.crafter.progress);
            if(status == MC_OK) status = write_i16(out, ext->data.constructor.recipe);
            break;
        case MC_JAVA_BUILDING_VARIANT_BLOCK_PRODUCER:
            status = write_payload_value(out, &ext->data.payload_producer.payload, true);
            if(status == MC_OK) status = write_f32(out, ext->data.payload_producer.progress);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_DECONSTRUCTOR:
            status = write_payload_value(out, &ext->data.payload_deconstructor.payload, true);
            if(status == MC_OK) status = write_f32(out, ext->data.payload_deconstructor.progress);
            if(status == MC_OK) status = write_i16(out, (int16_t)ext->data.payload_deconstructor.accumulator_count);
            for(size_t i = 0; status == MC_OK && i < ext->data.payload_deconstructor.accumulator_count; i++) status = write_f32(out, ext->data.payload_deconstructor.accumulators[i]);
            if(status == MC_OK && ext->data.payload_deconstructor.payload_present) return MC_INVALID_ARGUMENT;
            if(status == MC_OK) status = write_bool8(out, false);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_LOADER:
            status = write_payload_value(out, &ext->data.payload_loader.payload, true);
            if(status == MC_OK) status = write_bool8(out, ext->data.payload_loader.exporting);
            break;
        case MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_LOADER:
            status = write_i32(out, ext->data.unit_cargo_loader.unit_id);
            break;
        case MC_JAVA_BUILDING_VARIANT_UNIT_CARGO_UNLOAD_POINT:
            status = write_i16(out, ext->data.unit_cargo_unload_point.item_id);
            if(status == MC_OK) status = write_bool8(out, ext->data.unit_cargo_unload_point.stale);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_SOURCE:
            status = write_payload_value(out, &ext->data.payload_source.payload, true);
            if(status == MC_OK) status = write_i16(out, ext->data.payload_source.unit_id);
            if(status == MC_OK) status = write_i16(out, ext->data.payload_source.block_id);
            if(status == MC_OK) status = write_nullable_vec(out, ext->data.payload_source.command_position, ext->data.payload_source.command_x, ext->data.payload_source.command_y);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_VOID:
        case MC_JAVA_BUILDING_VARIANT_UNIT_ASSEMBLER_MODULE:
            status = write_payload_value(out, &ext->data.payload_only.payload, true);
            break;
        case MC_JAVA_BUILDING_VARIANT_TURRET:
            if(record->revision >= 1){
                status = write_f32(out, ext->data.turret.reload);
                if(status == MC_OK) status = write_f32(out, ext->data.turret.rotation);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_ITEM_TURRET:
            if(record->revision >= 1){
                status = write_f32(out, ext->data.turret.reload);
                if(status == MC_OK) status = write_f32(out, ext->data.turret.rotation);
            }
            if(status == MC_OK) status = mc_buffer_write_u8(out, ext->data.turret.ammo_count);
            for(size_t i = 0; status == MC_OK && i < ext->data.turret.ammo_count; i++){
                if(record->revision < 2){
                    if(ext->data.turret.ammo[i].item_id < -1 || ext->data.turret.ammo[i].item_id > INT8_MAX) return MC_INVALID_ARGUMENT;
                    status = mc_buffer_write_u8(out, (uint8_t)ext->data.turret.ammo[i].item_id);
                }else{
                    status = write_i16(out, ext->data.turret.ammo[i].item_id);
                }
                if(status == MC_OK) status = write_i16(out, ext->data.turret.ammo[i].amount);
            }
            break;
        case MC_JAVA_BUILDING_VARIANT_CONTINUOUS_TURRET:
            if(record->revision >= 1){
                status = write_f32(out, ext->data.turret.reload);
                if(status == MC_OK) status = write_f32(out, ext->data.turret.rotation);
            }
            if(status == MC_OK && record->revision >= 3) status = write_f32(out, ext->data.turret.last_length);
            break;
        case MC_JAVA_BUILDING_VARIANT_ROTATING_TURRET:
            status = write_f32(out, ext->data.rotating_turret.rotation);
            break;
        case MC_JAVA_BUILDING_VARIANT_BUILD_TURRET:
            status = write_f32(out, ext->data.build_turret.rotation);
            if(status == MC_OK) status = write_count_i16(out, ext->data.build_turret.plan_count);
            for(size_t i = 0; status == MC_OK && i < ext->data.build_turret.plan_count; i++) status = write_plan(out, &ext->data.build_turret.plans[i]);
            break;
        case MC_JAVA_BUILDING_VARIANT_PAYLOAD_TURRET:
            status = write_f32(out, ext->data.payload_turret.reload);
            if(status == MC_OK) status = write_f32(out, ext->data.payload_turret.rotation);
            if(status == MC_OK) status = write_payload_seq(out, ext->data.payload_turret.payloads, ext->data.payload_turret.payload_count);
            break;
        case MC_JAVA_BUILDING_VARIANT_NONE:
            status = MC_OK;
            break;
        default: return MC_FORMAT_ERROR;
    }
    return status;
}

static McStatus read_base(McBuffer *in, McJavaBuildingRecord *record, const McContentRemap *remap){
    uint8_t rotation = 0, team = 0;
    if(read_f32(in, &record->health) != MC_OK || mc_buffer_read_u8(in, &rotation) != MC_OK || mc_buffer_read_u8(in, &team) != MC_OK) return MC_FORMAT_ERROR;
    record->legacy_base = (rotation & 0x80u) == 0;
    record->rotation = rotation & 0x7fu;
    record->team = team;
    record->module_bits = default_module_bits(record);
    if(!record->legacy_base){
        if(mc_buffer_read_u8(in, &record->base_version) != MC_OK || record->base_version > 4) return MC_FORMAT_ERROR;
        if(record->base_version >= 1 && read_bool8(in, &record->enabled) != MC_OK) return MC_FORMAT_ERROR;
        if(record->base_version >= 2 && mc_buffer_read_u8(in, &record->module_bits) != MC_OK) return MC_FORMAT_ERROR;
    }else{
        record->base_version = 0;
    }
    memset(record->items, 0, sizeof(record->items));
    memset(record->liquids, 0, sizeof(record->liquids));
    record->power_link_count = 0;
    if((record->module_bits & BASE_ITEMS) != 0 && read_item_module(in, record->legacy_base, remap, record->items) != MC_OK) return MC_FORMAT_ERROR;
    if((record->module_bits & BASE_POWER) != 0 && read_power_module(in, record) != MC_OK) return MC_FORMAT_ERROR;
    if((record->module_bits & BASE_LIQUIDS) != 0 && read_liquid_module(in, record->legacy_base, remap, record->liquids) != MC_OK) return MC_FORMAT_ERROR;
    if((record->module_bits & BASE_TIMESCALE) != 0){
        if(read_f32(in, &record->time_scale) != MC_OK || read_f32(in, &record->time_scale_duration) != MC_OK) return MC_FORMAT_ERROR;
    }else{
        record->time_scale = 1.0f;
        record->time_scale_duration = 0.0f;
    }
    if((record->module_bits & BASE_DISABLER) != 0 && read_i32(in, &record->last_disabler) != MC_OK) return MC_FORMAT_ERROR;
    if(record->base_version <= 2){
        bool ignored = false;
        if(read_bool8(in, &ignored) != MC_OK) return MC_FORMAT_ERROR;
    }
    if(record->base_version >= 3){
        if(mc_buffer_read_u8(in, &record->efficiency) != MC_OK || mc_buffer_read_u8(in, &record->optional_efficiency) != MC_OK) return MC_FORMAT_ERROR;
    }
    if(record->base_version == 4 && read_u64_be(in, &record->visible_flags) != MC_OK) return MC_FORMAT_ERROR;
    return MC_OK;
}

static McStatus write_base(McBuffer *out, const McJavaBuildingRecord *record){
    if(record->rotation > 127 || !isfinite(record->health) || (record->module_bits & (uint8_t)~0x3fu)) return MC_INVALID_ARGUMENT;
    uint8_t rotation = record->rotation & 0x7fu;
    if(!record->legacy_base) rotation |= 0x80u;
    McStatus status = write_f32(out, record->health);
    if(status == MC_OK) status = mc_buffer_write_u8(out, rotation);
    if(status == MC_OK) status = mc_buffer_write_u8(out, record->team);
    if(status != MC_OK) return status;
    if(!record->legacy_base){
        if(record->base_version > 4) return MC_INVALID_ARGUMENT;
        status = mc_buffer_write_u8(out, record->base_version);
        if(status == MC_OK && record->base_version >= 1) status = write_bool8(out, record->enabled);
        if(status == MC_OK && record->base_version >= 2) status = mc_buffer_write_u8(out, record->module_bits);
    }
    if(status == MC_OK && (record->module_bits & BASE_ITEMS) != 0) status = write_item_module(out, record->legacy_base, record->items);
    if(status == MC_OK && (record->module_bits & BASE_POWER) != 0) status = write_power_module(out, record);
    if(status == MC_OK && (record->module_bits & BASE_LIQUIDS) != 0) status = write_liquid_module(out, record->legacy_base, record->liquids);
    if(status == MC_OK && (record->module_bits & BASE_TIMESCALE) != 0){
        status = write_f32(out, record->time_scale);
        if(status == MC_OK) status = write_f32(out, record->time_scale_duration);
    }
    if(status == MC_OK && (record->module_bits & BASE_DISABLER) != 0) status = write_i32(out, record->last_disabler);
    if(status == MC_OK && record->base_version <= 2) status = write_bool8(out, false);
    if(status == MC_OK && !record->legacy_base && record->base_version >= 3){
        status = mc_buffer_write_u8(out, record->efficiency);
        if(status == MC_OK) status = mc_buffer_write_u8(out, record->optional_efficiency);
    }
    if(status == MC_OK && !record->legacy_base && record->base_version == 4) status = write_u64_be(out, record->visible_flags);
    return status;
}

static McStatus read_building_internal(uint16_t block_id, uint8_t revision, const uint8_t *data, size_t size,
                                        const McContentRemap *remap, McJavaBuildingRecord *record){
    if(record == NULL || (data == NULL && size != 0)) return MC_INVALID_ARGUMENT;
    mc_java_building_record_destroy(record);
    mc_java_building_record_init(record, block_id, revision);
    if(block_entry(block_id) == NULL){
        if(size != 0){
            record->extension.raw = malloc(size);
            if(record->extension.raw == NULL) return MC_OUT_OF_MEMORY;
            memcpy(record->extension.raw, data, size);
        }
        record->extension.raw_size = size;
        record->extension.raw_exact = true;
        return MC_OK;
    }
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status = read_base(&input, record, remap);
    size_t extension_start = input.position;
    if(status == MC_OK) status = read_extension(&input, record, remap);
    if(status != MC_OK){
        mc_java_building_record_destroy(record);
        return status;
    }
    if(input.position != size){
        /* Java Reads does not assert end-of-chunk consumption. Preserve bytes
           after a known prefix so newer subtype revisions remain lossless. */
        size_t extension_size = size - extension_start;
        record->extension.raw = malloc(extension_size);
        if(record->extension.raw == NULL){
            mc_java_building_record_destroy(record);
            return MC_OUT_OF_MEMORY;
        }
        memcpy(record->extension.raw, data + extension_start, extension_size);
        record->extension.raw_size = extension_size;
        record->extension.raw_exact = true;
        return MC_OK;
    }
    size_t extension_size = size - extension_start;
    if(extension_size != 0){
        record->extension.raw = malloc(extension_size);
        if(record->extension.raw == NULL){
            mc_java_building_record_destroy(record);
            return MC_OUT_OF_MEMORY;
        }
        memcpy(record->extension.raw, data + extension_start, extension_size);
    }
    record->extension.raw_size = extension_size;
    record->extension.raw_exact = true;
    return MC_OK;
}

McStatus mc_java_building_read(uint16_t block_id, uint8_t revision, const uint8_t *data, size_t size,
                               McJavaBuildingRecord *record){
    return read_building_internal(block_id, revision, data, size, NULL, record);
}

McStatus mc_java_building_read_remap(uint16_t block_id, uint8_t revision, const uint8_t *data, size_t size,
                                     const McContentRemap *remap, McJavaBuildingRecord *record){
    return read_building_internal(block_id, revision, data, size, remap, record);
}

McStatus mc_java_building_write(const McJavaBuildingRecord *record, McBuffer *output){
    if(record == NULL || output == NULL || (block_entry(record->block_id) == NULL && !record->extension.raw_exact)) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    if(block_entry(record->block_id) == NULL && record->extension.raw_exact){
        return mc_buffer_write_bytes(output, record->extension.raw, record->extension.raw_size);
    }
    McStatus status = write_base(output, record);
    if(status == MC_OK) status = write_extension(output, record);
    return status;
}

McStatus mc_java_building_read_chunk(uint16_t block_id, const uint8_t *data, size_t size, McJavaBuildingRecord *record){
    if(data == NULL || record == NULL || size < 1) return MC_INVALID_ARGUMENT;
    return mc_java_building_read(block_id, data[0], data + 1, size - 1, record);
}

McStatus mc_java_building_read_chunk_remap(uint16_t block_id, const uint8_t *data, size_t size,
                                           const McContentRemap *remap, McJavaBuildingRecord *record){
    if(data == NULL || record == NULL || size < 1) return MC_INVALID_ARGUMENT;
    return mc_java_building_read_remap(block_id, data[0], data + 1, size - 1, remap, record);
}

McStatus mc_java_building_write_chunk(const McJavaBuildingRecord *record, McBuffer *output){
    if(record == NULL || output == NULL) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_u8(output, record->revision);
    if(status == MC_OK){
        McBuffer payload;
        mc_buffer_init(&payload);
        status = mc_java_building_write(record, &payload);
        if(status == MC_OK) status = mc_buffer_write_bytes(output, payload.data, payload.size);
        mc_buffer_destroy(&payload);
    }
    return status;
}

static bool legacy_block_for_registry(uint16_t block_id, McBlockId *legacy){
    const McContentEntry *entry = block_entry(block_id);
    if(entry == NULL || legacy == NULL) return false;
    for(int i = MC_BLOCK_AIR; i <= MC_BLOCK_DUO; i++){
        if(strcmp(entry->name, mc_block_name((McBlockId)i)) == 0){
            *legacy = (McBlockId)i;
            return true;
        }
    }
    return false;
}

McStatus mc_java_building_to_native(const McJavaBuildingRecord *record, McBuildingState *state){
    if(record == NULL || state == NULL) return MC_INVALID_ARGUMENT;
    McBlockId block;
    if(!legacy_block_for_registry(record->block_id, &block)) return MC_FORMAT_ERROR;
    McStatus status = mc_building_state_init(state, 0, block, (McTeam)record->team, 0, 0);
    if(status != MC_OK) return status;
    state->rotation = record->rotation;
    state->health = record->health;
    state->enabled = record->enabled;
    state->efficiency = (float)record->efficiency / 255.0f;
    for(size_t i = 0; i < MC_ITEM_COUNT && i < MC_CONTENT_REGISTRY_ITEM_COUNT; i++) state->inventory.items[i] = (uint32_t)record->items[i];
    for(size_t i = 0; i < MC_BUILDING_LIQUID_COUNT && i < MC_CONTENT_REGISTRY_LIQUID_COUNT; i++) state->liquids.amount[i] = record->liquids[i];
    state->power.satisfaction = record->power_status;
    return MC_OK;
}

McStatus mc_java_building_from_native(const McBuildingState *state, uint16_t block_id, uint8_t revision, McJavaBuildingRecord *record){
    if(state == NULL || record == NULL || block_entry(block_id) == NULL) return MC_INVALID_ARGUMENT;
    mc_java_building_record_destroy(record);
    mc_java_building_record_init(record, block_id, revision);
    record->team = (uint8_t)state->team;
    record->rotation = (uint8_t)(state->rotation & 127u);
    record->health = state->health;
    record->enabled = state->enabled;
    record->efficiency = (uint8_t)(fminf(fmaxf(state->efficiency, 0.0f), 1.0f) * 255.0f);
    for(size_t i = 0; i < MC_ITEM_COUNT && i < MC_CONTENT_REGISTRY_ITEM_COUNT; i++) record->items[i] = (int32_t)state->inventory.items[i];
    for(size_t i = 0; i < MC_BUILDING_LIQUID_COUNT && i < MC_CONTENT_REGISTRY_LIQUID_COUNT; i++) record->liquids[i] = state->liquids.amount[i];
    record->power_status = state->power.satisfaction;
    record->extension.raw_exact = false;
    return MC_OK;
}
