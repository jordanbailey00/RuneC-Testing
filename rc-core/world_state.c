#include "world_state.h"

#include "combat.h"
#include "npc.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct RcDormantNpcState {
    uint64_t spawn_key;
    int x, y, plane;
    int current_hp;
    int attack_timer;
    int death_timer;
    int respawn_timer;
    int wander_timer;
    int attack_count;
    int poison_damage;
    int poison_tick_counter;
    bool is_dead;
    bool disable_wander;
    bool force_player_max_hit;
    bool player_untargetable;
};

struct RcDormantGroundItemState {
    uint64_t key;
    bool static_spawn;
    RcGroundItem item;
};

static int reserve_npcs(RcWorld *world, int needed) {
    if (needed <= world->dormant_npc_capacity) return 1;
    int capacity = world->dormant_npc_capacity > 0
                 ? world->dormant_npc_capacity : 16;
    while (capacity < needed) {
        if (capacity > INT_MAX / 2) return 0;
        capacity *= 2;
    }
    void *states = realloc(world->dormant_npcs,
                           (size_t)capacity * sizeof(*world->dormant_npcs));
    if (!states) return 0;
    world->dormant_npcs = states;
    world->dormant_npc_capacity = capacity;
    return 1;
}

static int reserve_ground_items(RcWorld *world, int needed) {
    if (needed <= world->dormant_ground_item_capacity) return 1;
    int capacity = world->dormant_ground_item_capacity > 0
                 ? world->dormant_ground_item_capacity : 16;
    while (capacity < needed) {
        if (capacity > INT_MAX / 2) return 0;
        capacity *= 2;
    }
    void *states = realloc(
        world->dormant_ground_items,
        (size_t)capacity * sizeof(*world->dormant_ground_items));
    if (!states) return 0;
    world->dormant_ground_items = states;
    world->dormant_ground_item_capacity = capacity;
    return 1;
}

static int npc_lower_bound(const RcWorld *world, uint64_t key, bool *found) {
    int lo = 0;
    int hi = world->dormant_npc_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (world->dormant_npcs[mid].spawn_key < key) lo = mid + 1;
        else hi = mid;
    }
    if (found) {
        *found = lo < world->dormant_npc_count
              && world->dormant_npcs[lo].spawn_key == key;
    }
    return lo;
}

static void remove_npc_state(RcWorld *world, int index) {
    if (index < 0 || index >= world->dormant_npc_count) return;
    memmove(&world->dormant_npcs[index], &world->dormant_npcs[index + 1],
            (size_t)(world->dormant_npc_count - index - 1)
                * sizeof(*world->dormant_npcs));
    world->dormant_npc_count--;
}

static int npc_is_default(const RcNpc *npc) {
    if (!npc || npc->spawn_key == 0) return 1;
    return npc->active && !npc->is_dead
        && npc->x == npc->spawn_x && npc->y == npc->spawn_y
        && npc->plane == npc->spawn_plane
        && npc->current_hp == npc->spawn_hp
        && npc->attack_timer == 0 && npc->death_timer == 0
        && npc->respawn_timer == 0
        && npc->attack_count == 0 && npc->poison_damage == 0
        && npc->poison_tick_counter == 0 && !npc->disable_wander
        && !npc->force_player_max_hit && !npc->player_untargetable;
}

static struct RcDormantNpcState npc_state_from_active(const RcNpc *npc) {
    return (struct RcDormantNpcState){
        .spawn_key = npc->spawn_key,
        .x = npc->x,
        .y = npc->y,
        .plane = npc->plane,
        .current_hp = npc->current_hp,
        .attack_timer = npc->attack_timer,
        .death_timer = npc->death_timer,
        .respawn_timer = npc->respawn_timer,
        .wander_timer = (npc->x != npc->spawn_x || npc->y != npc->spawn_y)
                      ? npc->wander_timer : 0,
        .attack_count = npc->attack_count,
        .poison_damage = npc->poison_damage,
        .poison_tick_counter = npc->poison_tick_counter,
        .is_dead = npc->is_dead,
        .disable_wander = npc->disable_wander,
        .force_player_max_hit = npc->force_player_max_hit,
        .player_untargetable = npc->player_untargetable,
    };
}

int rc_world_state_save_npcs(RcWorld *world) {
    if (!world) return -1;
    if (!reserve_npcs(world, world->dormant_npc_count + world->npc_count))
        return -1;

    int saved = 0;
    for (int i = 0; i < world->npc_count; i++) {
        const RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->spawn_key == 0) continue;
        bool found = false;
        int at = npc_lower_bound(world, npc->spawn_key, &found);
        if (npc_is_default(npc)) {
            if (found) remove_npc_state(world, at);
            continue;
        }
        struct RcDormantNpcState state = npc_state_from_active(npc);
        if (found) {
            world->dormant_npcs[at] = state;
        } else {
            memmove(&world->dormant_npcs[at + 1],
                    &world->dormant_npcs[at],
                    (size_t)(world->dormant_npc_count - at)
                        * sizeof(*world->dormant_npcs));
            world->dormant_npcs[at] = state;
            world->dormant_npc_count++;
        }
        saved++;
    }
    return saved;
}

static void apply_npc_state(RcNpc *npc,
                            const struct RcDormantNpcState *state) {
    npc->x = state->x;
    npc->y = state->y;
    npc->plane = state->plane;
    npc->prev_x = state->x;
    npc->prev_y = state->y;
    npc->current_hp = state->current_hp;
    npc->attack_timer = state->attack_timer;
    npc->death_timer = state->death_timer;
    npc->respawn_timer = state->respawn_timer;
    npc->wander_timer = state->wander_timer;
    npc->attack_count = state->attack_count;
    npc->poison_damage = state->poison_damage;
    npc->poison_tick_counter = state->poison_tick_counter;
    npc->is_dead = state->is_dead;
    npc->disable_wander = state->disable_wander;
    npc->force_player_max_hit = state->force_player_max_hit;
    npc->player_untargetable = state->player_untargetable;
    npc->target_uid = -1;
    npc->facing_entity = -1;
    npc->facing_x = -1;
    npc->facing_y = -1;
    npc->last_hit = -1;
    npc->last_hit_timer = 0;
    npc->num_pending_hits = 0;
    rc_combat_init_npc_state(npc);
    npc->combat.hp_current = npc->current_hp;
    npc->combat.hp_max = npc->spawn_hp;
    npc->combat.attack_count = npc->attack_count;
}

int rc_world_state_restore_npcs(RcWorld *world) {
    if (!world) return -1;
    int restored = 0;
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        if (!npc->active || npc->spawn_key == 0) continue;
        bool found = false;
        int at = npc_lower_bound(world, npc->spawn_key, &found);
        if (!found) continue;
        apply_npc_state(npc, &world->dormant_npcs[at]);
        remove_npc_state(world, at);
        restored++;
    }
    return restored;
}

static int ground_compare(bool static_a, uint64_t key_a,
                          bool static_b, uint64_t key_b) {
    if (static_a != static_b) return static_a ? -1 : 1;
    return key_a < key_b ? -1 : key_a > key_b;
}

static int ground_lower_bound(const RcWorld *world, bool static_spawn,
                              uint64_t key, bool *found) {
    int lo = 0;
    int hi = world->dormant_ground_item_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        const struct RcDormantGroundItemState *state =
            &world->dormant_ground_items[mid];
        if (ground_compare(state->static_spawn, state->key,
                           static_spawn, key) < 0) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (found) {
        *found = lo < world->dormant_ground_item_count
              && ground_compare(
                    world->dormant_ground_items[lo].static_spawn,
                    world->dormant_ground_items[lo].key,
                    static_spawn, key) == 0;
    }
    return lo;
}

static void remove_ground_state(RcWorld *world, int index) {
    if (index < 0 || index >= world->dormant_ground_item_count) return;
    memmove(&world->dormant_ground_items[index],
            &world->dormant_ground_items[index + 1],
            (size_t)(world->dormant_ground_item_count - index - 1)
                * sizeof(*world->dormant_ground_items));
    world->dormant_ground_item_count--;
}

static int upsert_ground_state(RcWorld *world, bool static_spawn,
                               uint64_t key, const RcGroundItem *item) {
    bool found = false;
    int at = ground_lower_bound(world, static_spawn, key, &found);
    struct RcDormantGroundItemState state = {
        .key = key,
        .static_spawn = static_spawn,
        .item = *item,
    };
    if (found) {
        world->dormant_ground_items[at] = state;
        return 1;
    }
    memmove(&world->dormant_ground_items[at + 1],
            &world->dormant_ground_items[at],
            (size_t)(world->dormant_ground_item_count - at)
                * sizeof(*world->dormant_ground_items));
    world->dormant_ground_items[at] = state;
    world->dormant_ground_item_count++;
    return 1;
}

static int ground_item_is_default(const RcGroundItem *item) {
    return item->active && item->static_spawn && item->spawn_key != 0
        && item->quantity == item->spawn_quantity
        && item->owner_uid == RC_GROUND_OWNER_NONE
        && item->original_owner_uid == RC_GROUND_OWNER_NONE
        && item->visibility == RC_GROUND_VIS_PUBLIC
        && item->reveal_timer == 0 && item->despawn_timer == 0;
}

static int point_in_area(int x, int y, int plane,
                         int min_x, int min_y, int max_x, int max_y,
                         int min_plane, int max_plane) {
    return x >= min_x && x <= max_x && y >= min_y && y <= max_y
        && plane >= min_plane && plane <= max_plane;
}

int rc_world_state_save_ground_items(RcWorld *world,
                                     int min_x, int min_y,
                                     int max_x, int max_y,
                                     int min_plane, int max_plane) {
    if (!world || min_x > max_x || min_y > max_y
            || min_plane > max_plane) {
        return -1;
    }
    if (!reserve_ground_items(
            world, world->dormant_ground_item_count
                 + world->ground_item_count)) {
        return -1;
    }

    int saved = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem *item = &world->ground_items[i];
        if (item->static_spawn && item->spawn_key != 0) {
            bool found = false;
            int at = ground_lower_bound(world, true, item->spawn_key, &found);
            if (ground_item_is_default(item)) {
                if (found) remove_ground_state(world, at);
            } else {
                upsert_ground_state(world, true, item->spawn_key, item);
                saved++;
            }
            continue;
        }
        if (!item->active || point_in_area(
                item->x, item->y, item->plane,
                min_x, min_y, max_x, max_y, min_plane, max_plane)) {
            continue;
        }
        RcGroundItem state = *item;
        state.version++;
        if (state.version <= 0) state.version = 1;
        upsert_ground_state(world, false, (uint64_t)(uint32_t)item->uid,
                            &state);
        item->active = false;
        item->quantity = 0;
        item->version = state.version;
        saved++;
    }
    return saved;
}

static void apply_static_ground_state(
    RcGroundItem *item, const struct RcDormantGroundItemState *state) {
    int version = item->version + 1;
    item->quantity = state->item.quantity;
    item->owner_uid = state->item.owner_uid;
    item->original_owner_uid = state->item.original_owner_uid;
    item->reveal_timer = state->item.reveal_timer;
    item->despawn_timer = state->item.despawn_timer;
    item->visibility = state->item.visibility;
    item->active = state->item.active;
    item->version = version > 0 ? version : 1;
}

static int inactive_ground_slot(RcWorld *world) {
    for (int i = 0; i < world->ground_item_count; i++) {
        if (!world->ground_items[i].active
                && !world->ground_items[i].static_spawn) {
            return i;
        }
    }
    if (world->ground_item_count >= RC_MAX_GROUND_ITEMS) return -1;
    return world->ground_item_count++;
}

int rc_world_state_restore_ground_items(RcWorld *world,
                                        int min_x, int min_y,
                                        int max_x, int max_y,
                                        int min_plane, int max_plane) {
    if (!world || min_x > max_x || min_y > max_y
            || min_plane > max_plane) {
        return -1;
    }
    int restored = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem *item = &world->ground_items[i];
        if (!item->active || !item->static_spawn || item->spawn_key == 0)
            continue;
        bool found = false;
        int at = ground_lower_bound(world, true, item->spawn_key, &found);
        if (!found) continue;
        apply_static_ground_state(item, &world->dormant_ground_items[at]);
        remove_ground_state(world, at);
        restored++;
    }

    for (int i = 0; i < world->dormant_ground_item_count;) {
        struct RcDormantGroundItemState *state =
            &world->dormant_ground_items[i];
        if (state->static_spawn || !point_in_area(
                state->item.x, state->item.y, state->item.plane,
                min_x, min_y, max_x, max_y, min_plane, max_plane)) {
            i++;
            continue;
        }
        int slot = inactive_ground_slot(world);
        if (slot < 0) {
            i++;
            continue;
        }
        world->ground_items[slot] = state->item;
        world->ground_items[slot].active = true;
        world->ground_items[slot].static_spawn = false;
        world->ground_items[slot].spawn_key = 0;
        world->ground_items[slot].spawn_quantity = 0;
        if (world->next_ground_item_uid <= state->item.uid)
            world->next_ground_item_uid = state->item.uid + 1;
        remove_ground_state(world, i);
        restored++;
    }
    return restored;
}

void rc_world_state_destroy(RcWorld *world) {
    if (!world) return;
    free(world->dormant_npcs);
    free(world->dormant_ground_items);
    world->dormant_npcs = NULL;
    world->dormant_ground_items = NULL;
    world->dormant_npc_count = 0;
    world->dormant_ground_item_count = 0;
    world->dormant_npc_capacity = 0;
    world->dormant_ground_item_capacity = 0;
}
