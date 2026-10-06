#ifndef RC_TEST_STORAGE_FIXTURE_H
#define RC_TEST_STORAGE_FIXTURE_H

#include <assert.h>
#include "api.h"
#include "npc.h"
#include "storage.h"
#include "../rc-content/content.h"

// Tests use the same adjacent-banker interaction as player input, not an open flag.
static int test_storage_open(RcWorld *world) {
    rc_content_storage_register(world);
    int count = 0, index = -1, option = -1;
    const RcNpcDef *defs = rc_npc_defs_all(&count);
    for (int i = 0; i < count && index < 0; i++)
        for (int op = 0; op < RC_NPC_OPTION_COUNT; op++)
            if (rc_storage_kind_for_npc(world, &defs[i], op) == RC_STORAGE_BANK) {
                index = i; option = op; break;
            }
    assert(index >= 0 && "storage fixture needs banker definitions");
    int npc = -1;
    for (int i = 0; i < world->npc_count; i++)
        if (world->npcs[i].active && !world->npcs[i].is_dead && world->npcs[i].def_id == index
                && world->npcs[i].x == world->player.x + 1 && world->npcs[i].y == world->player.y
                && world->npcs[i].plane == world->player.plane) { npc = i; break; }
    if (npc < 0) npc = rc_npc_spawn(world, index, world->player.x + 1,
                                   world->player.y, world->player.plane);
    assert(npc >= 0);
    world->npcs[npc].wander_timer = 0;
    world->npcs[npc].disable_wander = true;
    bool in_tick = world->in_tick;
    world->in_tick = false;
    rc_player_interact_npc(world, world->npcs[npc].uid, option);
    rc_world_tick(world);
    assert(world->player.storage_kind == RC_STORAGE_BANK);
    world->in_tick = in_tick;
    return npc;
}

#endif
