#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "npc.h"
#include "npc_render_defs.h"

static void path_join(char *out, size_t out_sz, const char *rel) {
#ifdef RC_TEST_SOURCE_DIR
    snprintf(out, out_sz, "%s/%s", RC_TEST_SOURCE_DIR, rel);
#else
    snprintf(out, out_sz, "%s", rel);
#endif
}

int main(void) {
    char path[512];
    path_join(path, sizeof(path), "data/defs/npc_defs.bin");

    g_npc_def_count = 0;
    int loaded = rc_load_npc_defs(path);
    assert(loaded > 10000);

    int jad = rc_npc_def_find(3127);
    int zulrah = rc_npc_def_find(2042);
    int zuk = rc_npc_def_find(7706);
    int glyph = rc_npc_def_find(7707);
    int tempoross = rc_npc_def_find(10572);
    int nex = rc_npc_def_find(11278);
    int sol = rc_npc_def_find(12821);
    assert(jad >= 0);
    assert(zulrah >= 0);
    assert(zuk >= 0);
    assert(glyph >= 0);
    assert(tempoross >= 0);
    assert(nex >= 0);
    assert(sol >= 0);
    assert(strcmp(g_npc_defs[jad].name, "TzTok-Jad") == 0);
    assert(strcmp(g_npc_defs[zuk].name, "TzKal-Zuk") == 0);
    assert(strcmp(g_npc_defs[glyph].name, "Ancestral Glyph") == 0);
    assert(g_npc_defs[nex].hitpoints >= 3400);
    assert(g_npc_defs[sol].hitpoints > 0);
    int aggressive = rc_npc_def_find(1);
    assert(aggressive >= 0);
    assert(g_npc_defs[aggressive].wander_range == 0);
    assert(g_npc_defs[aggressive].respawn_ticks == 25);
    assert(g_npc_defs[aggressive].regen_ticks == 100);
    assert(g_npc_defs[aggressive].hunt.target == RC_NPC_HUNT_PLAYER);
    assert(g_npc_defs[aggressive].hunt.visibility
           == RC_NPC_HUNT_VIS_LINE_OF_SIGHT);
    assert(g_npc_defs[aggressive].hunt.flags
           & RC_NPC_HUNT_CHECK_NOT_BUSY);

    int transformed = rc_npc_def_find(471);
    assert(transformed >= 0);
    assert(g_npc_defs[transformed].transform_varp == 1306);
    assert(g_npc_defs[transformed].transform_count == 5);
    RcWorldConfig transform_cfg = rc_preset_base_only();
    RcWorld *transform_world = rc_world_create_config(&transform_cfg);
    assert(transform_world != NULL);
    int transform_slot = rc_npc_spawn(transform_world, transformed,
                                      3200, 3200, 0);
    assert(transform_slot >= 0);
    const RcNpcDef *active = rc_npc_def_for_npc(
        transform_world, &transform_world->npcs[transform_slot]);
    assert(active && active->id == 8690);
    transform_world->varps[1306] = 3;
    active = rc_npc_def_for_npc(
        transform_world, &transform_world->npcs[transform_slot]);
    assert(active && active->id == 8691);
    rc_world_destroy(transform_world);
    RuneCNpcRenderDefs render_defs;
    assert(runec_npc_render_defs_load(&render_defs, path) > 10000);
    const RuneCNpcRenderDef *jad_render =
        runec_npc_render_find(&render_defs, 3127);
    const RuneCNpcRenderDef *zuk_render =
        runec_npc_render_find(&render_defs, 7706);
    const RuneCNpcRenderDef *nex_render =
        runec_npc_render_find(&render_defs, 11278);
    assert(jad_render && jad_render->model_count > 0);
    assert(zuk_render && zuk_render->model_count > 0);
    assert(nex_render && nex_render->model_count > 0);

    char spawns_path[512];
    path_join(spawns_path, sizeof(spawns_path),
              "data/spawns/world.npc-spawns.indexed.bin");
    RcWorldConfig spawn_cfg = rc_preset_base_only();
    spawn_cfg.npc_capacity = RC_MAX_NPCS;
    RcWorld *spawn_world = rc_world_create_config(&spawn_cfg);
    assert(spawn_world != NULL);
    int spawned = rc_load_npc_spawns(spawn_world, spawns_path);
    assert(spawned > 20000);
    assert(spawn_world->npc_count == spawned);
    assert(spawn_world->npcs[0].spawn_direction == 6);
    assert(spawn_world->npcs[0].spawn_wander_range == 0);
    int resident_count = spawn_world->npc_count;
    assert(rc_load_npc_spawns(spawn_world, spawns_path) == 0);
    assert(spawn_world->npc_count == resident_count);
    rc_world_destroy(spawn_world);

    g_npc_def_count = 0;
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT;
    cfg.npc_defs_path = path;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_npc_def_count > 10000);
    rc_world_destroy(world);

    printf("test_npc_defs_bin: loaded broad NPC definition binary.\n");
    return 0;
}
