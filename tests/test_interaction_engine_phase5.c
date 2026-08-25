#include "../rc-core/api.h"
#include "../rc-core/npc.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <string.h>

typedef struct {
    int calls;
    int seen_uid;
} Phase5HandlerCtx;

static RcInteractionHandlerResult phase5_complete_handler(
    RcWorld *world, RcPlayer *player, const RcPendingInteraction *pending,
    void *ctx) {
    (void)world;
    (void)player;
    Phase5HandlerCtx *state = ctx;
    state->calls++;
    state->seen_uid = pending->target.entity_uid;
    return rc_interaction_result_complete();
}

static RcWorld *phase5_world(int cache_id, int add_attack) {
    g_npc_def_count = 1;
    memset(&g_npc_defs[0], 0, sizeof(g_npc_defs[0]));
    g_npc_defs[0].id = cache_id;
    strcpy(g_npc_defs[0].name, "Phase 5 Dummy");
    g_npc_defs[0].size = 1;
    g_npc_defs[0].combat_level = 2;
    g_npc_defs[0].hitpoints = 10;
    g_npc_defs[0].stats[3] = 10;
    g_npc_defs[0].max_hit = 1;
    g_npc_defs[0].attack_speed = 4;
    g_npc_defs[0].attack_types = 0x04;
    strcpy(g_npc_defs[0].options[0], "Talk-to");
    if (add_attack) strcpy(g_npc_defs[0].options[1], "Attack");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    RcWorld *world = rc_test_world_create_with_defs(&cfg, "interaction5", 0);
    assert(world);
    return world;
}

static int spawn_phase5_npc(RcWorld *world, int dx) {
    int npc_idx = rc_npc_spawn(world, 0, world->player.x + dx,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    return npc_idx;
}

static void tick_until_inactive(RcWorld *world, int max_ticks) {
    for (int i = 0; i < max_ticks; i++) {
        rc_world_tick(world);
        if (!rc_interaction_is_active(&world->player)) break;
    }
}

static void test_attack_group_handler_beats_generic_option_handler(void) {
    RcWorld *world = phase5_world(901500, 1);
    int npc_idx = spawn_phase5_npc(world, 1);
    int uid = world->npcs[npc_idx].uid;
    Phase5HandlerCtx generic = {0};
    Phase5HandlerCtx attack = {0};

    RcInteractionDispatchKey key = rc_interaction_dispatch_key_any();
    key.kind = RC_INTERACTION_NPC;
    key.op = RC_INTERACTION_OP2;
    assert(rc_interaction_register_world_handler(
        world, &key, phase5_complete_handler, &generic));
    key.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    assert(rc_interaction_register_world_handler(
        world, &key, phase5_complete_handler, &attack));

    rc_player_interact_npc(world, uid, 1);
    tick_until_inactive(world, 8);
    assert(attack.calls == 1);
    assert(attack.seen_uid == uid);
    assert(generic.calls == 0);
    assert(world->player.interaction.target.content_group
           == RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
}

static void test_default_attack_handler_starts_and_refreshes_combat(void) {
    RcWorld *world = phase5_world(901501, 1);
    int npc_idx = spawn_phase5_npc(world, 1);
    int uid = world->npcs[npc_idx].uid;

    rc_player_interact_npc(world, uid, 1);
    tick_until_inactive(world, 8);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.op == RC_INTERACTION_OP2);
    assert(world->player.interaction.target.content_group
           == RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK);
    assert(world->player.interaction.target.entity_uid == uid);
    assert(world->player.interaction.target.definition_id == 901501);
    assert(world->player.interaction.target.footprint_width == 1);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 1);
    assert(world->player.attack_target == uid);
    assert(world->npcs[npc_idx].target_uid == 0);

    world->player.attack_target = uid;
    world->player.interact_type = RC_INTERACT_NONE;
    rc_interaction_clear(&world->player);
    rc_player_attack_npc(world, uid);
    tick_until_inactive(world, 8);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.attack_target == uid);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.interact_target == uid);

    rc_world_destroy(world);
}

static void test_attack_requires_data_backed_attack_option(void) {
    RcWorld *world = phase5_world(901502, 0);
    int npc_idx = spawn_phase5_npc(world, 1);
    int uid = world->npcs[npc_idx].uid;

    assert(rc_player_attack_npc(world, uid));
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.attack_target == -1);
    rc_world_tick(world);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_INVALID);

    RcInteractionTarget target = {0};
    target.kind = RC_INTERACTION_NPC;
    target.entity_uid = uid;
    target.definition_id = 901502;
    target.content_group = RC_INTERACTION_CONTENT_GROUP_NPC_ATTACK;
    target.tile_x = world->npcs[npc_idx].x;
    target.tile_y = world->npcs[npc_idx].y;
    target.plane = world->npcs[npc_idx].plane;
    target.footprint_width = 1;
    target.footprint_height = 1;
    target.inventory_slot = -1;
    target.equipment_slot = -1;
    target.widget_id = -1;
    target.component_id = -1;
    target.ground_item_instance = -1;
    assert(rc_interaction_begin(&world->player, 0, RC_INTERACTION_OP2,
                                "Attack", &target, 1));
    rc_world_tick(world);
    assert(!rc_interaction_is_active(&world->player));
    assert(world->player.interaction.last_failure
           == RC_INTERACTION_FAIL_OPTION_UNAVAILABLE);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
}

int main(void) {
    test_attack_group_handler_beats_generic_option_handler();
    test_default_attack_handler_starts_and_refreshes_combat();
    test_attack_requires_data_backed_attack_option();
    return 0;
}
