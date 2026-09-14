#include "mindustry.h"

static const McItemDefinition item_definitions[MC_ITEM_COUNT] = {
    /* name, rgba, explosiveness, flammability, radioactivity, charge, hardness,
       cost, health scaling, low priority, buildable, hidden */
    {"scrap",          0x777777ffu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.5f, 0.0f, false, true,  false},
    {"copper",         0xd99d73ffu, 0.0f, 0.0f, 0.0f, 0.0f, 1, 0.5f, 0.0f, false, true,  false},
    {"lead",           0x8c7fa9ffu, 0.0f, 0.0f, 0.0f, 0.0f, 1, 0.7f, 0.0f, false, true,  false},
    {"graphite",       0xb2c6d2ffu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1.0f, 0.0f, false, true,  false},
    {"coal",           0x272727ffu, 0.2f, 1.0f, 0.0f, 0.0f, 2, 1.0f, 0.0f, false, false, false},
    {"titanium",       0x8da1e3ffu, 0.0f, 0.0f, 0.0f, 0.0f, 3, 1.0f, 0.0f, false, true,  false},
    {"thorium",        0xf9a3c7ffu, 0.2f, 0.0f, 1.0f, 0.0f, 4, 1.1f, 0.2f, false, true,  false},
    {"silicon",        0x53565cffu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.8f, 0.0f, false, true,  false},
    {"plastanium",     0xcbd97fffu, 0.2f, 0.1f, 0.0f, 0.0f, 0, 1.3f, 0.1f, false, true,  false},
    {"phase-fabric",   0xf4ba6effu, 0.0f, 0.0f, 0.6f, 0.0f, 0, 1.3f, 0.25f, false, true,  false},
    {"surge-alloy",    0xf3e979ffu, 0.0f, 0.0f, 0.0f, 0.75f, 0, 1.2f, 0.25f, false, true,  false},
    {"spore-pod",      0x7457ceffu, 0.0f, 1.15f, 0.0f, 0.0f, 0, 1.0f, 0.0f, false, false, false},
    {"sand",           0xf7cba4ffu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1.0f, 0.0f, true,  false, false},
    {"blast-compound", 0xff795effu, 1.2f, 0.4f, 0.0f, 0.0f, 0, 1.0f, 0.0f, false, false, false},
    {"pyratite",       0xffaa5fffu, 0.4f, 1.4f, 0.0f, 0.0f, 0, 1.0f, 0.0f, false, false, false},
    {"metaglass",      0xebeef5ffu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1.5f, 0.0f, false, true,  false},
    {"beryllium",      0x3a8f64ffu, 0.0f, 0.0f, 0.0f, 0.0f, 3, 1.2f, 0.6f, false, true,  false},
    {"tungsten",       0x768a9affu, 0.0f, 0.0f, 0.0f, 0.0f, 5, 1.5f, 0.8f, false, true,  false},
    {"oxide",          0xe4ffd6ffu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1.2f, 0.5f, false, true,  false},
    {"carbide",        0x89769affu, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1.4f, 1.1f, false, true,  false},
    {"fissile-matter", 0x5e988dffu, 0.0f, 0.0f, 1.5f, 0.0f, 0, 1.0f, 0.0f, false, true,  true},
    {"dormant-cyst",    0xdf824dffu, 0.0f, 0.1f, 0.0f, 0.0f, 0, 1.0f, 0.0f, false, true,  true}
};

const McItemDefinition *mc_item_definition(McItemId id){
    if(id >= MC_ITEM_COUNT) return NULL;
    return &item_definitions[id];
}

const McItemDefinition *mc_item_definitions(size_t *count){
    if(count != NULL) *count = MC_ITEM_COUNT;
    return item_definitions;
}

const char *mc_block_name(McBlockId id){
    static const char *names[] = {
        "air", "core-shard", "mechanical-drill", "conveyor", "duo"
    };
    return id <= MC_BLOCK_DUO ? names[id] : "unknown";
}

uint16_t mc_block_size(McBlockId id){
    switch(id){
        case MC_BLOCK_CORE_SHARD: return 3;
        case MC_BLOCK_MECHANICAL_DRILL: return 2;
        case MC_BLOCK_CONVEYOR: return 1;
        case MC_BLOCK_DUO: return 1;
        case MC_BLOCK_AIR: return 0;
        default: return 0;
    }
}

uint32_t mc_block_health(McBlockId id){
    switch(id){
        case MC_BLOCK_CORE_SHARD: return 1100;
        case MC_BLOCK_MECHANICAL_DRILL: return 160;
        case MC_BLOCK_CONVEYOR: return 45;
        case MC_BLOCK_DUO: return 40;
        case MC_BLOCK_AIR: return 0;
        default: return 0;
    }
}

float mc_entity_default_health(McEntityKind kind, McBlockId block){
    switch(kind){
        case MC_ENTITY_BUILDING: return (float)mc_block_health(block);
        case MC_ENTITY_UNIT: return 100.0f;
        case MC_ENTITY_BULLET: return 1.0f;
        case MC_ENTITY_EFFECT: return 1.0f;
        default: return 1.0f;
    }
}

float mc_entity_radius(McEntityKind kind, McBlockId block){
    if(kind == MC_ENTITY_BUILDING){
        return (float)mc_block_size(block) * MC_TILE_SIZE * 0.5f;
    }
    if(kind == MC_ENTITY_UNIT) return 4.0f;
    if(kind == MC_ENTITY_BULLET) return 1.0f;
    return 0.0f;
}
