#include "mindustry.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static uint64_t parse_u64(const char *text, uint64_t fallback){
    if(text == NULL || *text == '\0') return fallback;
    errno = 0;
    char *end = NULL;
    unsigned long long value = strtoull(text, &end, 10);
    if(errno != 0 || end == text || *end != '\0') return fallback;
    return (uint64_t)value;
}

int main(int argc, char **argv){
    uint64_t ticks = 600;
    if(argc == 3 && argv[1][0] == '-' && argv[1][1] == '-' &&
       argv[1][2] == 't' && argv[1][3] == 'i' && argv[1][4] == 'c' && argv[1][5] == 'k' && argv[1][6] == 's' && argv[1][7] == '\0'){
        ticks = parse_u64(argv[2], ticks);
    }else if(argc != 1){
        fprintf(stderr, "usage: %s [--ticks N]\n", argv[0]);
        return EXIT_FAILURE;
    }

    McSimulation simulation;
    McStatus status = mc_simulation_init(&simulation, 64, 64, UINT64_C(0x4d696e6475737472));
    if(status != MC_OK){
        fprintf(stderr, "could not initialize native simulation (%d)\n", status);
        return EXIT_FAILURE;
    }
    if(mc_simulation_place_block(&simulation, MC_BLOCK_CORE_SHARD, MC_TEAM_SHARDED, 30, 30) != MC_OK){
        fprintf(stderr, "could not place initial core\n");
        mc_simulation_destroy(&simulation);
        return EXIT_FAILURE;
    }
    McEntity *unit = mc_simulation_spawn(&simulation, MC_ENTITY_UNIT, MC_TEAM_SHARDED, 256.0f, 256.0f);
    if(unit == NULL){
        fprintf(stderr, "could not spawn initial unit\n");
        mc_simulation_destroy(&simulation);
        return EXIT_FAILURE;
    }
    unit->velocity.x = 8.0f;

    for(uint64_t i = 0; i < ticks; i++){
        if(mc_simulation_step(&simulation) != MC_OK){
            fprintf(stderr, "simulation failed at tick %" PRIu64 "\n", simulation.tick);
            mc_simulation_destroy(&simulation);
            return EXIT_FAILURE;
        }
    }

    printf("native simulation: ticks=%" PRIu64 " wave=%u hash=%016" PRIx64 "\n",
        simulation.tick, simulation.wave, mc_simulation_hash(&simulation));
    mc_simulation_destroy(&simulation);
    return EXIT_SUCCESS;
}
