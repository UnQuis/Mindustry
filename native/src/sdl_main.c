#include "mindustry.h"

#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>

static const int scale = 4;

static void draw_simulation(SDL_Renderer *renderer, const McSimulation *simulation){
    SDL_SetRenderDrawColor(renderer, 18, 20, 24, 255);
    SDL_RenderClear(renderer);

    for(uint16_t y = 0; y < simulation->world.height; y++){
        for(uint16_t x = 0; x < simulation->world.width; x++){
            const McTile *tile = mc_world_tile_const(&simulation->world, x, y);
            if(tile == NULL) continue;
            if(tile->floor == MC_FLOOR_SAND) SDL_SetRenderDrawColor(renderer, 210, 170, 120, 255);
            else if(tile->floor == MC_FLOOR_METAL) SDL_SetRenderDrawColor(renderer, 110, 120, 130, 255);
            else SDL_SetRenderDrawColor(renderer, 65, 70, 76, 255);
            SDL_Rect rect = {(int)x * scale, (int)y * scale, scale, scale};
            SDL_RenderFillRect(renderer, &rect);
            if(tile->block != MC_BLOCK_AIR){
                SDL_SetRenderDrawColor(renderer, 205, 155, 80, 255);
                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }

    for(size_t i = 0; i < MC_MAX_ENTITIES; i++){
        const McEntity *entity = &simulation->entities[i];
        if(!entity->active) continue;
        SDL_Rect rect = {
            (int)(entity->position.x / MC_TILE_SIZE * scale) - 2,
            (int)(entity->position.y / MC_TILE_SIZE * scale) - 2,
            4, 4
        };
        if(entity->kind == MC_ENTITY_BULLET) SDL_SetRenderDrawColor(renderer, 255, 235, 130, 255);
        else if(entity->team == MC_TEAM_CRUX) SDL_SetRenderDrawColor(renderer, 220, 70, 70, 255);
        else SDL_SetRenderDrawColor(renderer, 90, 220, 180, 255);
        SDL_RenderFillRect(renderer, &rect);
    }
    SDL_RenderPresent(renderer);
}

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0){
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("Mindustry C port",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 256, 256, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *renderer = window == NULL ? NULL : SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if(window == NULL || renderer == NULL){
        fprintf(stderr, "SDL window creation failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    McSimulation simulation;
    if(mc_simulation_init(&simulation, 64, 64, 1) != MC_OK ||
       mc_simulation_place_block(&simulation, MC_BLOCK_CORE_SHARD, MC_TEAM_SHARDED, 30, 30) != MC_OK){
        fprintf(stderr, "simulation initialization failed\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    McEntity *unit = mc_simulation_spawn(&simulation, MC_ENTITY_UNIT, MC_TEAM_SHARDED, 256.0f, 256.0f);
    if(unit != NULL) unit->velocity.x = 8.0f;

    bool running = true;
    uint64_t last = SDL_GetPerformanceCounter();
    double accumulator = 0.0;
    const double frequency = (double)SDL_GetPerformanceFrequency();
    while(running){
        SDL_Event event;
        while(SDL_PollEvent(&event)){
            if(event.type == SDL_QUIT) running = false;
            if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) running = false;
        }
        uint64_t now = SDL_GetPerformanceCounter();
        accumulator += (double)(now - last) / frequency;
        last = now;
        while(accumulator >= 1.0 / (double)MC_TICKS_PER_SECOND){
            mc_simulation_step(&simulation);
            accumulator -= 1.0 / (double)MC_TICKS_PER_SECOND;
        }
        draw_simulation(renderer, &simulation);
        SDL_Delay(1);
    }

    mc_simulation_destroy(&simulation);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
