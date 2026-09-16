#include "mindustry_combat.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define COMBAT_MAGIC_0 ((uint8_t)'M')
#define COMBAT_MAGIC_1 ((uint8_t)'C')
#define COMBAT_MAGIC_2 ((uint8_t)'B')
#define COMBAT_MAGIC_3 ((uint8_t)'T')
#define COMBAT_VERSION 1u
#define COMBAT_MAX_COLLECTION 1000000u

static const McBulletDefinition bullet_definitions[] = {
    {0, "placeholder", 2.5f, 9.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0.0f, true, true, true, false, false, false},
    {1, "space-liquid", 1.0f, 0.0f, 60.0f, 0.01f, 0.0f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0, 0, 0, 0.0f, true, true, true, false, false, false},
    {2, "damage-lightning", 0.0001f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 3, 14, 10.0f, false, true, true, false, false, false},
    {3, "damage-lightning-ground", 0.0001f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 3, 14, 10.0f, false, true, false, false, false, false},
    {4, "damage-lightning-air", 0.0001f, 0.0f, 30.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 3, 14, 10.0f, false, false, true, false, false, false},
    {5, "fireball", 1.0f, 4.0f, 60.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 1, 60.0f, true, true, true, false, true, false}
};

static McStatus write_u64(McBuffer *out, uint64_t value){
    McStatus status = MC_OK;
    for(int i = 7; i >= 0 && status == MC_OK; i--) status = mc_buffer_write_u8(out, (uint8_t)(value >> (i * 8)));
    return status;
}

static McStatus read_u64(McBuffer *in, uint64_t *value){
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

static McStatus write_bool(McBuffer *out, bool value){
    return mc_buffer_write_u8(out, value ? 1u : 0u);
}

static McStatus read_bool(McBuffer *in, bool *value){
    uint8_t encoded = 0;
    McStatus status = mc_buffer_read_u8(in, &encoded);
    if(status != MC_OK) return status;
    if(encoded > 1) return MC_FORMAT_ERROR;
    *value = encoded != 0;
    return MC_OK;
}

static McStatus write_count(McBuffer *out, size_t count){
    if(count > COMBAT_MAX_COLLECTION) return MC_CAPACITY_EXCEEDED;
    return mc_buffer_write_u32_be(out, (uint32_t)count);
}

static McStatus read_count(McBuffer *in, size_t *count){
    uint32_t encoded = 0;
    if(mc_buffer_read_u32_be(in, &encoded) != MC_OK || encoded > COMBAT_MAX_COLLECTION) return MC_FORMAT_ERROR;
    *count = encoded;
    return MC_OK;
}

static bool finite_state(float value){
    return isfinite(value);
}

const McBulletDefinition *mc_bullet_definition(uint16_t id){
    return id < sizeof(bullet_definitions) / sizeof(bullet_definitions[0]) ? &bullet_definitions[id] : NULL;
}

const McBulletDefinition *mc_bullet_definition_by_name(const char *name){
    if(name == NULL) return NULL;
    for(size_t i = 0; i < sizeof(bullet_definitions) / sizeof(bullet_definitions[0]); i++){
        if(strcmp(bullet_definitions[i].name, name) == 0) return &bullet_definitions[i];
    }
    return NULL;
}

size_t mc_bullet_definition_count(void){
    return sizeof(bullet_definitions) / sizeof(bullet_definitions[0]);
}

McStatus mc_bullet_definition_validate(void){
    for(size_t i = 0; i < mc_bullet_definition_count(); i++){
        const McBulletDefinition *definition = &bullet_definitions[i];
        if(definition->id != i || definition->name == NULL || definition->speed < 0.0f || definition->lifetime <= 0.0f ||
           !finite_state(definition->damage) || !finite_state(definition->speed)) return MC_FORMAT_ERROR;
    }
    return mc_bullet_definition_count() == MC_CONTENT_REGISTRY_BULLET_COUNT ? MC_OK : MC_FORMAT_ERROR;
}

static McStatus reserve_targets(McCombatWorld *world, size_t needed){
    if(needed <= world->target_capacity) return MC_OK;
    size_t capacity = world->target_capacity == 0 ? 16 : world->target_capacity * 2;
    while(capacity < needed) capacity *= 2;
    McCombatTarget *expanded = realloc(world->targets, capacity * sizeof(*expanded));
    if(expanded == NULL) return MC_OUT_OF_MEMORY;
    world->targets = expanded;
    world->target_capacity = capacity;
    return MC_OK;
}

static McStatus reserve_bullets(McCombatWorld *world, size_t needed){
    if(needed <= world->bullet_capacity) return MC_OK;
    size_t capacity = world->bullet_capacity == 0 ? 32 : world->bullet_capacity * 2;
    while(capacity < needed) capacity *= 2;
    McCombatBullet *expanded = realloc(world->bullets, capacity * sizeof(*expanded));
    if(expanded == NULL) return MC_OUT_OF_MEMORY;
    world->bullets = expanded;
    world->bullet_capacity = capacity;
    return MC_OK;
}

static McStatus reserve_turrets(McCombatWorld *world, size_t needed){
    if(needed <= world->turret_capacity) return MC_OK;
    size_t capacity = world->turret_capacity == 0 ? 8 : world->turret_capacity * 2;
    while(capacity < needed) capacity *= 2;
    McCombatTurret *expanded = realloc(world->turrets, capacity * sizeof(*expanded));
    if(expanded == NULL) return MC_OUT_OF_MEMORY;
    world->turrets = expanded;
    world->turret_capacity = capacity;
    return MC_OK;
}

static McStatus reserve_events(McCombatWorld *world, size_t needed){
    if(needed <= world->event_capacity) return MC_OK;
    size_t capacity = world->event_capacity == 0 ? 64 : world->event_capacity * 2;
    while(capacity < needed) capacity *= 2;
    McCombatEvent *expanded = realloc(world->events, capacity * sizeof(*expanded));
    if(expanded == NULL) return MC_OUT_OF_MEMORY;
    world->events = expanded;
    world->event_capacity = capacity;
    return MC_OK;
}

static void compact_targets(McCombatWorld *world){
    size_t write = 0;
    for(size_t i = 0; i < world->target_count; i++){
        if(world->targets[i].active) world->targets[write++] = world->targets[i];
    }
    world->target_count = write;
}

static void compact_bullets(McCombatWorld *world){
    size_t write = 0;
    for(size_t i = 0; i < world->bullet_count; i++){
        if(world->bullets[i].active) world->bullets[write++] = world->bullets[i];
    }
    world->bullet_count = write;
}

void mc_combat_init(McCombatWorld *world, size_t max_events){
    if(world == NULL) return;
    memset(world, 0, sizeof(*world));
    world->max_events = max_events;
    world->next_bullet_id = 1;
    world->next_event = 1;
}

void mc_combat_destroy(McCombatWorld *world){
    if(world == NULL) return;
    free(world->targets);
    free(world->bullets);
    free(world->turrets);
    free(world->events);
    memset(world, 0, sizeof(*world));
}

McStatus mc_combat_clear(McCombatWorld *world){
    if(world == NULL) return MC_INVALID_ARGUMENT;
    world->target_count = 0;
    world->bullet_count = 0;
    world->turret_count = 0;
    world->event_count = 0;
    world->tick = 0;
    world->shots = 0;
    world->hits = 0;
    world->kills = 0;
    world->damage_events = 0;
    return MC_OK;
}

McStatus mc_combat_set_event_limit(McCombatWorld *world, size_t max_events){
    if(world == NULL) return MC_INVALID_ARGUMENT;
    world->max_events = max_events;
    if(max_events == 0){
        world->event_count = 0;
        return MC_OK;
    }
    if(world->event_count > max_events){
        size_t first = world->event_count - max_events;
        memmove(world->events, world->events + first, max_events * sizeof(*world->events));
        world->event_count = max_events;
    }
    return MC_OK;
}

McCombatTarget *mc_combat_add_target(McCombatWorld *world, McEntityId id, McCombatTargetKind kind,
                                     uint8_t team, float x, float y, float radius, float health, float armor){
    if(world == NULL || radius < 0.0f || health < 0.0f || armor < 0.0f || !finite_state(x) || !finite_state(y)) return NULL;
    McCombatTarget *existing = mc_combat_find_target(world, id);
    if(existing != NULL){
        existing->kind = kind;
        existing->team = team;
        existing->x = x;
        existing->y = y;
        existing->radius = radius;
        existing->health = health;
        existing->max_health = health;
        existing->armor = armor;
        existing->active = true;
        return existing;
    }
    if(reserve_targets(world, world->target_count + 1) != MC_OK) return NULL;
    McCombatTarget *target = &world->targets[world->target_count++];
    memset(target, 0, sizeof(*target));
    target->id = id;
    target->kind = kind;
    target->team = team;
    target->x = x;
    target->y = y;
    target->radius = radius;
    target->health = health;
    target->max_health = health;
    target->armor = armor;
    target->active = true;
    return target;
}

McStatus mc_combat_remove_target(McCombatWorld *world, McEntityId id){
    McCombatTarget *target = mc_combat_find_target(world, id);
    if(target == NULL) return MC_NOT_FOUND;
    target->active = false;
    compact_targets(world);
    return MC_OK;
}

McCombatTarget *mc_combat_find_target(McCombatWorld *world, McEntityId id){
    if(world == NULL) return NULL;
    for(size_t i = 0; i < world->target_count; i++) if(world->targets[i].active && world->targets[i].id == id) return &world->targets[i];
    return NULL;
}

const McCombatTarget *mc_combat_find_target_const(const McCombatWorld *world, McEntityId id){
    return mc_combat_find_target((McCombatWorld *)world, id);
}

McCombatTurret *mc_combat_add_turret(McCombatWorld *world, McEntityId id, uint8_t team,
                                     float x, float y, uint16_t bullet_type){
    if(world == NULL || mc_bullet_definition(bullet_type) == NULL) return NULL;
    McCombatTurret *existing = mc_combat_find_turret(world, id);
    if(existing != NULL){
        existing->team = team;
        existing->x = x;
        existing->y = y;
        existing->bullet_type = bullet_type;
        existing->active = true;
        return existing;
    }
    if(reserve_turrets(world, world->turret_count + 1) != MC_OK) return NULL;
    McCombatTurret *turret = &world->turrets[world->turret_count++];
    *turret = (McCombatTurret){
        .id = id, .team = team, .x = x, .y = y, .range = 80.0f, .rotation = 0.0f,
        .target_rotation = 0.0f, .reload = 0.0f, .reload_time = 60.0f,
        .power_efficiency = 1.0f, .coolant_multiplier = 1.0f,
        .bullet_type = bullet_type, .ammo = UINT32_MAX, .ammo_capacity = UINT32_MAX,
        .target = MC_ENTITY_NONE, .active = true, .enabled = true, .continuous = false
    };
    return turret;
}

McStatus mc_combat_remove_turret(McCombatWorld *world, McEntityId id){
    if(world == NULL) return MC_INVALID_ARGUMENT;
    for(size_t i = 0; i < world->turret_count; i++){
        if(world->turrets[i].id == id){
            world->turrets[i] = world->turrets[--world->turret_count];
            return MC_OK;
        }
    }
    return MC_NOT_FOUND;
}

McCombatTurret *mc_combat_find_turret(McCombatWorld *world, McEntityId id){
    if(world == NULL) return NULL;
    for(size_t i = 0; i < world->turret_count; i++) if(world->turrets[i].active && world->turrets[i].id == id) return &world->turrets[i];
    return NULL;
}

McStatus mc_combat_set_turret(McCombatTurret *turret, float range, float reload_time,
                               uint32_t ammo, float power_efficiency, float coolant_multiplier){
    if(turret == NULL || range < 0.0f || reload_time <= 0.0f || power_efficiency < 0.0f || coolant_multiplier < 0.0f) return MC_INVALID_ARGUMENT;
    turret->range = range;
    turret->reload_time = reload_time;
    turret->ammo = ammo;
    turret->ammo_capacity = ammo;
    turret->power_efficiency = power_efficiency;
    turret->coolant_multiplier = coolant_multiplier;
    return MC_OK;
}

static McStatus push_event(McCombatWorld *world, uint64_t tick, McCombatDamageKind kind,
                           McEntityId source, McEntityId target, uint16_t bullet_type,
                           uint8_t status_id, float amount, float x, float y){
    if(world->max_events == 0) return MC_OK;
    if(world->event_count == world->max_events){
        if(world->event_count > 1) memmove(world->events, world->events + 1, (world->event_count - 1) * sizeof(*world->events));
        world->event_count--;
    }
    if(reserve_events(world, world->event_count + 1) != MC_OK) return MC_OUT_OF_MEMORY;
    world->events[world->event_count++] = (McCombatEvent){
        .sequence = world->next_event++, .tick = tick, .kind = kind, .source = source,
        .target = target, .bullet_type = bullet_type, .status_id = status_id,
        .amount = amount, .x = x, .y = y
    };
    return MC_OK;
}

McCombatBullet *mc_combat_fire(McCombatWorld *world, McEntityId owner, McEntityId shooter,
                               uint8_t team, uint16_t bullet_type, float x, float y, float angle,
                               float damage_scale){
    const McBulletDefinition *definition = mc_bullet_definition(bullet_type);
    if(world == NULL || definition == NULL || !finite_state(x) || !finite_state(y) || !finite_state(angle) || damage_scale < 0.0f) return NULL;
    if(reserve_bullets(world, world->bullet_count + 1) != MC_OK) return NULL;
    McCombatBullet *bullet = &world->bullets[world->bullet_count++];
    memset(bullet, 0, sizeof(*bullet));
    bullet->id = world->next_bullet_id++;
    if(bullet->id == MC_ENTITY_NONE) bullet->id = world->next_bullet_id++;
    bullet->owner = owner;
    bullet->shooter = shooter;
    bullet->type_id = bullet_type;
    bullet->team = team;
    bullet->x = x;
    bullet->y = y;
    bullet->last_x = x;
    bullet->last_y = y;
    bullet->velocity_x = cosf(angle) * definition->speed;
    bullet->velocity_y = sinf(angle) * definition->speed;
    bullet->time = 0.0f;
    bullet->lifetime = definition->lifetime;
    bullet->damage = definition->damage * damage_scale;
    bullet->hit_size = 2.0f;
    bullet->pierce_left = definition->pierce ? definition->pierce_cap : 1;
    bullet->active = true;
    world->shots++;
    return bullet;
}

McStatus mc_combat_apply_status(McCombatTarget *target, uint16_t status_id, float duration){
    if(target == NULL || status_id >= MC_CONTENT_REGISTRY_STATUS_COUNT || duration < 0.0f || !finite_state(duration)) return MC_INVALID_ARGUMENT;
    McCombatStatus *status = &target->statuses[status_id];
    if(duration > status->duration) status->duration = duration;
    status->active = status->duration > 0.0f;
    status->damage_multiplier = status->damage_multiplier == 0.0f ? 1.0f : status->damage_multiplier;
    status->speed_multiplier = status->speed_multiplier == 0.0f ? 1.0f : status->speed_multiplier;
    return MC_OK;
}

float mc_combat_status_multiplier(const McCombatTarget *target, uint16_t status_id){
    if(target == NULL || status_id >= MC_CONTENT_REGISTRY_STATUS_COUNT || !target->statuses[status_id].active) return 1.0f;
    return target->statuses[status_id].damage_multiplier == 0.0f ? 1.0f : target->statuses[status_id].damage_multiplier;
}

McStatus mc_combat_damage(McCombatWorld *world, McEntityId source, McEntityId target_id,
                          float amount, McCombatDamageKind kind, uint16_t bullet_type, float x, float y){
    if(world == NULL || amount < 0.0f || !finite_state(amount)) return MC_INVALID_ARGUMENT;
    McCombatTarget *target = mc_combat_find_target(world, target_id);
    if(target == NULL) return MC_NOT_FOUND;
    float mitigated = fmaxf(amount - target->armor, amount * 0.1f);
    target->health = fmaxf(target->health - mitigated, 0.0f);
    world->damage_events++;
    if(mitigated > 0.0f) world->hits++;
    McStatus status = push_event(world, world->tick, kind, source, target_id, bullet_type, 0, mitigated, x, y);
    if(status != MC_OK) return status;
    if(target->health <= 0.0f){
        target->active = false;
        world->kills++;
    }
    return MC_OK;
}

static float distance_sq(float x1, float y1, float x2, float y2){
    float x = x1 - x2, y = y1 - y2;
    return x * x + y * y;
}

static McCombatTarget *nearest_target(McCombatWorld *world, uint8_t team, float x, float y, float range){
    McCombatTarget *best = NULL;
    float best_distance = range * range;
    for(size_t i = 0; i < world->target_count; i++){
        McCombatTarget *target = &world->targets[i];
        if(!target->active || target->team == team) continue;
        float distance = distance_sq(x, y, target->x, target->y);
        if(distance <= best_distance){
            best_distance = distance;
            best = target;
        }
    }
    return best;
}

static float cross_distance(float ax, float ay, float bx, float by, float px, float py){
    float dx = bx - ax, dy = by - ay;
    float length_sq = dx * dx + dy * dy;
    if(length_sq <= 0.000001f) return sqrtf(distance_sq(ax, ay, px, py));
    float t = ((px - ax) * dx + (py - ay) * dy) / length_sq;
    t = fminf(fmaxf(t, 0.0f), 1.0f);
    return sqrtf(distance_sq(ax + t * dx, ay + t * dy, px, py));
}

static bool already_collided(const McCombatBullet *bullet, McEntityId id){
    for(size_t i = 0; i < bullet->collision_count; i++) if(bullet->collided[i] == id) return true;
    return false;
}

static void update_statuses(McCombatWorld *world, float delta){
    for(size_t i = 0; i < world->target_count; i++){
        McCombatTarget *target = &world->targets[i];
        if(!target->active) continue;
        for(size_t j = 0; j < MC_CONTENT_REGISTRY_STATUS_COUNT; j++){
            McCombatStatus *status = &target->statuses[j];
            if(status->duration > 0.0f){
                status->duration = fmaxf(status->duration - delta, 0.0f);
                status->active = status->duration > 0.0f;
            }
        }
    }
}

static McStatus fire_turrets(McCombatWorld *world, float delta){
    for(size_t i = 0; i < world->turret_count; i++){
        McCombatTurret *turret = &world->turrets[i];
        if(!turret->active || !turret->enabled || turret->power_efficiency <= 0.0f) continue;
        McCombatTarget *target = nearest_target(world, turret->team, turret->x, turret->y, turret->range);
        turret->target = target == NULL ? MC_ENTITY_NONE : target->id;
        if(target == NULL) continue;
        turret->target_rotation = atan2f(target->y - turret->y, target->x - turret->x);
        turret->rotation = turret->target_rotation;
        turret->reload += delta * turret->power_efficiency * (turret->coolant_multiplier <= 0.0f ? 1.0f : turret->coolant_multiplier);
        float interval = turret->reload_time <= 0.0f ? 1.0f : turret->reload_time;
        while(turret->reload >= interval){
            if(turret->ammo == 0) break;
            turret->reload -= interval;
            if(turret->ammo != UINT32_MAX) turret->ammo--;
            if(mc_combat_fire(world, turret->id, turret->id, turret->team, turret->bullet_type,
                              turret->x, turret->y, turret->rotation, 1.0f) == NULL) return MC_OUT_OF_MEMORY;
        }
    }
    return MC_OK;
}

static McStatus update_bullets(McCombatWorld *world, float delta){
    for(size_t i = 0; i < world->bullet_count; i++){
        McCombatBullet *bullet = &world->bullets[i];
        if(!bullet->active) continue;
        const McBulletDefinition *definition = mc_bullet_definition(bullet->type_id);
        if(definition == NULL){ bullet->active = false; continue; }
        bullet->last_x = bullet->x;
        bullet->last_y = bullet->y;
        bullet->x += bullet->velocity_x * delta;
        bullet->y += bullet->velocity_y * delta;
        float drag = fmaxf(1.0f - definition->drag * delta, 0.0f);
        bullet->velocity_x *= drag;
        bullet->velocity_y *= drag;
        if(definition->acceleration != 0.0f){
            float speed = hypotf(bullet->velocity_x, bullet->velocity_y) + definition->acceleration * delta;
            float angle = atan2f(bullet->velocity_y, bullet->velocity_x);
            bullet->velocity_x = cosf(angle) * speed;
            bullet->velocity_y = sinf(angle) * speed;
        }
        bullet->time += delta;
        if(bullet->time >= bullet->lifetime){ bullet->active = false; continue; }

        for(size_t j = 0; j < world->target_count && bullet->active; j++){
            McCombatTarget *target = &world->targets[j];
            if(!target->active || target->team == bullet->team || target->id == bullet->owner || already_collided(bullet, target->id)) continue;
            float hit_distance = target->radius + bullet->hit_size;
            if(cross_distance(bullet->last_x, bullet->last_y, bullet->x, bullet->y, target->x, target->y) > hit_distance) continue;
            if(bullet->collision_count < MC_COMBAT_MAX_COLLISIONS) bullet->collided[bullet->collision_count++] = target->id;
            bullet->hit = true;
            McStatus status = mc_combat_damage(world, bullet->shooter, target->id, bullet->damage,
                                                definition->lightning != 0 ? MC_COMBAT_DAMAGE_LIGHTNING : MC_COMBAT_DAMAGE_DIRECT,
                                                bullet->type_id, target->x, target->y);
            if(status != MC_OK && status != MC_NOT_FOUND) return status;
            if(definition->status_id < MC_CONTENT_REGISTRY_STATUS_COUNT && definition->status_duration > 0.0f)
                (void)mc_combat_apply_status(target, definition->status_id, definition->status_duration);
            if(definition->splash_radius > 0.0f){
                for(size_t k = 0; k < world->target_count; k++){
                    McCombatTarget *splash = &world->targets[k];
                    if(!splash->active || splash->team == bullet->team || splash->id == target->id) continue;
                    float distance = sqrtf(distance_sq(splash->x, splash->y, target->x, target->y));
                    if(distance <= definition->splash_radius){
                        float scale = 1.0f - distance / definition->splash_radius;
                        (void)mc_combat_damage(world, bullet->shooter, splash->id,
                                                definition->splash_damage * scale, MC_COMBAT_DAMAGE_SPLASH,
                                                bullet->type_id, splash->x, splash->y);
                    }
                }
            }
            if(definition->lightning > 0){
                uint16_t chained = 0;
                for(size_t k = 0; k < world->target_count && chained < definition->lightning; k++){
                    McCombatTarget *chain = &world->targets[k];
                    if(!chain->active || chain->team == bullet->team || chain->id == target->id) continue;
                    if(distance_sq(chain->x, chain->y, target->x, target->y) <= 64.0f * 64.0f){
                        (void)mc_combat_damage(world, bullet->shooter, chain->id, bullet->damage,
                                                MC_COMBAT_DAMAGE_LIGHTNING, bullet->type_id, chain->x, chain->y);
                        chained++;
                    }
                }
            }
            if(!definition->pierce || bullet->pierce_left <= 1){
                bullet->active = false;
            }else{
                bullet->pierce_left--;
            }
        }
    }
    compact_bullets(world);
    compact_targets(world);
    return MC_OK;
}

McStatus mc_combat_step(McCombatWorld *world, float delta){
    if(world == NULL || delta <= 0.0f || !finite_state(delta) || delta > 10.0f) return MC_INVALID_ARGUMENT;
    if(world->tick == UINT64_MAX) return MC_CAPACITY_EXCEEDED;
    world->tick++;
    update_statuses(world, delta);
    McStatus status = fire_turrets(world, delta);
    if(status != MC_OK) return status;
    return update_bullets(world, delta);
}

McStatus mc_combat_step_ticks(McCombatWorld *world, uint32_t ticks){
    if(world == NULL) return MC_INVALID_ARGUMENT;
    for(uint32_t i = 0; i < ticks; i++){
        McStatus status = mc_combat_step(world, 1.0f / MC_COMBAT_TICKS_PER_SECOND);
        if(status != MC_OK) return status;
    }
    return MC_OK;
}

size_t mc_combat_event_count_since(const McCombatWorld *world, uint64_t sequence){
    if(world == NULL) return 0;
    size_t count = 0;
    for(size_t i = 0; i < world->event_count; i++) if(world->events[i].sequence > sequence) count++;
    return count;
}

const McCombatEvent *mc_combat_event_at(const McCombatWorld *world, size_t index){
    return world != NULL && index < world->event_count ? &world->events[index] : NULL;
}

static uint64_t hash_bytes(uint64_t hash, const void *data, size_t size){
    const uint8_t *bytes = data;
    for(size_t i = 0; i < size; i++){
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_target(uint64_t hash, const McCombatTarget *target){
    hash = hash_bytes(hash, &target->id, sizeof(target->id));
    hash = hash_bytes(hash, &target->kind, sizeof(target->kind));
    hash = hash_bytes(hash, &target->team, sizeof(target->team));
    hash = hash_bytes(hash, &target->x, sizeof(target->x));
    hash = hash_bytes(hash, &target->y, sizeof(target->y));
    hash = hash_bytes(hash, &target->radius, sizeof(target->radius));
    hash = hash_bytes(hash, &target->health, sizeof(target->health));
    hash = hash_bytes(hash, &target->max_health, sizeof(target->max_health));
    hash = hash_bytes(hash, &target->armor, sizeof(target->armor));
    hash = hash_bytes(hash, &target->flags, sizeof(target->flags));
    hash = hash_bytes(hash, &target->active, sizeof(target->active));
    for(size_t i = 0; i < MC_CONTENT_REGISTRY_STATUS_COUNT; i++){
        hash = hash_bytes(hash, &target->statuses[i].duration, sizeof(target->statuses[i].duration));
        hash = hash_bytes(hash, &target->statuses[i].damage_multiplier, sizeof(target->statuses[i].damage_multiplier));
        hash = hash_bytes(hash, &target->statuses[i].speed_multiplier, sizeof(target->statuses[i].speed_multiplier));
        hash = hash_bytes(hash, &target->statuses[i].active, sizeof(target->statuses[i].active));
    }
    return hash;
}

static uint64_t hash_bullet(uint64_t hash, const McCombatBullet *bullet){
    hash = hash_bytes(hash, &bullet->id, sizeof(bullet->id));
    hash = hash_bytes(hash, &bullet->owner, sizeof(bullet->owner));
    hash = hash_bytes(hash, &bullet->shooter, sizeof(bullet->shooter));
    hash = hash_bytes(hash, &bullet->type_id, sizeof(bullet->type_id));
    hash = hash_bytes(hash, &bullet->team, sizeof(bullet->team));
    hash = hash_bytes(hash, &bullet->x, sizeof(bullet->x));
    hash = hash_bytes(hash, &bullet->y, sizeof(bullet->y));
    hash = hash_bytes(hash, &bullet->last_x, sizeof(bullet->last_x));
    hash = hash_bytes(hash, &bullet->last_y, sizeof(bullet->last_y));
    hash = hash_bytes(hash, &bullet->velocity_x, sizeof(bullet->velocity_x));
    hash = hash_bytes(hash, &bullet->velocity_y, sizeof(bullet->velocity_y));
    hash = hash_bytes(hash, &bullet->time, sizeof(bullet->time));
    hash = hash_bytes(hash, &bullet->lifetime, sizeof(bullet->lifetime));
    hash = hash_bytes(hash, &bullet->damage, sizeof(bullet->damage));
    hash = hash_bytes(hash, &bullet->hit_size, sizeof(bullet->hit_size));
    hash = hash_bytes(hash, &bullet->pierce_left, sizeof(bullet->pierce_left));
    hash = hash_bytes(hash, &bullet->collision_count, sizeof(bullet->collision_count));
    hash = hash_bytes(hash, bullet->collided, sizeof(bullet->collided));
    hash = hash_bytes(hash, &bullet->flags, sizeof(bullet->flags));
    hash = hash_bytes(hash, &bullet->active, sizeof(bullet->active));
    return hash_bytes(hash, &bullet->hit, sizeof(bullet->hit));
}

static uint64_t hash_turret(uint64_t hash, const McCombatTurret *turret){
    hash = hash_bytes(hash, &turret->id, sizeof(turret->id));
    hash = hash_bytes(hash, &turret->team, sizeof(turret->team));
    hash = hash_bytes(hash, &turret->x, sizeof(turret->x));
    hash = hash_bytes(hash, &turret->y, sizeof(turret->y));
    hash = hash_bytes(hash, &turret->range, sizeof(turret->range));
    hash = hash_bytes(hash, &turret->rotation, sizeof(turret->rotation));
    hash = hash_bytes(hash, &turret->target_rotation, sizeof(turret->target_rotation));
    hash = hash_bytes(hash, &turret->reload, sizeof(turret->reload));
    hash = hash_bytes(hash, &turret->reload_time, sizeof(turret->reload_time));
    hash = hash_bytes(hash, &turret->power_efficiency, sizeof(turret->power_efficiency));
    hash = hash_bytes(hash, &turret->coolant_multiplier, sizeof(turret->coolant_multiplier));
    hash = hash_bytes(hash, &turret->bullet_type, sizeof(turret->bullet_type));
    hash = hash_bytes(hash, &turret->ammo, sizeof(turret->ammo));
    hash = hash_bytes(hash, &turret->ammo_capacity, sizeof(turret->ammo_capacity));
    hash = hash_bytes(hash, &turret->target, sizeof(turret->target));
    hash = hash_bytes(hash, &turret->active, sizeof(turret->active));
    hash = hash_bytes(hash, &turret->enabled, sizeof(turret->enabled));
    return hash_bytes(hash, &turret->continuous, sizeof(turret->continuous));
}

uint64_t mc_combat_state_hash(const McCombatWorld *world){
    if(world == NULL) return 0;
    uint64_t hash = UINT64_C(1469598103934665603);
    hash = hash_bytes(hash, &world->tick, sizeof(world->tick));
    for(size_t i = 0; i < world->target_count; i++) if(world->targets[i].active) hash = hash_target(hash, &world->targets[i]);
    for(size_t i = 0; i < world->bullet_count; i++) if(world->bullets[i].active) hash = hash_bullet(hash, &world->bullets[i]);
    for(size_t i = 0; i < world->turret_count; i++) if(world->turrets[i].active) hash = hash_turret(hash, &world->turrets[i]);
    return hash;
}

uint64_t mc_combat_hash(const McCombatWorld *world){
    if(world == NULL) return 0;
    uint64_t hash = mc_combat_state_hash(world);
    hash = hash_bytes(hash, &world->shots, sizeof(world->shots));
    hash = hash_bytes(hash, &world->hits, sizeof(world->hits));
    hash = hash_bytes(hash, &world->kills, sizeof(world->kills));
    hash = hash_bytes(hash, &world->damage_events, sizeof(world->damage_events));
    for(size_t i = 0; i < world->event_count; i++) hash = hash_bytes(hash, &world->events[i], sizeof(world->events[i]));
    return hash;
}

static McStatus write_target(McBuffer *out, const McCombatTarget *target){
    McStatus status = mc_buffer_write_u32_be(out, target->id);
    if(status == MC_OK) status = mc_buffer_write_u8(out, (uint8_t)target->kind);
    if(status == MC_OK) status = mc_buffer_write_u8(out, target->team);
    if(status == MC_OK) status = write_f32(out, target->x);
    if(status == MC_OK) status = write_f32(out, target->y);
    if(status == MC_OK) status = write_f32(out, target->radius);
    if(status == MC_OK) status = write_f32(out, target->health);
    if(status == MC_OK) status = write_f32(out, target->max_health);
    if(status == MC_OK) status = write_f32(out, target->armor);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, target->flags);
    if(status == MC_OK) status = write_bool(out, target->active);
    for(size_t i = 0; status == MC_OK && i < MC_CONTENT_REGISTRY_STATUS_COUNT; i++){
        status = write_f32(out, target->statuses[i].duration);
        if(status == MC_OK) status = write_f32(out, target->statuses[i].damage_multiplier);
        if(status == MC_OK) status = write_f32(out, target->statuses[i].speed_multiplier);
        if(status == MC_OK) status = write_bool(out, target->statuses[i].active);
    }
    return status;
}

static McStatus read_target(McBuffer *in, McCombatTarget *target){
    uint8_t kind = 0;
    McStatus status = mc_buffer_read_u32_be(in, &target->id);
    if(status == MC_OK) status = mc_buffer_read_u8(in, &kind);
    if(status == MC_OK && kind > MC_COMBAT_TARGET_BULLET) status = MC_FORMAT_ERROR;
    target->kind = (McCombatTargetKind)kind;
    if(status == MC_OK) status = mc_buffer_read_u8(in, &target->team);
    if(status == MC_OK) status = read_f32(in, &target->x);
    if(status == MC_OK) status = read_f32(in, &target->y);
    if(status == MC_OK) status = read_f32(in, &target->radius);
    if(status == MC_OK) status = read_f32(in, &target->health);
    if(status == MC_OK) status = read_f32(in, &target->max_health);
    if(status == MC_OK) status = read_f32(in, &target->armor);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &target->flags);
    if(status == MC_OK) status = read_bool(in, &target->active);
    for(size_t i = 0; status == MC_OK && i < MC_CONTENT_REGISTRY_STATUS_COUNT; i++){
        status = read_f32(in, &target->statuses[i].duration);
        if(status == MC_OK) status = read_f32(in, &target->statuses[i].damage_multiplier);
        if(status == MC_OK) status = read_f32(in, &target->statuses[i].speed_multiplier);
        if(status == MC_OK) status = read_bool(in, &target->statuses[i].active);
    }
    return status;
}

static McStatus write_bullet(McBuffer *out, const McCombatBullet *bullet){
    McStatus status = mc_buffer_write_u32_be(out, bullet->id);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, bullet->owner);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, bullet->shooter);
    if(status == MC_OK) status = mc_buffer_write_u16_be(out, bullet->type_id);
    if(status == MC_OK) status = mc_buffer_write_u8(out, bullet->team);
    const float values[] = {bullet->x, bullet->y, bullet->last_x, bullet->last_y, bullet->velocity_x, bullet->velocity_y, bullet->time, bullet->lifetime, bullet->damage, bullet->hit_size};
    for(size_t i = 0; status == MC_OK && i < sizeof(values) / sizeof(values[0]); i++) status = write_f32(out, values[i]);
    if(status == MC_OK) status = mc_buffer_write_u16_be(out, bullet->pierce_left);
    if(status == MC_OK) status = mc_buffer_write_u16_be(out, bullet->collision_count);
    for(size_t i = 0; status == MC_OK && i < MC_COMBAT_MAX_COLLISIONS; i++) status = mc_buffer_write_u32_be(out, bullet->collided[i]);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, bullet->flags);
    if(status == MC_OK) status = write_bool(out, bullet->active);
    if(status == MC_OK) status = write_bool(out, bullet->hit);
    return status;
}

static McStatus read_bullet(McBuffer *in, McCombatBullet *bullet){
    McStatus status = mc_buffer_read_u32_be(in, &bullet->id);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &bullet->owner);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &bullet->shooter);
    if(status == MC_OK) status = mc_buffer_read_u16_be(in, &bullet->type_id);
    if(status == MC_OK) status = mc_buffer_read_u8(in, &bullet->team);
    float *values[] = {&bullet->x, &bullet->y, &bullet->last_x, &bullet->last_y, &bullet->velocity_x, &bullet->velocity_y, &bullet->time, &bullet->lifetime, &bullet->damage, &bullet->hit_size};
    for(size_t i = 0; status == MC_OK && i < sizeof(values) / sizeof(values[0]); i++) status = read_f32(in, values[i]);
    if(status == MC_OK) status = mc_buffer_read_u16_be(in, &bullet->pierce_left);
    if(status == MC_OK) status = mc_buffer_read_u16_be(in, &bullet->collision_count);
    if(bullet->collision_count > MC_COMBAT_MAX_COLLISIONS) return MC_FORMAT_ERROR;
    for(size_t i = 0; status == MC_OK && i < MC_COMBAT_MAX_COLLISIONS; i++) status = mc_buffer_read_u32_be(in, &bullet->collided[i]);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &bullet->flags);
    if(status == MC_OK) status = read_bool(in, &bullet->active);
    if(status == MC_OK) status = read_bool(in, &bullet->hit);
    return status;
}

static McStatus write_turret(McBuffer *out, const McCombatTurret *turret){
    McStatus status = mc_buffer_write_u32_be(out, turret->id);
    if(status == MC_OK) status = mc_buffer_write_u8(out, turret->team);
    const float values[] = {turret->x, turret->y, turret->range, turret->rotation, turret->target_rotation, turret->reload, turret->reload_time, turret->power_efficiency, turret->coolant_multiplier};
    for(size_t i = 0; status == MC_OK && i < sizeof(values) / sizeof(values[0]); i++) status = write_f32(out, values[i]);
    if(status == MC_OK) status = mc_buffer_write_u16_be(out, turret->bullet_type);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, turret->ammo);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, turret->ammo_capacity);
    if(status == MC_OK) status = mc_buffer_write_u32_be(out, turret->target);
    if(status == MC_OK) status = write_bool(out, turret->active);
    if(status == MC_OK) status = write_bool(out, turret->enabled);
    if(status == MC_OK) status = write_bool(out, turret->continuous);
    return status;
}

static McStatus read_turret(McBuffer *in, McCombatTurret *turret){
    McStatus status = mc_buffer_read_u32_be(in, &turret->id);
    if(status == MC_OK) status = mc_buffer_read_u8(in, &turret->team);
    float *values[] = {&turret->x, &turret->y, &turret->range, &turret->rotation, &turret->target_rotation, &turret->reload, &turret->reload_time, &turret->power_efficiency, &turret->coolant_multiplier};
    for(size_t i = 0; status == MC_OK && i < sizeof(values) / sizeof(values[0]); i++) status = read_f32(in, values[i]);
    if(status == MC_OK) status = mc_buffer_read_u16_be(in, &turret->bullet_type);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &turret->ammo);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &turret->ammo_capacity);
    if(status == MC_OK) status = mc_buffer_read_u32_be(in, &turret->target);
    if(status == MC_OK) status = read_bool(in, &turret->active);
    if(status == MC_OK) status = read_bool(in, &turret->enabled);
    if(status == MC_OK) status = read_bool(in, &turret->continuous);
    return status;
}

McStatus mc_combat_write(const McCombatWorld *world, McBuffer *output){
    if(world == NULL || output == NULL) return MC_INVALID_ARGUMENT;
    mc_buffer_clear(output);
    McStatus status = mc_buffer_write_bytes(output, (uint8_t[]){COMBAT_MAGIC_0, COMBAT_MAGIC_1, COMBAT_MAGIC_2, COMBAT_MAGIC_3}, 4);
    if(status == MC_OK) status = mc_buffer_write_u16_be(output, COMBAT_VERSION);
    if(status == MC_OK) status = write_u64(output, world->tick);
    if(status == MC_OK) status = mc_buffer_write_u32_be(output, world->next_bullet_id);
    if(status == MC_OK) status = write_count(output, world->target_count);
    for(size_t i = 0; status == MC_OK && i < world->target_count; i++) status = write_target(output, &world->targets[i]);
    if(status == MC_OK) status = write_count(output, world->bullet_count);
    for(size_t i = 0; status == MC_OK && i < world->bullet_count; i++) status = write_bullet(output, &world->bullets[i]);
    if(status == MC_OK) status = write_count(output, world->turret_count);
    for(size_t i = 0; status == MC_OK && i < world->turret_count; i++) status = write_turret(output, &world->turrets[i]);
    if(status == MC_OK) status = write_u64(output, world->shots);
    if(status == MC_OK) status = write_u64(output, world->hits);
    if(status == MC_OK) status = write_u64(output, world->kills);
    if(status == MC_OK) status = write_u64(output, world->damage_events);
    return status;
}

McStatus mc_combat_read(const uint8_t *data, size_t size, McCombatWorld *world){
    if(data == NULL || world == NULL || size < 4 + 2 + 8 + 4) return MC_INVALID_ARGUMENT;
    McBuffer input = {.data = (uint8_t *)data, .size = size, .capacity = size, .position = 0};
    uint8_t magic[4] = {0};
    uint16_t version = 0;
    if(mc_buffer_read_bytes(&input, magic, 4) != MC_OK || memcmp(magic, (uint8_t[]){COMBAT_MAGIC_0, COMBAT_MAGIC_1, COMBAT_MAGIC_2, COMBAT_MAGIC_3}, 4) != 0 ||
       mc_buffer_read_u16_be(&input, &version) != MC_OK || version != COMBAT_VERSION) return MC_FORMAT_ERROR;
    McCombatWorld decoded;
    mc_combat_init(&decoded, world->max_events);
    McStatus status = read_u64(&input, &decoded.tick);
    if(status == MC_OK) status = mc_buffer_read_u32_be(&input, &decoded.next_bullet_id);
    size_t count = 0;
    if(status == MC_OK) status = read_count(&input, &count);
    if(status == MC_OK && reserve_targets(&decoded, count) != MC_OK) status = MC_OUT_OF_MEMORY;
    for(size_t i = 0; status == MC_OK && i < count; i++){
        status = read_target(&input, &decoded.targets[decoded.target_count++]);
    }
    if(status == MC_OK) status = read_count(&input, &count);
    if(status == MC_OK && reserve_bullets(&decoded, count) != MC_OK) status = MC_OUT_OF_MEMORY;
    for(size_t i = 0; status == MC_OK && i < count; i++) status = read_bullet(&input, &decoded.bullets[decoded.bullet_count++]);
    if(status == MC_OK) status = read_count(&input, &count);
    if(status == MC_OK && reserve_turrets(&decoded, count) != MC_OK) status = MC_OUT_OF_MEMORY;
    for(size_t i = 0; status == MC_OK && i < count; i++) status = read_turret(&input, &decoded.turrets[decoded.turret_count++]);
    if(status == MC_OK) status = read_u64(&input, &decoded.shots);
    if(status == MC_OK) status = read_u64(&input, &decoded.hits);
    if(status == MC_OK) status = read_u64(&input, &decoded.kills);
    if(status == MC_OK) status = read_u64(&input, &decoded.damage_events);
    if(status == MC_OK && input.position != input.size) status = MC_FORMAT_ERROR;
    if(status != MC_OK){
        mc_combat_destroy(&decoded);
        return status;
    }
    mc_combat_destroy(world);
    *world = decoded;
    return MC_OK;
}
