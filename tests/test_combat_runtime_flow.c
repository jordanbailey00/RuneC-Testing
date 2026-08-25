#include "../rc-core/api.h"
#include "../rc-core/combat.h"
#include "../rc-core/npc.h"
#include "runtime_test_fixture.h"

#include <assert.h>
#include <string.h>

int main(void) {
    g_npc_def_count = 1;
    memset(&g_npc_defs[0], 0, sizeof(g_npc_defs[0]));
    g_npc_defs[0].id = 900001;
    g_npc_defs[0].size = 1;
    g_npc_defs[0].combat_level = 2;
    g_npc_defs[0].hitpoints = 5;
    g_npc_defs[0].stats[1] = 1;
    g_npc_defs[0].max_hit = 1;
    g_npc_defs[0].attack_speed = 4;
    g_npc_defs[0].attack_types = 0x04;
    g_npc_defs[0].respawn_ticks = 8;
    strcpy(g_npc_defs[0].options[1], "Attack");

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.seed = 12345;

    RcWorld *world = rc_test_world_create_with_defs(&cfg, "combat_flow", 0);
    assert(world);
    rc_test_open_mapsquare(world, world->player.x, world->player.y,
                           world->player.plane);

    RcPlayer *p = &world->player;
    for (int i = 0; i < SKILL_COUNT; i++) {
        p->skills.base_level[i] = 99;
        p->skills.boosted_level[i] = 99;
    }
    p->equipment_bonuses[EQ_CRUSH_ATK] = 10000;
    p->equipment_bonuses[EQ_STR] = 100;
    rc_player_set_attack_style(world, 0);

    int npc_idx = rc_npc_spawn(world, 0, p->x + 3, p->y, p->plane);
    assert(npc_idx >= 0);
    int uid = world->npcs[npc_idx].uid;

    int xp_before = p->skills.xp[SKILL_ATTACK];
    rc_player_attack_npc(world, uid);
    for (int i = 0; i < 40 && !world->npcs[npc_idx].is_dead; i++) {
        rc_world_tick(world);
    }

    assert(world->npcs[npc_idx].is_dead);
    assert(world->npcs[npc_idx].death_timer > 0 ||
           world->npcs[npc_idx].respawn_timer > 0);
    assert(p->skills.xp[SKILL_ATTACK] > xp_before);
    assert(p->attack_target == -1);

    rc_world_destroy(world);
    return 0;
}
