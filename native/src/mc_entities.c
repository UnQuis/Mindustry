#include "mindustry_entities.h"

#include <stdlib.h>
#include <string.h>

static McStatus read_count(McBuffer *input, size_t minimum, size_t *count){
    uint32_t encoded = 0;
    if(mc_buffer_read_u32_be(input, &encoded) != MC_OK || encoded > INT32_MAX) return MC_FORMAT_ERROR;
    if((size_t)encoded > (input->size - input->position) / minimum && minimum != 0) return MC_FORMAT_ERROR;
    *count = (size_t)encoded;
    return MC_OK;
}

static McStatus copy_bytes(McBuffer *input, size_t size, uint8_t **destination){
    *destination = NULL;
    if(size == 0) return MC_OK;
    *destination = malloc(size);
    if(*destination == NULL) return MC_OUT_OF_MEMORY;
    if(mc_buffer_read_bytes(input, *destination, size) != MC_OK){
        free(*destination);
        *destination = NULL;
        return MC_FORMAT_ERROR;
    }
    return MC_OK;
}

void mc_entities_destroy(McEntitiesRegion *entities){
    if(entities == NULL) return;
    for(size_t i = 0; i < entities->mapping_count; i++) free(entities->mapping[i].name);
    for(size_t i = 0; i < entities->plan_count; i++) free(entities->plans[i].config);
    for(size_t i = 0; i < entities->record_count; i++) free(entities->records[i].data);
    free(entities->mapping);
    free(entities->plans);
    free(entities->records);
    *entities = (McEntitiesRegion){0};
}

McStatus mc_entities_read(const uint8_t *data, size_t size, McEntitiesRegion *entities){
    if(data == NULL || entities == NULL || size == 0) return MC_INVALID_ARGUMENT;
    *entities = (McEntitiesRegion){0};
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    McStatus status;
    uint16_t mapping_count = 0;
    if(mc_buffer_read_u16_be(&input, &mapping_count) != MC_OK) return MC_FORMAT_ERROR;
    if(mapping_count != 0){
        entities->mapping = calloc(mapping_count, sizeof(*entities->mapping));
        if(entities->mapping == NULL) return MC_OUT_OF_MEMORY;
        entities->mapping_count = mapping_count;
        for(size_t i = 0; i < entities->mapping_count; i++){
            if(mc_buffer_read_u16_be(&input, &entities->mapping[i].id) != MC_OK ||
               mc_save_read_utf(&input, &entities->mapping[i].name) != MC_OK){
                mc_entities_destroy(entities);
                return MC_FORMAT_ERROR;
            }
        }
    }

    size_t plan_team_count = 0;
    status = read_count(&input, 12, &plan_team_count);
    if(status != MC_OK) goto failure;
    for(size_t team_index = 0; team_index < plan_team_count; team_index++){
        uint32_t team_value = 0;
        if(mc_buffer_read_u32_be(&input, &team_value) != MC_OK) { status = MC_FORMAT_ERROR; goto failure; }
        size_t plans = 0;
        status = read_count(&input, 12, &plans);
        if(status != MC_OK || plans > SIZE_MAX - entities->plan_count) { status = MC_FORMAT_ERROR; goto failure; }
        size_t old_count = entities->plan_count;
        size_t new_count = old_count + plans;
        McTeamPlan *expanded = realloc(entities->plans, new_count * sizeof(*expanded));
        if(expanded == NULL && new_count != 0) { status = MC_OUT_OF_MEMORY; goto failure; }
        entities->plans = expanded;
        memset(entities->plans + old_count, 0, plans * sizeof(*entities->plans));
        entities->plan_count = new_count;
        for(size_t plan_index = 0; plan_index < plans; plan_index++){
            McTeamPlan *plan = &entities->plans[old_count + plan_index];
            plan->team = (int32_t)team_value;
            uint16_t value = 0;
            if(mc_buffer_read_u16_be(&input, &value) != MC_OK) { status = MC_FORMAT_ERROR; goto failure; }
            plan->x = (int16_t)value;
            if(mc_buffer_read_u16_be(&input, &value) != MC_OK) { status = MC_FORMAT_ERROR; goto failure; }
            plan->y = (int16_t)value;
            if(mc_buffer_read_u16_be(&input, &value) != MC_OK) { status = MC_FORMAT_ERROR; goto failure; }
            plan->rotation = (int16_t)value;
            if(mc_buffer_read_u16_be(&input, &plan->block) != MC_OK) { status = MC_FORMAT_ERROR; goto failure; }
            size_t config_start = input.position;
            size_t config_size = 0;
            status = mc_typeio_skip(&input, &config_size);
            if(status != MC_OK) goto failure;
            plan->config = malloc(config_size);
            if(config_size != 0 && plan->config == NULL) { status = MC_OUT_OF_MEMORY; goto failure; }
            if(config_size != 0) memcpy(plan->config, input.data + config_start, config_size);
            plan->config_size = config_size;
        }
    }

    status = read_count(&input, 4, &entities->record_count);
    if(status != MC_OK) goto failure;
    if(entities->record_count != 0){
        entities->records = calloc(entities->record_count, sizeof(*entities->records));
        if(entities->records == NULL) { status = MC_OUT_OF_MEMORY; goto failure; }
        for(size_t i = 0; i < entities->record_count; i++){
            uint32_t length = 0;
            if(mc_buffer_read_u32_be(&input, &length) != MC_OK || length < 5 ||
               length > input.size - input.position){
                status = MC_FORMAT_ERROR;
                goto failure;
            }
            entities->records[i].size = length;
            status = copy_bytes(&input, length, &entities->records[i].data);
            if(status != MC_OK) goto failure;
        }
    }
    if(input.position != input.size){ status = MC_FORMAT_ERROR; goto failure; }
    return MC_OK;

failure:
    mc_entities_destroy(entities);
    return status;
}

static McStatus write_i32_count(McBuffer *output, size_t count){
    if(count > INT32_MAX) return MC_CAPACITY_EXCEEDED;
    return mc_buffer_write_u32_be(output, (uint32_t)count);
}

McStatus mc_entities_write(const McEntitiesRegion *entities, McBuffer *output){
    if(entities == NULL || output == NULL || entities->mapping_count > UINT16_MAX) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_u16_be(output, (uint16_t)entities->mapping_count);
    for(size_t i = 0; status == MC_OK && i < entities->mapping_count; i++){
        if(entities->mapping[i].name == NULL) return MC_INVALID_ARGUMENT;
        status = mc_buffer_write_u16_be(output, entities->mapping[i].id);
        if(status == MC_OK) status = mc_save_write_utf(output, entities->mapping[i].name);
    }
    if(status != MC_OK) return status;

    /* The Java format groups plans by team. The native semantic model stores
       the team on each plan, so emit one group per contiguous team value. */
    size_t team_groups = 0;
    for(size_t i = 0; i < entities->plan_count;){
        int32_t team = entities->plans[i].team;
        team_groups++;
        size_t next = i + 1;
        while(next < entities->plan_count && entities->plans[next].team == team) next++;
        i = next;
    }
    status = write_i32_count(output, team_groups);
    for(size_t i = 0; status == MC_OK && i < entities->plan_count;){
        int32_t team = entities->plans[i].team;
        size_t next = i + 1;
        while(next < entities->plan_count && entities->plans[next].team == team) next++;
        status = mc_buffer_write_u32_be(output, (uint32_t)team);
        if(status == MC_OK) status = write_i32_count(output, next - i);
        for(size_t j = i; status == MC_OK && j < next; j++){
            const McTeamPlan *plan = &entities->plans[j];
            status = mc_buffer_write_u16_be(output, (uint16_t)plan->x);
            if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)plan->y);
            if(status == MC_OK) status = mc_buffer_write_u16_be(output, (uint16_t)plan->rotation);
            if(status == MC_OK) status = mc_buffer_write_u16_be(output, plan->block);
            if(status == MC_OK) status = mc_buffer_write_bytes(output, plan->config, plan->config_size);
        }
        i = next;
    }
    if(status != MC_OK) return status;

    status = write_i32_count(output, entities->record_count);
    for(size_t i = 0; status == MC_OK && i < entities->record_count; i++){
        const McEntityRecord *record = &entities->records[i];
        if(record->size < 5 || record->size > UINT32_MAX || (record->data == NULL && record->size != 0)) return MC_INVALID_ARGUMENT;
        status = mc_buffer_write_u32_be(output, (uint32_t)record->size);
        if(status == MC_OK) status = mc_buffer_write_bytes(output, record->data, record->size);
    }
    return status;
}
