#include "api.h"
#include "combat.h"
#include "combat_profiles.h"
#include "area_flags.h"
#include "activity_mechanics.h"
#include "activity_schemas.h"
#include "activity_spawns.h"
#include "activity_states.h"
#include "rng.h"
#include "skills.h"
#include "config.h"
#include "collision.h"
#include "dialogue.h"
#include "drops.h"
#include "events.h"
#include "encounter.h"
#include "game_data.h"
#include "items.h"
#include "interaction.h"
#include "monster_mechanics.h"
#include "npc.h"
#include "normalization.h"
#include "objects.h"
#include "player_actions.h"
#include "player_command.h"
#include "prayer.h"
#include "quests.h"
#include "slayer.h"
#include "spells.h"
#include "shops.h"
#include "traversal.h"
#include "varbits.h"
#include "world_state.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static void copy_world_path(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, cap, "%s", src);
}

static void init_player_defaults(RcPlayer *p) {
    // Varrock centre start for legacy-seeded worlds. RL configs
    // typically override player position via their own init step.
    p->x = 3213;
    p->y = 3428;
    p->plane = 0;
    p->prev_x = p->x;
    p->prev_y = p->y;
    p->attack_target = -1;
    p->attack_target_def_id = -1;
    p->interact_type = RC_INTERACT_NONE;
    p->interact_target = -1;
    p->interact_option = -1;
    p->storage_target = -1;
    p->storage_option = -1;
    p->skill_target_x = -1;
    p->skill_target_y = -1;
    p->pending_traversal_x = -1;
    p->pending_traversal_y = -1;
    p->pending_traversal_plane = -1;
    rc_interaction_clear(p);
    p->combat_style = COMBAT_MELEE_CRUSH;
    p->attack_style_idx = 0;
    p->attack_stance = RC_ATTACK_STANCE_ACCURATE;
    p->combat_xp_mask = RC_COMBAT_XP_ATTACK;
    p->special_energy = 10000;
    p->current_spellbook = RC_SPELL_BOOK_STANDARD;
    p->selected_spell = -1;
    p->manual_spell_cast = -1;
    p->autocast_spell = -1;
    p->defensive_autocast = false;
    for (int i = 0; i < 4; i++) {
        p->rune_pouch[i].item_id = -1;
        p->rune_pouch[i].quantity = 0;
    }
    p->last_hit = -1;
    p->facing_entity = -1;
    p->facing_x = -1;
    p->facing_y = -1;
    p->slayer_master_idx = -1;
    p->slayer_task_idx = -1;
    p->run_energy = 10000;
    p->auto_retaliate = true;
    rc_combat_init_player_state(p);

    // Default stats: level 1 everything, 10 HP.
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 1;
        p->skills.boosted_level[i] = 1;
        p->skills.xp[i] = 0;
    }
    p->skills.base_level[SKILL_HITPOINTS] = 10;
    p->skills.boosted_level[SKILL_HITPOINTS] = 10;
    p->skills.xp[SKILL_HITPOINTS] = 1154;   // XP for level 10
    p->current_hp = 100;                    // tenths-of-HP
    p->max_hp = 100;

    // Empty inventory + equipment.
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
        p->inventory[i].item_id = -1;
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        p->bank[i].item_id = -1;
        p->bank_tab[i] = 0;
    }
    for (int i = 0; i < RC_EQUIP_COUNT; i++) {
        p->equipment[i].item_id = -1;
    }
}

static int init_world_state(RcWorld *world) {
    if (!world || !world->game_data || !world->npcs
            || world->npc_capacity <= 0) {
        return 0;
    }
    world->rng_state = world->initial_seed;
    world->tick = 0;
    world->next_ground_item_uid = 0;
    world->next_npc_uid = 0;
    world->player_commands.next_sequence = 0;
    world->player_commands.last_result = RC_COMMAND_RESULT_NONE;
    rc_events_init(&world->events);
    if ((world->enabled & RC_SUB_SLAYER) && rc_slayer_init(world) != 0)
        return 0;
    if (world->enabled & RC_SUB_ENCOUNTER) {
        if (rc_encounter_init(world) != 0) return 0;
        int spec_count = 0;
        const RcEncounterSpec *specs =
            rc_game_data_encounter_specs(world->game_data, &spec_count);
        for (int i = 0; specs && i < spec_count; i++) {
            if (rc_encounter_register(world, &specs[i]) < 0) return 0;
        }
    }
    init_player_defaults(&world->player);
    return 1;
}

RcWorld *rc_world_create_config(const RcWorldConfig *cfg) {
    if (!cfg) return NULL;

    char config_error[256];
    if (!rc_world_config_validate(cfg, config_error, sizeof(config_error))) {
        fprintf(stderr, "rc-core: %s\n", config_error);
        return NULL;
    }

    RcGameDataLoadReport data_report;
    RcGameData *data = rc_game_data_load(cfg, &data_report);
    if (!data) {
        fprintf(stderr, "rc-core: %s\n", data_report.message);
        return NULL;
    }

    RcWorld *world = rc_world_create_with_data(data, cfg);
    rc_game_data_release(data);
    return world;
}

RcWorld *rc_world_create_with_data(RcGameData *data,
                                   const RcWorldConfig *cfg) {
    if (!data || !cfg) return NULL;
    char config_error[256];
    if (!rc_world_config_validate(cfg, config_error, sizeof(config_error))) {
        fprintf(stderr, "rc-core: %s\n", config_error);
        return NULL;
    }
    uint32_t data_subsystems = rc_game_data_subsystems(data);
    if ((cfg->subsystems & ~data_subsystems) != 0) {
        fprintf(stderr,
                "rc-core: RcGameData missing requested subsystem bits 0x%08x\n",
                cfg->subsystems & ~data_subsystems);
        return NULL;
    }

    // calloc is fine here — world_create is not on the tick path
    // (see rc-core/README.md §10).
    RcWorld *world = calloc(1, sizeof(RcWorld));
    if (!world) return NULL;
    world->npcs = calloc((size_t)cfg->npc_capacity, sizeof(*world->npcs));
    if (!world->npcs) {
        free(world);
        return NULL;
    }
    world->npc_capacity = cfg->npc_capacity;
    world->game_data = data;
    rc_game_data_retain(data);

    world->initial_seed = cfg->seed ? cfg->seed : RC_DEFAULT_SEED;
    world->enabled = cfg->subsystems;
    world->streaming = cfg->streaming;
    rc_world_streaming_config_sanitize(&world->streaming);
    copy_world_path(world->npc_spawns_path, sizeof(world->npc_spawns_path),
                    cfg->spawns_path);

    if (!init_world_state(world)) {
        rc_world_destroy(world);
        return NULL;
    }
    // Full world spawns stay explicit: callers load a world slice with
    // rc_load_npc_spawns_rect/near after choosing the active area.

    return world;
}

// Legacy create — used by viewer + tests that haven't been updated
// to pass a full config. Defaults to the full-game preset.
RcWorld *rc_world_create(uint32_t seed) {
    RcWorldConfig cfg = rc_preset_full_game();
    cfg.seed = seed;
    return rc_world_create_config(&cfg);
}

void rc_world_destroy(RcWorld *world) {
    if (!world) return;
    rc_world_state_destroy(world);
    rc_game_data_release(world->game_data);
    free(world->npcs);
    free(world);
}

int rc_world_reset(RcWorld *world) {
    if (!world || !world->game_data || !world->npcs) return 0;

    RcGameData *game_data = world->game_data;
    RcNpc *npcs = world->npcs;
    int npc_capacity = world->npc_capacity;
    int next_npc_uid = world->next_npc_uid;
    uint32_t initial_seed = world->initial_seed;
    uint32_t enabled = world->enabled;
    RcWorldStreamingConfig streaming = world->streaming;
    char spawns_path[sizeof(world->npc_spawns_path)];
    memcpy(spawns_path, world->npc_spawns_path, sizeof(spawns_path));

    rc_world_state_destroy(world);
    memset(npcs, 0, (size_t)npc_capacity * sizeof(*npcs));
    memset(world, 0, sizeof(*world));
    world->game_data = game_data;
    world->npcs = npcs;
    world->npc_capacity = npc_capacity;
    world->initial_seed = initial_seed;
    world->enabled = enabled;
    world->streaming = streaming;
    memcpy(world->npc_spawns_path, spawns_path, sizeof(spawns_path));
    if (!init_world_state(world)) return 0;
    world->next_npc_uid = next_npc_uid;
    return 1;
}

const RcGameData *rc_world_get_game_data(const RcWorld *world) {
    return world ? world->game_data : NULL;
}

const RcPlayer *rc_get_player(const RcWorld *world) {
    return world ? &world->player : NULL;
}

RcTick rc_world_get_tick(const RcWorld *world) {
    return world ? world->tick : 0;
}

RcPlayerCommandResult rc_player_last_command_result(const RcWorld *world,
                                                     uint64_t *sequence) {
    if (sequence) {
        *sequence = world ? world->player_commands.last_sequence : 0;
    }
    return world ? world->player_commands.last_result
                 : RC_COMMAND_RESULT_REJECTED_INVALID;
}

int rc_player_pending_command_count(const RcWorld *world) {
    return world ? world->player_commands.count : 0;
}

int rc_world_relocate_player(RcWorld *world, int x, int y, int plane) {
    if (!world || x < 0 || y < 0 || plane < 0 || plane >= RC_MAX_PLANES) {
        return 0;
    }
    rc_player_cancel_action(world, RC_ACTION_CANCEL_RELOCATED);
    RcPlayer *player = &world->player;
    player->prev_x = player->x;
    player->prev_y = player->y;
    player->x = x;
    player->y = y;
    player->plane = plane;
    return 1;
}

int rc_world_respawn_player(RcWorld *world, int x, int y, int plane) {
    if (!world || !world->player.is_dead) return 0;
    if (!rc_world_relocate_player(world, x, y, plane)) return 0;
    world->player.current_hp = world->player.max_hp;
    world->player.is_dead = false;
    world->player.death_tick = 0;
    return 1;
}

const RcNpc *rc_get_npcs(const RcWorld *world, int *count) {
    if (count) *count = world ? world->npc_count : 0;
    return world ? world->npcs : NULL;
}

static int valid_active_area_request(const RcActiveAreaRequest *request) {
    return request && request->width > 0 && request->height > 0
        && request->min_plane >= 0 && request->max_plane < RC_MAX_PLANES
        && request->min_plane <= request->max_plane;
}

static void clear_active_npcs(RcWorld *world) {
    if (world->npc_count > 0) {
        memset(world->npcs, 0,
               (size_t)world->npc_count * sizeof(world->npcs[0]));
    }
    world->npc_count = 0;
}

static int active_npc_count(const RcWorld *world) {
    int count = 0;
    for (int i = 0; world && i < world->npc_count; i++)
        if (world->npcs[i].active)
            count++;
    return count;
}

static int active_ground_item_count(const RcWorld *world) {
    int count = 0;
    for (int i = 0; world && i < world->ground_item_count; i++)
        if (world->ground_items[i].active)
            count++;
    return count;
}

int rc_world_activate_area(RcWorld *world, const RcActiveAreaRequest *request,
                           RcActiveAreaStats *stats) {
    if (stats) memset(stats, 0, sizeof(*stats));
    if (!world || !valid_active_area_request(request))
        return -1;

    double area_load_started_ms = monotonic_ms();

    uint32_t flags = request->flags;
    if (flags == 0) {
        flags = RC_ACTIVE_AREA_LOAD_COLLISION
              | RC_ACTIVE_AREA_LOAD_NPCS
              | RC_ACTIVE_AREA_CLEAR_NPCS
              | RC_ACTIVE_AREA_LOAD_OBJECT_PLACEMENTS;
    }

    int min_x = request->origin_x;
    int min_y = request->origin_y;
    int max_x = request->origin_x + request->width - 1;
    int max_y = request->origin_y + request->height - 1;

    int collision_regions = world->map.region_count;
    RcCollisionLoadStats collision_stats;
    memset(&collision_stats, 0, sizeof(collision_stats));
    int pages_loaded = 0;
    double page_load_ms = 0.0;
    if (flags & RC_ACTIVE_AREA_LOAD_COLLISION) {
        if (rc_collision_set_cache_limit(
                world->streaming.max_cached_regions) < 0) {
            return -1;
        }
        double page_load_started_ms = monotonic_ms();
        collision_regions = rc_collision_populate_map_rect_stats(
            &world->map, min_x, min_y, max_x, max_y, &collision_stats);
        page_load_ms = monotonic_ms() - page_load_started_ms;
        if (collision_regions < 0)
            return -1;
        pages_loaded += (int)collision_stats.pages_loaded;
    }

    RcObjectPlacementLoadStats object_placement_stats;
    memset(&object_placement_stats, 0, sizeof(object_placement_stats));
    if (flags & RC_ACTIVE_AREA_LOAD_OBJECT_PLACEMENTS) {
        if (rc_object_placements_set_cache_limit(
                world->streaming.max_cached_regions) < 0) {
            return -1;
        }
        double page_load_started_ms = monotonic_ms();
        int object_pages = rc_object_placements_prefetch_rect(
            min_x, min_y, max_x, max_y, &object_placement_stats);
        page_load_ms += monotonic_ms() - page_load_started_ms;
        if (object_pages < 0) return -1;
        pages_loaded += object_placement_stats.pages_loaded;
    }

    int saved_npc_states = 0;
    int restored_npc_states = 0;
    int saved_ground_items = 0;
    int restored_ground_items = 0;
    if (flags & RC_ACTIVE_AREA_CLEAR_NPCS) {
        saved_npc_states = rc_world_state_save_npcs(world);
        if (saved_npc_states < 0) return -1;
        clear_active_npcs(world);
    }
    if (flags & RC_ACTIVE_AREA_CLEAR_STATIC_GROUND_ITEMS) {
        saved_ground_items = rc_world_state_save_ground_items(
            world, min_x, min_y, max_x, max_y,
            request->min_plane, request->max_plane);
        if (saved_ground_items < 0) return -1;
        rc_clear_static_ground_items(world);
    }

    RcNpcSpawnLoadStats npc_stats;
    memset(&npc_stats, 0, sizeof(npc_stats));
    int spawned = 0;
    if (flags & RC_ACTIVE_AREA_LOAD_NPCS) {
        const char *path = request->npc_spawns_path && request->npc_spawns_path[0]
                         ? request->npc_spawns_path : world->npc_spawns_path;
        if (path && path[0]) {
            uint32_t npc_load_flags = 0;
            if (flags & RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS)
                npc_load_flags |= RC_NPC_SPAWN_LOAD_INCLUDE_INSTANCE;
            double page_load_started_ms = monotonic_ms();
            spawned = rc_load_npc_spawns_rect_stats_flags(
                world, path, min_x, min_y, max_x, max_y,
                request->min_plane, request->max_plane, npc_load_flags,
                &npc_stats);
            page_load_ms += monotonic_ms() - page_load_started_ms;
            if (spawned < 0)
                return -1;
            pages_loaded += npc_stats.pages_loaded;
        }
        restored_npc_states = rc_world_state_restore_npcs(world);
        if (restored_npc_states < 0) return -1;
    }

    RcGroundItemSpawnLoadStats ground_item_stats;
    memset(&ground_item_stats, 0, sizeof(ground_item_stats));
    int spawned_ground_items = 0;
    if (flags & RC_ACTIVE_AREA_LOAD_STATIC_GROUND_ITEMS) {
        const char *path = request->ground_item_spawns_path;
        if (path && path[0]) {
            double page_load_started_ms = monotonic_ms();
            spawned_ground_items = rc_load_ground_item_spawns_rect_stats(
                world, path, min_x, min_y, max_x, max_y,
                request->min_plane, request->max_plane, &ground_item_stats);
            page_load_ms += monotonic_ms() - page_load_started_ms;
            if (spawned_ground_items < 0)
                return -1;
            pages_loaded += ground_item_stats.pages_loaded;
        }
        restored_ground_items = rc_world_state_restore_ground_items(
            world, min_x, min_y, max_x, max_y,
            request->min_plane, request->max_plane);
        if (restored_ground_items < 0) return -1;
    }

    uint32_t generation = world->active_area.generation + 1;
    world->active_area = (RcActiveArea){
        .active = true,
        .origin_x = request->origin_x,
        .origin_y = request->origin_y,
        .width = request->width,
        .height = request->height,
        .min_plane = request->min_plane,
        .max_plane = request->max_plane,
        .generation = generation ? generation : 1,
    };

    double area_load_ms = monotonic_ms() - area_load_started_ms;
    RcWorldStreamingTelemetry *telemetry = &world->streaming_telemetry;
    telemetry->active_area_load_count++;
    telemetry->active_area_load_ms = area_load_ms;
    telemetry->active_area_load_total_ms += area_load_ms;
    telemetry->backend_page_load_ms = page_load_ms;
    telemetry->backend_page_load_total_ms += page_load_ms;
    telemetry->backend_pages_loaded = pages_loaded;
    telemetry->active_npcs = active_npc_count(world);
    telemetry->active_ground_items = active_ground_item_count(world);
    telemetry->dormant_npc_states = world->dormant_npc_count;
    telemetry->dormant_ground_items = world->dormant_ground_item_count;
    telemetry->saved_npc_states = saved_npc_states;
    telemetry->restored_npc_states = restored_npc_states;
    telemetry->saved_ground_items = saved_ground_items;
    telemetry->restored_ground_items = restored_ground_items;

    if (stats) {
        stats->collision_regions = collision_regions;
        stats->collision_stats = collision_stats;
        stats->spawned_npcs = spawned;
        stats->spawned_ground_items = spawned_ground_items;
        stats->object_placement_stats = object_placement_stats;
        stats->npc_stats = npc_stats;
        stats->ground_item_stats = ground_item_stats;
        stats->active_area = world->active_area;
        stats->streaming = *telemetry;
    }
    return 1;
}

const RcActiveArea *rc_world_get_active_area(const RcWorld *world) {
    return world ? &world->active_area : NULL;
}

const RcWorldStreamingConfig *rc_world_get_streaming_config(
    const RcWorld *world) {
    return world ? &world->streaming : NULL;
}

int rc_world_get_streaming_telemetry(const RcWorld *world,
                                     RcWorldStreamingTelemetry *out) {
    if (!world || !out)
        return -1;
    *out = world->streaming_telemetry;
    out->active_npcs = active_npc_count(world);
    out->active_ground_items = active_ground_item_count(world);
    out->dormant_npc_states = world->dormant_npc_count;
    out->dormant_ground_items = world->dormant_ground_item_count;
    return 1;
}

int rc_world_find_npc_near(const RcWorld *world, int npc_id, int x, int y,
                           int plane, int radius) {
    if (!world || npc_id < 0 || plane < 0 || plane >= RC_MAX_PLANES
            || radius < 0)
        return -1;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        const RcNpcDef *def = rc_npc_def_for_npc(npc);
        if (!npc->active || !def)
            continue;
        if (npc->plane != plane || def->id != npc_id)
            continue;
        if (abs(npc->x - x) <= radius && abs(npc->y - y) <= radius)
            return i;
    }
    return -1;
}

int rc_world_ensure_npc_near(RcWorld *world, int npc_id, int x, int y,
                             int plane, int radius,
                             RcNpcEnsureResult *result) {
    if (result) {
        result->index = -1;
        result->uid = -1;
        result->spawned = 0;
    }
    if (!world)
        return -1;

    int idx = rc_world_find_npc_near(world, npc_id, x, y, plane, radius);
    if (idx >= 0) {
        if (result) {
            result->index = idx;
            result->uid = world->npcs[idx].uid;
        }
        return idx;
    }

    int def_idx = rc_npc_def_find(npc_id);
    if (def_idx < 0)
        return -1;
    idx = rc_npc_spawn(world, def_idx, x, y, plane);
    if (idx < 0)
        return -1;
    if (result) {
        result->index = idx;
        result->uid = world->npcs[idx].uid;
        result->spawned = 1;
    }
    return idx;
}
