#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "collision.h"
#include "config.h"
#include "npc.h"
#include "pathfinding.h"

#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define SPAWN_PATH RC_TEST_SOURCE_DIR "/data/spawns/world.npc-spawns.bin"
#define CTIL_PATH RC_TEST_SOURCE_DIR "/data/defs/collision_tiles.bin"
#define STATIC_GROUND_ITEM_TEST_PATH "/tmp/runec_static_ground_items_test.bin"

enum {
    GSPN_TEST_MAGIC = 0x4E505347,
    GSPN_TEST_VERSION = 1,
};

static void write_u32(FILE *f, uint32_t v) {
    assert(fwrite(&v, sizeof(v), 1, f) == 1);
}

static void write_i32(FILE *f, int32_t v) {
    assert(fwrite(&v, sizeof(v), 1, f) == 1);
}

static void write_u8(FILE *f, uint8_t v) {
    assert(fwrite(&v, sizeof(v), 1, f) == 1);
}

static void write_ground_item_row(FILE *f, uint32_t item_id,
                                  uint32_t quantity, int32_t x, int32_t y,
                                  uint8_t plane) {
    write_u32(f, item_id);
    write_u32(f, quantity);
    write_i32(f, x);
    write_i32(f, y);
    write_u8(f, plane);
    write_u8(f, 0);
}

static void write_static_ground_item_fixture(void) {
    FILE *f = fopen(STATIC_GROUND_ITEM_TEST_PATH, "wb");
    assert(f != NULL);
    write_u32(f, GSPN_TEST_MAGIC);
    write_u32(f, GSPN_TEST_VERSION);
    write_u32(f, 4);
    write_ground_item_row(f, 995, 100, 3213, 3428, 0);
    write_ground_item_row(f, 995, 50, 3213, 3428, 0);
    write_ground_item_row(f, 995, 25, 3214, 3428, 1);
    write_ground_item_row(f, 995, 10, 4000, 4000, 0);
    assert(fclose(f) == 0);
}

static int active_ground_items(const RcWorld *world, int static_only) {
    int count = 0;
    for (int i = 0; i < world->ground_item_count; i++) {
        const RcGroundItem *g = &world->ground_items[i];
        if (g->active && (!static_only || g->static_spawn))
            count++;
    }
    return count;
}

int main(void) {
    g_npc_def_count = 0;
    g_rc_collision_region_count = 0;

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_REGIONS | RC_SUB_LOOT;
    cfg.npc_defs_path = NPC_PATH;
    cfg.spawns_path = SPAWN_PATH;
    cfg.collision_tiles_path = CTIL_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(g_npc_def_count > 10000);
    assert(rc_collision_is_loaded());
    assert(rc_world_get_streaming_config(NULL) == NULL);

    const RcWorldStreamingConfig *streaming =
        rc_world_get_streaming_config(world);
    assert(streaming != NULL);
    assert(streaming->active_radius_regions == 2);
    assert(streaming->preload_radius_regions == 3);
    assert(streaming->max_cached_regions == 64);
    RcWorldStreamingTelemetry telemetry;
    assert(rc_world_get_streaming_telemetry(NULL, &telemetry) == -1);
    assert(rc_world_get_streaming_telemetry(world, NULL) == -1);
    assert(rc_world_get_streaming_telemetry(world, &telemetry) == 1);
    assert(telemetry.active_area_load_count == 0);

    RcActiveAreaRequest bad = {0};
    assert(rc_world_activate_area(world, &bad, NULL) == -1);

    RcActiveAreaRequest req = {
        .origin_x = 3072,
        .origin_y = 3264,
        .width = 320,
        .height = 320,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
        .flags = RC_ACTIVE_AREA_LOAD_COLLISION
               | RC_ACTIVE_AREA_LOAD_NPCS
               | RC_ACTIVE_AREA_CLEAR_NPCS,
        .npc_spawns_path = SPAWN_PATH,
    };
    RcActiveAreaStats stats;
    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(stats.collision_regions > 0);
    assert(stats.collision_regions <= RC_MAX_REGIONS);
    assert(stats.npc_stats.total_rows == 24110);
    assert(stats.npc_stats.matched_filter == 840);
    assert(stats.spawned_npcs == 837);
    assert(stats.streaming.active_area_load_count == 1);
    assert(stats.streaming.active_area_load_ms >= 0.0);
    assert(stats.streaming.backend_page_load_ms >= 0.0);
    assert(stats.streaming.backend_pages_loaded == stats.collision_regions);
    assert(stats.streaming.active_npcs == 837);
    assert(stats.streaming.active_ground_items == 0);
    assert(world->npc_count == 837);
    assert(world->active_area.active);
    const RcActiveArea *active = rc_world_get_active_area(world);
    assert(active == &world->active_area);
    assert(world->active_area.origin_x == req.origin_x);
    assert(world->active_area.origin_y == req.origin_y);
    assert(world->active_area.width == req.width);
    assert(world->active_area.height == req.height);
    assert(world->active_area.min_plane == 0);
    assert(world->active_area.max_plane == RC_MAX_PLANES - 1);
    assert(rc_get_flags(&world->map, 3213, 3428, 0)
           == rc_collision_flags_at(3213, 3428, 0, NULL));

    uint32_t first_gen = world->active_area.generation;
    RcNpcEnsureResult ensured;
    assert(rc_world_find_npc_near(NULL, 2215, 2872, 5358, 2, 8) == -1);
    assert(rc_world_ensure_npc_near(NULL, 2215, 2872, 5358, 2, 8,
                                    &ensured) == -1);
    assert(ensured.index == -1 && ensured.uid == -1 && ensured.spawned == 0);
    assert(rc_world_find_npc_near(world, 2215, 2872, 5358, 2, 8) == -1);
    assert(rc_world_ensure_npc_near(world, 2215, 2872, 5358, 2, 8,
                                    &ensured) >= 0);
    assert(ensured.index >= 0);
    assert(ensured.uid >= 0);
    assert(ensured.spawned == 1);
    assert(world->npc_count == 838);
    assert(rc_world_find_npc_near(world, 2215, 2872, 5358, 2, 8)
           == ensured.index);
    RcNpcEnsureResult reused;
    assert(rc_world_ensure_npc_near(world, 2215, 2872, 5358, 2, 8,
                                    &reused) >= 0);
    assert(reused.index == ensured.index);
    assert(reused.uid == ensured.uid);
    assert(reused.spawned == 0);
    assert(world->npc_count == 838);
    assert(rc_world_ensure_npc_near(world, -1, 2872, 5358, 2, 8,
                                    &reused) == -1);
    assert(reused.index == -1 && reused.uid == -1 && reused.spawned == 0);
    assert(rc_world_activate_area(world, &req, &stats) == 1);
    assert(world->active_area.generation == first_gen + 1);
    assert(world->npc_count == 837);
    assert(stats.streaming.active_area_load_count == 2);
    assert(stats.streaming.active_area_load_total_ms
           >= stats.streaming.active_area_load_ms);

    RcActiveAreaRequest collision_only = req;
    collision_only.origin_x = 3200;
    collision_only.origin_y = 3392;
    collision_only.width = 64;
    collision_only.height = 64;
    collision_only.flags = RC_ACTIVE_AREA_LOAD_COLLISION;
    assert(rc_world_activate_area(world, &collision_only, &stats) == 1);
    assert(stats.spawned_npcs == 0);
    assert(world->npc_count == 837);
    assert(world->active_area.origin_x == 3200);
    assert(world->active_area.width == 64);

    RcActiveAreaRequest default_flags = req;
    default_flags.flags = 0;
    default_flags.npc_spawns_path = NULL;
    assert(rc_world_activate_area(world, &default_flags, &stats) == 1);
    assert(stats.spawned_npcs == 837);

    write_static_ground_item_fixture();
    RcActiveAreaRequest ground_items = req;
    ground_items.max_plane = 0;
    ground_items.flags = RC_ACTIVE_AREA_CLEAR_STATIC_GROUND_ITEMS
                       | RC_ACTIVE_AREA_LOAD_STATIC_GROUND_ITEMS;
    ground_items.ground_item_spawns_path = STATIC_GROUND_ITEM_TEST_PATH;
    assert(rc_world_activate_area(world, &ground_items, &stats) == 1);
    assert(stats.ground_item_stats.total_rows == 4);
    assert(stats.ground_item_stats.matched_filter == 2);
    assert(stats.ground_item_stats.skipped_plane == 1);
    assert(stats.ground_item_stats.skipped_filtered == 1);
    assert(stats.ground_item_stats.spawned == 2);
    assert(stats.spawned_ground_items == 2);
    assert(stats.streaming.active_ground_items == 1);
    assert(active_ground_items(world, 1) == 1);
    assert(world->ground_items[0].active);
    assert(world->ground_items[0].static_spawn);
    assert(world->ground_items[0].item_id == 995);
    assert(world->ground_items[0].quantity == 150);

    assert(rc_ground_item_spawn(world, 995, 1, 3213, 3428, 0,
                                RC_GROUND_OWNER_NONE));
    assert(active_ground_items(world, 0) == 2);
    ground_items.flags = RC_ACTIVE_AREA_CLEAR_STATIC_GROUND_ITEMS;
    assert(rc_world_activate_area(world, &ground_items, &stats) == 1);
    assert(active_ground_items(world, 1) == 0);
    assert(active_ground_items(world, 0) == 1);
    assert(!world->ground_items[1].static_spawn);
    assert(rc_world_get_streaming_telemetry(world, &telemetry) == 1);
    assert(telemetry.active_ground_items == 1);
    remove(STATIC_GROUND_ITEM_TEST_PATH);

    RcActiveAreaRequest stronghold = req;
    stronghold.origin_x = 1792;
    stronghold.origin_y = 5120;
    stronghold.width = 192;
    stronghold.height = 192;
    assert(rc_world_activate_area(world, &stronghold, &stats) == 1);
    assert(stats.npc_stats.matched_filter == 150);
    assert(stats.npc_stats.skipped_instance == 40);
    assert(stats.spawned_npcs == 110);

    stronghold.flags |= RC_ACTIVE_AREA_INCLUDE_INSTANCE_NPCS;
    assert(rc_world_activate_area(world, &stronghold, &stats) == 1);
    assert(stats.npc_stats.matched_filter == 150);
    assert(stats.npc_stats.skipped_instance == 0);
    assert(stats.spawned_npcs == 150);

    rc_world_destroy(world);
    printf("test_active_area_runtime: core-owned active area loaded.\n");
    return 0;
}
