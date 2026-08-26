#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "api.h"
#include "config.h"
#include "npc.h"
#include "npc_render_defs.h"
#include "spawn_index.h"

enum {
    TEST_NSPI_MAGIC = 0x4950534e,
    TEST_NSPI_RECORD_SIZE = 20,
};

static void path_join(char *out, size_t out_sz, const char *rel) {
#ifdef RC_TEST_SOURCE_DIR
    snprintf(out, out_sz, "%s/%s", RC_TEST_SOURCE_DIR, rel);
#else
    snprintf(out, out_sz, "%s", rel);
#endif
}

static void write_u32(FILE *file, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, file) == 1);
}

static void write_u8(FILE *file, uint8_t value) {
    assert(fwrite(&value, sizeof(value), 1, file) == 1);
}

static void write_spawn_row(FILE *file, uint32_t source_order,
                            uint32_t npc_id, int x, int y, uint8_t wander) {
    write_u32(file, source_order);
    write_u32(file, npc_id);
    write_u32(file, (uint32_t)x);
    write_u32(file, (uint32_t)y);
    write_u8(file, 0);
    write_u8(file, 6);
    write_u8(file, wander);
    write_u8(file, 0);
}

static void write_wander_spawn_fixture(const char *path, uint32_t npc_id) {
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    write_u32(file, TEST_NSPI_MAGIC);
    write_u32(file, RC_SPAWN_INDEX_VERSION);
    write_u32(file, 2);
    write_u32(file, TEST_NSPI_RECORD_SIZE);
    write_u32(file, 1);
    write_u32(file, 2);
    write_u32(file, 0);
    write_u32(file, 0);
    write_u32(file, 0);
    uint32_t first_row = 0;
    uint32_t fixture_page = (50u << 8) | 53u;
    for (uint32_t page = 0; page < RC_SPAWN_INDEX_MAPSQUARE_COUNT; page++) {
        uint32_t count = page == fixture_page ? 2u : 0u;
        write_u32(file, first_row);
        write_u32(file, count);
        first_row += count;
    }
    write_spawn_row(file, 0, npc_id, 3200, 3392, 0);
    write_spawn_row(file, 1, npc_id, 3201, 3392,
                    RC_NPC_SPAWN_WANDER_USE_DEF);
    assert(fclose(file) == 0);
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
    assert(g_npc_defs[aggressive].wander_range == 5);
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
    assert(spawn_world->npcs[0].spawn_wander_range
           == g_npc_defs[spawn_world->npcs[0].def_id].wander_range);
    assert(spawn_world->npcs[0].spawn_wander_range == 5);
    int resident_count = spawn_world->npc_count;
    assert(rc_load_npc_spawns(spawn_world, spawns_path) == 0);
    assert(spawn_world->npc_count == resident_count);
    rc_world_destroy(spawn_world);

    char wander_fixture[256];
    snprintf(wander_fixture, sizeof(wander_fixture),
             "/tmp/runec_npc_wander_spawns_%ld.bin", (long)getpid());
    write_wander_spawn_fixture(wander_fixture,
                               (uint32_t)g_npc_defs[aggressive].id);
    RcWorldConfig wander_cfg = rc_preset_base_only();
    RcWorld *wander_world = rc_world_create_config(&wander_cfg);
    assert(wander_world != NULL);
    assert(rc_load_npc_spawns(wander_world, wander_fixture) == 2);
    assert(wander_world->npcs[0].spawn_wander_range == 0);
    assert(wander_world->npcs[1].spawn_wander_range == 5);
    rc_world_destroy(wander_world);
    assert(remove(wander_fixture) == 0);

    char collision_path[512];
    path_join(collision_path, sizeof(collision_path),
              "data/regions/world.collision-tiles.indexed.bin");
    RcWorldConfig live_cfg = rc_preset_base_only();
    live_cfg.subsystems = RC_SUB_COMBAT | RC_SUB_REGIONS;
    live_cfg.npc_capacity = RC_WORLD_NPC_CAPACITY_SIM;
    live_cfg.npc_defs_path = path;
    live_cfg.spawns_path = spawns_path;
    live_cfg.collision_tiles_path = collision_path;
    RcWorld *live_world = rc_world_create_config(&live_cfg);
    assert(live_world != NULL);
    RcActiveAreaRequest varrock = {
        .origin_x = 3008,
        .origin_y = 3264,
        .width = 320,
        .height = 320,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
    };
    assert(rc_world_activate_area(live_world, &varrock, NULL) == 1);
    assert(live_world->npc_count > 500);
    for (int tick = 0; tick < 30; tick++) rc_world_tick(live_world);
    int wandered = 0;
    for (int i = 0; i < live_world->npc_count; i++) {
        const RcNpc *npc = &live_world->npcs[i];
        if (npc->active
                && (npc->x != npc->spawn_x || npc->y != npc->spawn_y)) {
            wandered++;
        }
    }
    assert(wandered > 0);
    rc_world_destroy(live_world);

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
