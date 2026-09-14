#include "mindustry.h"

#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a_bytes(uint64_t hash, const void *data, size_t size){
    const unsigned char *bytes = (const unsigned char *)data;
    for(size_t i = 0; i < size; i++){
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

McStatus mc_world_init(McWorld *world, uint16_t width, uint16_t height){
    if(world == NULL || width == 0 || height == 0) return MC_INVALID_ARGUMENT;
    if((size_t)width > SIZE_MAX / (size_t)height / sizeof(McTile)) return MC_CAPACITY_EXCEEDED;

    world->width = width;
    world->height = height;
    world->tiles = calloc((size_t)width * height, sizeof(*world->tiles));
    if(world->tiles == NULL){
        world->width = 0;
        world->height = 0;
        return MC_OUT_OF_MEMORY;
    }
    mc_world_clear(world, MC_FLOOR_STONE);
    return MC_OK;
}

void mc_world_destroy(McWorld *world){
    if(world == NULL) return;
    free(world->tiles);
    world->tiles = NULL;
    world->width = 0;
    world->height = 0;
}

void mc_world_clear(McWorld *world, McFloor floor){
    if(world == NULL || world->tiles == NULL) return;
    for(size_t i = 0; i < (size_t)world->width * world->height; i++){
        world->tiles[i] = (McTile){.floor = (uint8_t)floor, .overlay = 0, .block = MC_BLOCK_AIR};
    }
}

McTile *mc_world_tile(McWorld *world, uint16_t x, uint16_t y){
    if(world == NULL || world->tiles == NULL || x >= world->width || y >= world->height) return NULL;
    return &world->tiles[(size_t)y * world->width + x];
}

const McTile *mc_world_tile_const(const McWorld *world, uint16_t x, uint16_t y){
    if(world == NULL || world->tiles == NULL || x >= world->width || y >= world->height) return NULL;
    return &world->tiles[(size_t)y * world->width + x];
}

uint64_t mc_world_hash(const McWorld *world){
    if(world == NULL) return 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = fnv1a_bytes(hash, &world->width, sizeof(world->width));
    hash = fnv1a_bytes(hash, &world->height, sizeof(world->height));
    if(world->tiles != NULL){
        hash = fnv1a_bytes(hash, world->tiles, (size_t)world->width * world->height * sizeof(*world->tiles));
    }
    return hash;
}

void mc_inventory_clear(McInventory *inventory, uint32_t capacity){
    if(inventory == NULL) return;
    memset(inventory->items, 0, sizeof(inventory->items));
    inventory->capacity = capacity;
}

uint32_t mc_inventory_count(const McInventory *inventory, McItemId item){
    if(inventory == NULL || item >= MC_ITEM_COUNT) return 0;
    return inventory->items[item];
}

uint32_t mc_inventory_add(McInventory *inventory, McItemId item, uint32_t amount){
    if(inventory == NULL || item >= MC_ITEM_COUNT || amount == 0) return 0;
    uint32_t total = 0;
    for(size_t i = 0; i < MC_ITEM_COUNT; i++) total += inventory->items[i];
    uint32_t free_space = total < inventory->capacity ? inventory->capacity - total : 0;
    uint32_t accepted = amount < free_space ? amount : free_space;
    inventory->items[item] += accepted;
    return accepted;
}

uint32_t mc_inventory_remove(McInventory *inventory, McItemId item, uint32_t amount){
    if(inventory == NULL || item >= MC_ITEM_COUNT || amount == 0) return 0;
    uint32_t removed = amount < inventory->items[item] ? amount : inventory->items[item];
    inventory->items[item] -= removed;
    return removed;
}
