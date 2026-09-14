#include "mindustry.h"

#include <string.h>

static uint64_t fnv1a_bytes(uint64_t hash, const void *data, size_t size){
    const unsigned char *bytes = (const unsigned char *)data;
    for(size_t i = 0; i < size; i++){
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint32_t next_random(McSimulation *simulation){
    /* xorshift64* is deterministic on every supported C implementation. */
    uint64_t value = simulation->random_state;
    if(value == 0) value = UINT64_C(0x9e3779b97f4a7c15);
    value ^= value >> 12;
    value ^= value << 25;
    value ^= value >> 27;
    simulation->random_state = value;
    return (uint32_t)((value * UINT64_C(2685821657736338717)) >> 32);
}

McStatus mc_simulation_init(McSimulation *simulation, uint16_t width, uint16_t height, uint64_t seed){
    if(simulation == NULL) return MC_INVALID_ARGUMENT;
    memset(simulation, 0, sizeof(*simulation));
    McStatus status = mc_world_init(&simulation->world, width, height);
    if(status != MC_OK) return status;
    mc_simulation_reset(simulation, seed);
    return MC_OK;
}

void mc_simulation_destroy(McSimulation *simulation){
    if(simulation == NULL) return;
    mc_world_destroy(&simulation->world);
    memset(simulation, 0, sizeof(*simulation));
}

void mc_simulation_reset(McSimulation *simulation, uint64_t seed){
    if(simulation == NULL) return;
    mc_world_clear(&simulation->world, MC_FLOOR_STONE);
    memset(simulation->entities, 0, sizeof(simulation->entities));
    simulation->tick = 0;
    simulation->next_entity_id = 1;
    simulation->random_state = seed == 0 ? UINT64_C(0x9e3779b97f4a7c15) : seed;
    simulation->wave_time = 0.0f;
    simulation->wave = 1;
    simulation->paused = false;
    for(size_t i = 0; i < MC_TEAM_COUNT; i++) mc_inventory_clear(&simulation->team_inventory[i], 10000);
}

McEntity *mc_simulation_find(McSimulation *simulation, McEntityId id){
    if(simulation == NULL || id == MC_ENTITY_NONE) return NULL;
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        if(simulation->entities[i].active && simulation->entities[i].id == id) return &simulation->entities[i];
    }
    return NULL;
}

const McEntity *mc_simulation_find_const(const McSimulation *simulation, McEntityId id){
    return mc_simulation_find((McSimulation *)simulation, id);
}

McEntity *mc_simulation_spawn(McSimulation *simulation, McEntityKind kind, McTeam team, float x, float y){
    if(simulation == NULL || team >= MC_TEAM_COUNT) return NULL;
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        McEntity *entity = &simulation->entities[i];
        if(entity->active) continue;
        memset(entity, 0, sizeof(*entity));
        entity->active = true;
        entity->id = simulation->next_entity_id++;
        if(entity->id == MC_ENTITY_NONE) entity->id = simulation->next_entity_id++;
        entity->kind = kind;
        entity->team = team;
        entity->position = (McVec2){x, y};
        entity->health = mc_entity_default_health(kind, MC_BLOCK_AIR);
        entity->max_health = entity->health;
        entity->lifetime = kind == MC_ENTITY_BULLET ? MC_TICKS_PER_SECOND : 0;
        return entity;
    }
    return NULL;
}

void mc_simulation_destroy_entity(McSimulation *simulation, McEntityId id){
    McEntity *entity = mc_simulation_find(simulation, id);
    if(entity != NULL) entity->active = false;
}

McStatus mc_simulation_place_block(McSimulation *simulation, McBlockId block, McTeam team, uint16_t x, uint16_t y){
    if(simulation == NULL || block == MC_BLOCK_AIR || team >= MC_TEAM_COUNT) return MC_INVALID_ARGUMENT;
    uint16_t size = mc_block_size(block);
    if(size == 0 || x + size > simulation->world.width || y + size > simulation->world.height) return MC_INVALID_ARGUMENT;

    for(uint16_t dy = 0; dy < size; dy++){
        for(uint16_t dx = 0; dx < size; dx++){
            McTile *tile = mc_world_tile(&simulation->world, (uint16_t)(x + dx), (uint16_t)(y + dy));
            if(tile == NULL || tile->block != MC_BLOCK_AIR) return MC_CAPACITY_EXCEEDED;
        }
    }
    /* Allocate the entity before mutating tiles, so a full entity pool cannot
       leave behind a block that has no corresponding building entity. */
    McEntity *entity = mc_simulation_spawn(simulation, MC_ENTITY_BUILDING, team,
        ((float)x + (float)size * 0.5f) * MC_TILE_SIZE,
        ((float)y + (float)size * 0.5f) * MC_TILE_SIZE);
    if(entity == NULL) return MC_CAPACITY_EXCEEDED;

    for(uint16_t dy = 0; dy < size; dy++){
        for(uint16_t dx = 0; dx < size; dx++){
            mc_world_tile(&simulation->world, (uint16_t)(x + dx), (uint16_t)(y + dy))->block = (uint8_t)block;
        }
    }
    entity->tile_x = x;
    entity->tile_y = y;
    entity->block = block;
    entity->health = mc_entity_default_health(MC_ENTITY_BUILDING, block);
    entity->max_health = entity->health;
    return MC_OK;
}

static void update_entity(McSimulation *simulation, McEntity *entity){
    float dt = 1.0f / (float)MC_TICKS_PER_SECOND;
    switch(entity->kind){
        case MC_ENTITY_UNIT:
        case MC_ENTITY_BULLET:
            entity->position.x += entity->velocity.x * dt;
            entity->position.y += entity->velocity.y * dt;
            break;
        case MC_ENTITY_BUILDING:
        case MC_ENTITY_EFFECT:
            break;
    }
    if(entity->kind == MC_ENTITY_BULLET && entity->lifetime > 0){
        entity->lifetime--;
        if(entity->lifetime == 0) entity->active = false;
    }
    (void)simulation;
}

McStatus mc_simulation_step(McSimulation *simulation){
    if(simulation == NULL || simulation->world.tiles == NULL) return MC_INVALID_ARGUMENT;
    if(simulation->paused) return MC_OK;
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        if(simulation->entities[i].active) update_entity(simulation, &simulation->entities[i]);
    }
    simulation->tick++;
    simulation->wave_time += 1.0f / (float)MC_TICKS_PER_SECOND;
    if(simulation->wave_time >= 60.0f){
        simulation->wave_time -= 60.0f;
        simulation->wave++;
        /* Consume the RNG on wave boundaries so future wave spawning is deterministic. */
        (void)next_random(simulation);
    }
    return MC_OK;
}

uint64_t mc_simulation_hash(const McSimulation *simulation){
    if(simulation == NULL) return 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = fnv1a_bytes(hash, &simulation->tick, sizeof(simulation->tick));
    hash = fnv1a_bytes(hash, &simulation->wave, sizeof(simulation->wave));
    hash = fnv1a_bytes(hash, &simulation->wave_time, sizeof(simulation->wave_time));
    hash = fnv1a_bytes(hash, &simulation->random_state, sizeof(simulation->random_state));
    hash = fnv1a_bytes(hash, &simulation->world.width, sizeof(simulation->world.width));
    hash = fnv1a_bytes(hash, &simulation->world.height, sizeof(simulation->world.height));
    hash = fnv1a_bytes(hash, simulation->world.tiles,
        (size_t)simulation->world.width * simulation->world.height * sizeof(*simulation->world.tiles));
    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        const McEntity *entity = &simulation->entities[i];
        if(!entity->active) continue;
        hash = fnv1a_bytes(hash, &entity->id, sizeof(entity->id));
        hash = fnv1a_bytes(hash, &entity->kind, sizeof(entity->kind));
        hash = fnv1a_bytes(hash, &entity->team, sizeof(entity->team));
        hash = fnv1a_bytes(hash, &entity->position, sizeof(entity->position));
        hash = fnv1a_bytes(hash, &entity->velocity, sizeof(entity->velocity));
        hash = fnv1a_bytes(hash, &entity->health, sizeof(entity->health));
        hash = fnv1a_bytes(hash, &entity->lifetime, sizeof(entity->lifetime));
        hash = fnv1a_bytes(hash, &entity->block, sizeof(entity->block));
    }
    return hash;
}
