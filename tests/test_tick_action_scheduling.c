#include "api.h"
#include "combat.h"
#include "events.h"
#include "npc.h"
#include "player_command.h"
#include "storage.h"
#include "spells.h"
#include "tick_pacing.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static RcWorld *make_world(uint32_t seed) {
    memset(g_npc_defs, 0, sizeof(g_npc_defs));
    g_npc_def_count = 1;
    g_npc_defs[0].id = 991001;
    strcpy(g_npc_defs[0].name, "Scheduling target");
    g_npc_defs[0].size = 1;
    g_npc_defs[0].hitpoints = 10;
    g_npc_defs[0].attack_speed = 4;

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.seed = seed;
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_test_world_create_with_defs(
        &cfg, "tick_action_scheduling", 0);
    assert(world);
    world->map.region_count = 1;
    RcRegion *region = &world->map.regions[0];
    memset(region, 0, sizeof(*region));
    region->loaded = true;
    region->region_x = 50;
    region->region_y = 53;
    assert(rc_world_relocate_player(world, 3200, 3392, 0));
    return world;
}

static void test_input_boundary_fifo_and_overflow(void) {
    RcWorld *world = make_world(1);
    rc_player_walk_to(world, 3200, 3394);
    rc_player_walk_to(world, 3202, 3392);
    assert(world->player.route_len == 0);
    assert(rc_player_pending_command_count(world) == 2);
    rc_world_tick(world);
    assert(world->player.x == 3201 && world->player.y == 3392);
    assert(rc_player_pending_command_count(world) == 0);

    for (int i = 0; i < RC_MAX_PLAYER_COMMANDS; i++)
        assert(rc_player_set_running(world, i & 1));
    assert(!rc_player_set_running(world, 1));
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_FULL);
    assert(world->player_commands.rejected_count == 1);
    rc_world_tick(world);
    assert(world->player.running == ((RC_MAX_PLAYER_COMMANDS - 1) & 1));
    rc_world_destroy(world);
}

static void test_run_soft_command_and_reentrancy(void) {
    RcWorld *world = make_world(9);
    rc_player_run_to(world, 3202, 3392);
    rc_player_set_spellbook(world, RC_SPELL_BOOK_ANCIENT);
    assert(!world->player.running);
    assert(world->player.current_spellbook == RC_SPELL_BOOK_STANDARD);
    assert(rc_player_pending_command_count(world) == 2);
    rc_world_tick(world);
    assert(world->player.running);
    assert(world->player.x == 3202 && world->player.y == 3392);
    assert(world->player.current_spellbook == RC_SPELL_BOOK_ANCIENT);

    RcTick tick = world->tick;
    world->in_tick = true;
    rc_world_tick(world);
    assert(world->tick == tick);
    world->in_tick = false;
    rc_world_tick(NULL);
    rc_world_destroy(world);
}

static void test_categories_and_central_cancellation(void) {
    RcWorld *world = make_world(2);
    world->player_action = (RcPlayerActionState){
        .active = true,
        .owner = RC_ACTION_OWNER_TRAVERSAL,
        .category = RC_ACTION_CATEGORY_STRONG,
        .started_tick = world->tick,
        .ready_tick = world->tick + 2,
    };
    assert(rc_player_set_running(world, 1));
    assert(!rc_player_move_inventory_item(world, 0, 1));
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_BUSY);
    rc_world_tick(world);
    assert(world->player.running);

    rc_player_walk_to(world, 3202, 3392);
    assert(!rc_player_pending_command_count(world));
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_BUSY);
    rc_player_cancel_action(world, RC_ACTION_CANCEL_FRONTEND);
    rc_player_walk_to(world, 3202, 3392);
    assert(rc_player_pending_command_count(world) == 1);
    rc_world_tick(world);
    assert(world->player.x == 3201);
    assert(world->player_action.owner == RC_ACTION_OWNER_MOVEMENT);
    rc_world_destroy(world);
}

static void test_queued_command_cancellation(void) {
    RcWorld *world = make_world(7);
    assert(rc_player_set_running(world, 1));
    rc_player_walk_to(world, 3202, 3392);
    assert(rc_player_pending_command_count(world) == 2);
    rc_player_cancel_action(world, RC_ACTION_CANCEL_FRONTEND);
    assert(rc_player_pending_command_count(world) == 0);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_CANCELLED);
    assert(world->player_commands.rejected_count == 2);
    rc_world_tick(world);
    assert(!world->player.running);
    assert(world->player.x == 3200 && world->player.y == 3392);
    rc_world_destroy(world);
}

static void test_same_cycle_strong_command_blocks_replacement(void) {
    RcWorld *world = make_world(10);
    world->enabled |= RC_SUB_TRAVERSAL;
    RcTraversalEdge edge = {
        .dest_x = 3210,
        .dest_y = 3400,
        .dest_plane = 0,
    };
    assert(rc_player_apply_traversal(world, &edge));
    rc_player_walk_to(world, 3202, 3392);
    assert(rc_player_pending_command_count(world) == 2);

    rc_world_tick(world);

    assert(world->player.x == 3210 && world->player.y == 3400);
    assert(world->player.route_len == 0);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_BUSY);
    assert(world->player_commands.rejected_count == 1);
    rc_world_destroy(world);
}

static void test_replacement_matrix_and_one_tick_lock(void) {
    RcWorld *world = make_world(6);
    int npc_idx = rc_npc_spawn(world, 0, 3201, 3392, 0);
    assert(npc_idx >= 0);
    assert(rc_combat_start_player_vs_npc(
        world, 0, world->npcs[npc_idx].uid));

    assert(rc_player_set_running(world, 1));
    rc_world_tick(world);
    assert(world->player.running);
    assert(world->player.combat.active);

    rc_player_walk_to(world, 3203, 3392);
    rc_world_tick(world);
    assert(!world->player.combat.active);
    assert(world->player_action.owner == RC_ACTION_OWNER_MOVEMENT);
    assert(world->player_action.last_cancel_reason == RC_ACTION_CANCEL_REPLACED);

    int x = world->player.x;
    world->player_action = (RcPlayerActionState){
        .active = true,
        .owner = RC_ACTION_OWNER_TRAVERSAL,
        .category = RC_ACTION_CATEGORY_STRONG,
        .started_tick = world->tick,
        .ready_tick = world->tick + 1,
    };
    rc_world_tick(world);
    assert(world->player.x == x);
    rc_world_tick(world);
    assert(world->player.x == x + 1);

    world->enabled |= RC_SUB_STORAGE;
    world->player.storage_kind = RC_STORAGE_BANK;
    rc_player_action_refresh(world);
    assert(world->player_action.owner == RC_ACTION_OWNER_MODAL);
    rc_player_walk_to(world, 3205, 3392);
    rc_world_tick(world);
    assert(world->player.storage_kind == RC_STORAGE_NONE);
    assert(world->player_action.last_cancel_reason == RC_ACTION_CANCEL_REPLACED);
    rc_world_destroy(world);
}

typedef struct {
    int deaths;
    RcTick tick;
} DeathEvents;

static void count_player_death(RcWorld *world, int event,
                               const void *payload, void *ctx) {
    (void)world;
    assert(event == RC_EVT_PLAYER_DIED);
    const RcPayloadPlayerDeath *death = payload;
    DeathEvents *events = ctx;
    events->deaths++;
    events->tick = death->tick;
}

static void test_player_death_and_respawn_handoff(void) {
    RcWorld *world = make_world(3);
    DeathEvents events = {0};
    assert(rc_event_subscribe(world, RC_EVT_PLAYER_DIED,
                              count_player_death, &events) == 0);
    world->player.current_hp = 0;
    rc_world_tick(world);
    assert(world->player.is_dead);
    assert(events.deaths == 1 && events.tick == 0);
    rc_world_tick(world);
    assert(events.deaths == 1);
    rc_player_walk_to(world, 3202, 3392);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_DEAD);
    assert(rc_world_respawn_player(world, 3210, 3400, 0));
    assert(!world->player.is_dead);
    assert(world->player.current_hp == world->player.max_hp);
    assert(world->player.x == 3210 && world->player.y == 3400);
    rc_world_destroy(world);
}

static void test_absolute_hit_deadlines_and_overflow(void) {
    RcPendingHit hits[RC_MAX_PENDING_HITS] = {0};
    int count = 0;
    for (int delay = 0; delay <= 4; delay++) {
        assert(rc_queue_hit(hits, &count, delay + 1, delay,
                            COMBAT_MELEE_CRUSH, -1, 0, 100));
    }
    assert(rc_resolve_pending(hits, &count, true, 100) == 1);
    assert(rc_resolve_pending(hits, &count, true, 101) == 2);
    assert(rc_resolve_pending(hits, &count, true, 102) == 3);
    assert(rc_resolve_pending(hits, &count, true, 103) == 4);
    assert(rc_resolve_pending(hits, &count, true, 104) == 5);
    assert(count == 0);
    for (int i = 0; i < RC_MAX_PENDING_HITS; i++)
        assert(rc_queue_hit(hits, &count, 1, i, COMBAT_RANGED,
                            -1, 0, 200));
    assert(!rc_queue_hit(hits, &count, 1, 0, COMBAT_RANGED,
                         -1, 0, 200));
}

static void freeze_on_hit(RcWorld *world, const RcPendingHit *hit,
                          int damage) {
    (void)hit;
    (void)damage;
    rc_player_apply_freeze(world, 1);
}

static void test_phase_independent_status_deadlines(void) {
    RcWorld *world = make_world(8);
    int npc_idx = rc_npc_spawn(world, 0, 3204, 3392, 0);
    assert(npc_idx >= 0);
    RcCombatContentHooks hooks = {
        .on_npc_hit_player = freeze_on_hit,
    };
    rc_combat_register_content_hooks(world, &hooks);

    rc_player_apply_freeze(world, 1);
    assert(rc_player_freeze_ticks_remaining(world) == 1);
    rc_player_walk_to(world, 3202, 3392);
    rc_world_tick(world);
    assert(world->player.x == 3200);
    assert(rc_player_freeze_ticks_remaining(world) == 0);
    rc_world_tick(world);
    assert(world->player.x == 3201);

    assert(rc_queue_hit(world->player.pending_hits,
                        &world->player.num_pending_hits,
                        1, 0, COMBAT_MAGIC, world->npcs[npc_idx].uid,
                        0, world->tick));
    rc_world_tick(world);
    assert(world->player.x == 3202);
    assert(rc_player_freeze_ticks_remaining(world) == 1);
    rc_player_walk_to(world, 3204, 3392);
    rc_world_tick(world);
    assert(world->player.x == 3202);
    rc_world_tick(world);
    assert(world->player.x == 3203);

    world->player.freeze_start_tick = 0;
    world->player.freeze_expire_tick = 0;
    world->in_tick = true;
    rc_player_apply_freeze(world, 1);
    assert(!rc_player_is_frozen(world));
    assert(rc_player_freeze_ticks_remaining(world) == 1);
    world->in_tick = false;
    world->tick++;
    assert(rc_player_is_frozen(world));
    assert(rc_player_freeze_ticks_remaining(world) == 1);
    world->tick++;
    assert(!rc_player_is_frozen(world));

    rc_player_apply_teleblock(world, 2);
    assert(rc_player_teleblock_ticks_remaining(world) == 2);
    rc_world_tick(world);
    assert(rc_player_teleblock_ticks_remaining(world) == 1);
    rc_world_tick(world);
    assert(rc_player_teleblock_ticks_remaining(world) == 0);
    rc_world_destroy(world);
}

static void test_npc_death_respawn_timeline(void) {
    RcWorld *world = make_world(4);
    int idx = rc_npc_spawn(world, 0, 3201, 3392, 0);
    assert(idx >= 0);
    RcNpc *npc = &world->npcs[idx];
    npc->is_dead = true;
    npc->current_hp = 0;
    npc->death_timer = 3;
    npc->respawn_timer = 4;
    for (int i = 0; i < 3; i++) {
        rc_npc_tick(world, npc);
        assert(npc->is_dead);
        assert(npc->death_timer == 2 - i);
        assert(npc->respawn_timer == 4);
    }
    for (int i = 0; i < 3; i++) {
        rc_npc_tick(world, npc);
        assert(npc->is_dead);
        assert(npc->respawn_timer == 3 - i);
    }
    rc_npc_tick(world, npc);
    assert(!npc->is_dead);
    assert(npc->current_hp == npc->spawn_hp);
    rc_world_destroy(world);
}

static void run_paced_trace(RcWorld *world, const double *frames,
                            int frame_count) {
    RcViewerTickPacing pacing = {0};
    const double tick_seconds = 0.6;
    for (int frame = 0; frame < frame_count; frame++) {
        int due = rc_viewer_tick_pacing_advance(
            &pacing, frames[frame], tick_seconds);
        for (int i = 0; i < due; i++) {
            if (world->tick == 0 || world->tick == 2 || world->tick == 4)
                assert(rc_player_set_running(world, world->tick != 2));
            rc_world_tick(world);
        }
    }
}

static void test_viewer_pacing_and_headless_parity(void) {
    RcViewerTickPacing pacing = {0};
    assert(rc_viewer_tick_pacing_advance(NULL, 0.6, 0.6) == 0);
    assert(rc_viewer_tick_pacing_advance(&pacing, -0.1, 0.6) == 0);
    assert(rc_viewer_tick_pacing_advance(&pacing, 0.6, 0.0) == 0);
    assert(rc_viewer_tick_pacing_advance(&pacing, 0.2, 0.6) == 0);
    assert(rc_viewer_tick_pacing_advance(&pacing, 0.4, 0.6) == 1);
    assert(rc_viewer_tick_pacing_advance(&pacing, 5.4, 0.6)
           == RC_VIEWER_MAX_CATCH_UP_TICKS);
    assert(pacing.dropped_ticks == 4);
    assert(rc_viewer_tick_pacing_fraction(&pacing, 0.6) >= 0.0);
    rc_viewer_tick_pacing_reset(&pacing);
    assert(rc_viewer_tick_pacing_fraction(&pacing, 0.6) == 0.0);
    assert(pacing.dropped_ticks == 4);

    RcWorld *regular = make_world(5);
    RcWorld *stalled = make_world(5);
    const double regular_frames[] = {0.6, 0.6, 0.6, 0.6, 0.6, 0.6};
    const double stalled_frames[] = {1.8, 0.6, 1.2, 0.0};
    run_paced_trace(regular, regular_frames, 6);
    run_paced_trace(stalled, stalled_frames, 4);
    assert(regular->tick == stalled->tick);
    assert(regular->player.running == stalled->player.running);
    assert(regular->player_commands.next_sequence
           == stalled->player_commands.next_sequence);
    rc_world_destroy(regular);
    rc_world_destroy(stalled);
}

int main(void) {
    test_input_boundary_fifo_and_overflow();
    test_run_soft_command_and_reentrancy();
    test_categories_and_central_cancellation();
    test_queued_command_cancellation();
    test_same_cycle_strong_command_blocks_replacement();
    test_replacement_matrix_and_one_tick_lock();
    test_player_death_and_respawn_handoff();
    test_absolute_hit_deadlines_and_overflow();
    test_phase_independent_status_deadlines();
    test_npc_death_respawn_timeline();
    test_viewer_pacing_and_headless_parity();
    printf("tick/action scheduling tests passed\n");
    return 0;
}
