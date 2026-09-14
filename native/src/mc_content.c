#include "mindustry.h"
#include "mindustry_save.h"

#include <stdlib.h>
#include <string.h>

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

typedef struct McLegacyContentName{
    const char *old_name;
    const char *new_name;
} McLegacyContentName;

static const McLegacyContentName legacy_content_names[] = {
    {"craters", "crater-stone"}, {"deepwater", "deep-water"},
    {"water", "shallow-water"}, {"sand", "sand-floor"},
    {"slag", "molten-slag"}, {"mass-conveyor", "payload-conveyor"},
    {"vestige", "scepter"}, {"turbine-generator", "steam-generator"},
    {"fabricator", "tank-fabricator"}, {"basic-reconstructor", "refabricator"},
    {"rocks", "stone-wall"}, {"sporerocks", "spore-wall"},
    {"icerocks", "ice-wall"}, {"dunerocks", "dune-wall"},
    {"sandrocks", "sand-wall"}, {"shalerocks", "shale-wall"},
    {"snowrocks", "snow-wall"}, {"saltrocks", "salt-wall"},
    {"dirtwall", "dirt-wall"}, {"ignarock", "basalt"},
    {"holostone", "dacite"}, {"holostone-wall", "dacite-wall"},
    {"rock", "boulder"}, {"snowrock", "snow-boulder"},
    {"cliffs", "stone-wall"}, {"craters", "crater-stone"}
};

const char *mc_content_name_fallback(uint8_t type, const char *name){
    (void)type;
    if(name == NULL) return NULL;
    for(size_t i = 0; i < sizeof(legacy_content_names) / sizeof(legacy_content_names[0]); i++){
        if(strcmp(legacy_content_names[i].old_name, name) == 0) return legacy_content_names[i].new_name;
    }
    return name;
}

void mc_content_remap_destroy(McContentRemap *remap){
    if(remap == NULL) return;
    for(size_t i = 0; i < remap->count; i++) free(remap->groups[i].ids);
    free(remap->groups);
    *remap = (McContentRemap){0};
}

McStatus mc_content_remap_build(const McContentHeader *saved, const McContentGroupView *current,
                                size_t current_count, McContentRemap *remap){
    if(saved == NULL || remap == NULL || (current == NULL && current_count != 0)) return MC_INVALID_ARGUMENT;
    *remap = (McContentRemap){0};
    if(saved->count == 0) return MC_OK;
    remap->groups = calloc(saved->count, sizeof(*remap->groups));
    if(remap->groups == NULL) return MC_OUT_OF_MEMORY;
    remap->count = saved->count;
    for(size_t i = 0; i < saved->count; i++){
        const McContentGroup *source = &saved->groups[i];
        McContentRemapGroup *destination = &remap->groups[i];
        destination->type = source->type;
        destination->count = source->count;
        destination->ids = malloc(source->count * sizeof(*destination->ids));
        if(destination->ids == NULL){
            mc_content_remap_destroy(remap);
            return MC_OUT_OF_MEMORY;
        }
        for(size_t j = 0; j < source->count; j++){
            destination->ids[j] = -1;
            const char *fallback = mc_content_name_fallback(source->type, source->names[j]);
            for(size_t k = 0; k < current_count; k++){
                if(current[k].type != source->type) continue;
                for(size_t n = 0; n < current[k].count; n++){
                    if(strcmp(current[k].names[n], source->names[j]) == 0 ||
                       (fallback != NULL && strcmp(current[k].names[n], fallback) == 0)){
                        destination->ids[j] = (int32_t)n;
                        break;
                    }
                }
                if(destination->ids[j] >= 0) break;
            }
        }
    }
    return MC_OK;
}

int32_t mc_content_remap_find(const McContentRemap *remap, uint8_t type, uint16_t saved_id){
    if(remap == NULL) return -1;
    for(size_t i = 0; i < remap->count; i++){
        if(remap->groups[i].type == type){
            return saved_id < remap->groups[i].count ? remap->groups[i].ids[saved_id] : -1;
        }
    }
    return -1;
}
