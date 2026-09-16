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

static size_t world_tile_count(uint16_t width, uint16_t height){
    return (size_t)width * height;
}

McStatus mc_world_init(McWorld *world, uint16_t width, uint16_t height){
    if(world == NULL || width == 0 || height == 0) return MC_INVALID_ARGUMENT;
    if((size_t)width > SIZE_MAX / (size_t)height) return MC_CAPACITY_EXCEEDED;
    size_t count = world_tile_count(width, height);
    if(count > SIZE_MAX / sizeof(McTile) || count > SIZE_MAX / sizeof(McTileData)) return MC_CAPACITY_EXCEEDED;

    McTile *tiles = calloc(count, sizeof(*tiles));
    McTileData *tile_data = calloc(count, sizeof(*tile_data));
    if(tiles == NULL || tile_data == NULL){
        free(tiles);
        free(tile_data);
        return MC_OUT_OF_MEMORY;
    }
    world->width = width;
    world->height = height;
    world->tiles = tiles;
    world->tile_data = tile_data;
    mc_world_clear(world, MC_FLOOR_STONE);
    return MC_OK;
}

void mc_world_destroy(McWorld *world){
    if(world == NULL) return;
    free(world->tiles);
    free(world->tile_data);
    world->tiles = NULL;
    world->tile_data = NULL;
    world->width = 0;
    world->height = 0;
}

void mc_world_clear(McWorld *world, McFloor floor){
    if(world == NULL || world->tiles == NULL) return;
    size_t count = world_tile_count(world->width, world->height);
    for(size_t i = 0; i < count; i++){
        world->tiles[i] = (McTile){.floor = (uint16_t)floor, .overlay = 0, .block = MC_BLOCK_AIR};
        if(world->tile_data != NULL) world->tile_data[i] = (McTileData){0};
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

McTileData *mc_world_tile_data(McWorld *world, uint16_t x, uint16_t y){
    if(world == NULL || world->tile_data == NULL || x >= world->width || y >= world->height) return NULL;
    return &world->tile_data[(size_t)y * world->width + x];
}

const McTileData *mc_world_tile_data_const(const McWorld *world, uint16_t x, uint16_t y){
    if(world == NULL || world->tile_data == NULL || x >= world->width || y >= world->height) return NULL;
    return &world->tile_data[(size_t)y * world->width + x];
}

uint64_t mc_world_hash(const McWorld *world){
    if(world == NULL) return 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = fnv1a_bytes(hash, &world->width, sizeof(world->width));
    hash = fnv1a_bytes(hash, &world->height, sizeof(world->height));
    size_t count = world_tile_count(world->width, world->height);
    if(world->tiles != NULL) hash = fnv1a_bytes(hash, world->tiles, count * sizeof(*world->tiles));
    if(world->tile_data != NULL) hash = fnv1a_bytes(hash, world->tile_data, count * sizeof(*world->tile_data));
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

uint32_t mc_inventory_total(const McInventory *inventory){
    if(inventory == NULL) return 0;
    uint64_t total = 0;
    for(size_t i = 0; i < MC_ITEM_COUNT; i++) total += inventory->items[i];
    return total > UINT32_MAX ? UINT32_MAX : (uint32_t)total;
}

uint32_t mc_inventory_space(const McInventory *inventory){
    if(inventory == NULL) return 0;
    uint32_t total = mc_inventory_total(inventory);
    return total < inventory->capacity ? inventory->capacity - total : 0;
}

uint32_t mc_inventory_add(McInventory *inventory, McItemId item, uint32_t amount){
    if(inventory == NULL || item >= MC_ITEM_COUNT || amount == 0) return 0;
    uint32_t accepted = amount < mc_inventory_space(inventory) ? amount : mc_inventory_space(inventory);
    inventory->items[item] += accepted;
    return accepted;
}

uint32_t mc_inventory_remove(McInventory *inventory, McItemId item, uint32_t amount){
    if(inventory == NULL || item >= MC_ITEM_COUNT || amount == 0) return 0;
    uint32_t removed = amount < inventory->items[item] ? amount : inventory->items[item];
    inventory->items[item] -= removed;
    return removed;
}

void mc_inventory_transfer(McInventory *from, McInventory *to, McItemId item, uint32_t amount){
    if(from == NULL || to == NULL || from == to) return;
    uint32_t removed = mc_inventory_remove(from, item, amount);
    uint32_t accepted = mc_inventory_add(to, item, removed);
    if(accepted < removed) (void)mc_inventory_add(from, item, removed - accepted);
}
