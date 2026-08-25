#include "../rc-core/api.h"
#include "../rc-core/npc.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <string.h>

int main(void) {
    g_npc_def_count = 1;
    memset(&g_npc_defs[0], 0, sizeof(g_npc_defs[0]));
    g_npc_defs[0].id = 900002;
    strcpy(g_npc_defs[0].name, "Option Dummy");
    g_npc_defs[0].size = 1;
    g_npc_defs[0].combat_level = 2;
    g_npc_defs[0].hitpoints = 10;
    g_npc_defs[0].stats[3] = 10;
    strcpy(g_npc_defs[0].options[0], "Talk-to");
    strcpy(g_npc_defs[0].options[1], "Attack");
    strcpy(g_npc_defs[0].options[2], "Trade");

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;

    RcWorld *world = rc_test_world_create_with_defs(&cfg, "npc_options", 0);
    assert(world);

    int npc_idx = rc_npc_spawn(world, 0, world->player.x + 2,
                               world->player.y, world->player.plane);
    assert(npc_idx >= 0);
    int uid = world->npcs[npc_idx].uid;

    rc_player_interact_npc(world, uid, 0);
    assert(!world->player.interaction.active);
    assert(world->player.route_len == 0);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC);
    assert(world->player.interact_target == uid);
    assert(world->player.interact_option == 0);
    assert(world->player.attack_target == -1);

    rc_player_interact_npc(world, uid, 1);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NPC_ATTACK);
    assert(world->player.interact_target == uid);
    assert(world->player.attack_target == uid);

    world->player.attack_target = -1;
    world->player.interact_type = RC_INTERACT_NONE;
    world->player.interact_target = -1;
    rc_player_interact_npc(world, uid, 4);
    rc_world_tick(world);
    assert(world->player.interact_type == RC_INTERACT_NONE);
    assert(world->player.attack_target == -1);

    rc_world_destroy(world);
    return 0;
}
