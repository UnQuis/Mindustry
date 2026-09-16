#ifndef MINDUSTRY_NATIVE_COMBAT_H
#define MINDUSTRY_NATIVE_COMBAT_H

#include "mindustry_content_registry.h"

#ifdef __cplusplus
#error "The native Mindustry port is C, not C++."
#endif

#define MC_COMBAT_MAX_COLLISIONS 32u
#define MC_COMBAT_DEFAULT_BULLET_TYPES 6u
#define MC_COMBAT_TICKS_PER_SECOND 60.0f

typedef enum McCombatTargetKind{
    MC_COMBAT_TARGET_UNIT = 0,
    MC_COMBAT_TARGET_BUILDING,
    MC_COMBAT_TARGET_CORE,
    MC_COMBAT_TARGET_BULLET
} McCombatTargetKind;

typedef enum McCombatDamageKind{
    MC_COMBAT_DAMAGE_DIRECT = 0,
    MC_COMBAT_DAMAGE_SPLASH,
    MC_COMBAT_DAMAGE_LIGHTNING,
    MC_COMBAT_DAMAGE_LASER,
    MC_COMBAT_DAMAGE_STATUS
} McCombatDamageKind;

typedef struct McBulletDefinition{
    uint16_t id;
    const char *name;
    float speed;
    float damage;
    float lifetime;
    float drag;
    float acceleration;
    float splash_damage;
    float splash_radius;
    float knockback;
    float homing_power;
    float homing_range;
    uint16_t pierce_cap;
    uint16_t lightning;
    uint16_t status_id;
    float status_duration;
    bool collides_tiles;
    bool collides_ground;
    bool collides_air;
    bool pierce;
    bool incend;
    bool absorbable;
} McBulletDefinition;

typedef struct McCombatStatus{
    float duration;
    float damage_multiplier;
    float speed_multiplier;
    bool active;
} McCombatStatus;

typedef struct McCombatTarget{
    McEntityId id;
    McCombatTargetKind kind;
    uint8_t team;
    float x;
    float y;
    float radius;
    float health;
    float max_health;
    float armor;
    uint32_t flags;
    bool active;
    McCombatStatus statuses[MC_CONTENT_REGISTRY_STATUS_COUNT];
} McCombatTarget;

typedef struct McCombatBullet{
    McEntityId id;
    McEntityId owner;
    McEntityId shooter;
    uint16_t type_id;
    uint8_t team;
    float x;
    float y;
    float last_x;
    float last_y;
    float velocity_x;
    float velocity_y;
    float time;
    float lifetime;
    float damage;
    float hit_size;
    uint16_t pierce_left;
    uint16_t collision_count;
    McEntityId collided[MC_COMBAT_MAX_COLLISIONS];
    uint32_t flags;
    bool active;
    bool hit;
} McCombatBullet;

typedef struct McCombatTurret{
    McEntityId id;
    uint8_t team;
    float x;
    float y;
    float range;
    float rotation;
    float target_rotation;
    float reload;
    float reload_time;
    float power_efficiency;
    float coolant_multiplier;
    uint16_t bullet_type;
    uint32_t ammo;
    uint32_t ammo_capacity;
    McEntityId target;
    bool active;
    bool enabled;
    bool continuous;
} McCombatTurret;

typedef struct McCombatEvent{
    uint64_t sequence;
    uint64_t tick;
    McCombatDamageKind kind;
    McEntityId source;
    McEntityId target;
    uint16_t bullet_type;
    uint8_t status_id;
    float amount;
    float x;
    float y;
} McCombatEvent;

typedef struct McCombatWorld{
    McCombatTarget *targets;
    size_t target_count;
    size_t target_capacity;
    McCombatBullet *bullets;
    size_t bullet_count;
    size_t bullet_capacity;
    McCombatTurret *turrets;
    size_t turret_count;
    size_t turret_capacity;
    McCombatEvent *events;
    size_t event_count;
    size_t event_capacity;
    size_t max_events;
    uint64_t tick;
    McEntityId next_bullet_id;
    uint64_t next_event;
    uint64_t shots;
    uint64_t hits;
    uint64_t kills;
    uint64_t damage_events;
} McCombatWorld;

MC_API const McBulletDefinition *mc_bullet_definition(uint16_t id);
MC_API const McBulletDefinition *mc_bullet_definition_by_name(const char *name);
MC_API size_t mc_bullet_definition_count(void);
MC_API McStatus mc_bullet_definition_validate(void);

MC_API void mc_combat_init(McCombatWorld *world, size_t max_events);
MC_API void mc_combat_destroy(McCombatWorld *world);
MC_API McStatus mc_combat_clear(McCombatWorld *world);
MC_API McStatus mc_combat_set_event_limit(McCombatWorld *world, size_t max_events);
MC_API McCombatTarget *mc_combat_add_target(McCombatWorld *world, McEntityId id, McCombatTargetKind kind,
                                             uint8_t team, float x, float y, float radius, float health,
                                             float armor);
MC_API McStatus mc_combat_remove_target(McCombatWorld *world, McEntityId id);
MC_API McCombatTarget *mc_combat_find_target(McCombatWorld *world, McEntityId id);
MC_API const McCombatTarget *mc_combat_find_target_const(const McCombatWorld *world, McEntityId id);
MC_API McCombatTurret *mc_combat_add_turret(McCombatWorld *world, McEntityId id, uint8_t team,
                                             float x, float y, uint16_t bullet_type);
MC_API McStatus mc_combat_remove_turret(McCombatWorld *world, McEntityId id);
MC_API McCombatTurret *mc_combat_find_turret(McCombatWorld *world, McEntityId id);
MC_API McStatus mc_combat_set_turret(McCombatTurret *turret, float range, float reload_time,
                                     uint32_t ammo, float power_efficiency, float coolant_multiplier);
MC_API McCombatBullet *mc_combat_fire(McCombatWorld *world, McEntityId owner, McEntityId shooter,
                                      uint8_t team, uint16_t bullet_type, float x, float y, float angle,
                                      float damage_scale);
MC_API McStatus mc_combat_step(McCombatWorld *world, float delta);
MC_API McStatus mc_combat_step_ticks(McCombatWorld *world, uint32_t ticks);
MC_API McStatus mc_combat_damage(McCombatWorld *world, McEntityId source, McEntityId target,
                                 float amount, McCombatDamageKind kind, uint16_t bullet_type,
                                 float x, float y);
MC_API McStatus mc_combat_apply_status(McCombatTarget *target, uint16_t status_id, float duration);
MC_API float mc_combat_status_multiplier(const McCombatTarget *target, uint16_t status_id);
MC_API size_t mc_combat_event_count_since(const McCombatWorld *world, uint64_t sequence);
MC_API const McCombatEvent *mc_combat_event_at(const McCombatWorld *world, size_t index);
MC_API uint64_t mc_combat_hash(const McCombatWorld *world);
MC_API uint64_t mc_combat_state_hash(const McCombatWorld *world);

/* Java-like fixed-width bullet/projectile state codec for native replays. */
MC_API McStatus mc_combat_write(const McCombatWorld *world, McBuffer *output);
MC_API McStatus mc_combat_read(const uint8_t *data, size_t size, McCombatWorld *world);

#endif
