#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "collision.h"
#include "config.h"
#include "pathfinding.h"

#define CTPI_PATH \
    RC_TEST_SOURCE_DIR "/data/regions/world.collision-tiles.indexed.bin"
#define BAD_PATH "/tmp/runec_bad_collision.bin"
#define FIXTURE_PATH "/tmp/runec_collision_index_fixture.bin"
#define BAD_PAGE_PATH "/tmp/runec_bad_collision_page.bin"

enum {
    CTPI_TEST_MAGIC = 0x49505443,
    CTPI_TEST_VERSION = 1,
    CTPI_TEST_RECORD_SIZE = 8,
    CTPI_TEST_MAPSQUARES = 65536,
};

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_row(FILE *f, uint8_t local_x, uint8_t local_y,
                      uint8_t plane, uint32_t flags) {
    assert(fputc(local_x, f) != EOF);
    assert(fputc(local_y, f) != EOF);
    assert(fputc(plane, f) != EOF);
    assert(fputc(0, f) != EOF);
    write_u32(f, flags);
}

static void write_bad_header(void) {
    uint32_t bad[4] = {0, 1, 0, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

static void write_fixture(const char *path, int bad_page) {
    const uint32_t first_ms = (50u << 8) | 53u;
    const uint32_t second_ms = (62u << 8) | 62u;
    FILE *f = fopen(path, "wb");
    assert(f != NULL);
    write_u32(f, CTPI_TEST_MAGIC);
    write_u32(f, CTPI_TEST_VERSION);
    write_u32(f, 3);
    write_u32(f, CTPI_TEST_RECORD_SIZE);
    write_u32(f, 2);
    write_u32(f, 2);
    write_u32(f, 1);
    write_u32(f, 0);
    write_u32(f, 0);

    uint32_t first = 0;
    for (uint32_t mapsquare = 0; mapsquare < CTPI_TEST_MAPSQUARES;
         mapsquare++) {
        uint32_t count = mapsquare == first_ms ? 2u
                       : mapsquare == second_ms ? 1u : 0u;
        write_u32(f, first);
        write_u32(f, count);
        first += count;
    }
    assert(first == 3);
    write_row(f, bad_page ? 64 : 0, 3, 0, COL_BLOCK_WALK);
    write_row(f, 33, 44, 1, COL_WALL_S);
    write_row(f, 32, 32, 0, COL_LOC);
    assert(fclose(f) == 0);
}

int main(void) {
    RcWorldMap empty = {0};
    assert(rc_get_flags(&empty, 0, 0, 0)
           == (COL_BLOCK_WALK | COL_PROJ_BLOCK_FULL));

    write_bad_header();
    assert(rc_load_collision_tiles(NULL) == -1);
    assert(rc_load_collision_tiles("/missing/collision-tiles.indexed.bin") == -1);
    assert(rc_load_collision_tiles(BAD_PATH) == -1);
    assert(rc_load_collision_tiles(CTPI_PATH) > 2000);
    assert(rc_collision_is_loaded());

    int found = 0;
    uint32_t blocked = rc_collision_flags_at(3072, 3395, 0, &found);
    assert(found == 1);
    assert(blocked & COL_BLOCK_WALK);
    assert(rc_get_flags(&empty, 3072, 3395, 0)
           == (COL_BLOCK_WALK | COL_PROJ_BLOCK_FULL));

    uint32_t open = rc_collision_flags_at(3213, 3428, 0, &found);
    assert(found == 1);
    RcWorldMap resident = {0};
    assert(rc_collision_populate_map_rect(
        &resident, 3072, 3328, 3263, 3455) > 0);
    assert(rc_get_flags(&resident, 3072, 3395, 0) == blocked);
    assert(rc_get_flags(&resident, 3213, 3428, 0) == open);

    uint32_t bridge_south_rail = rc_collision_flags_at(3105, 3420, 0, &found);
    assert(found == 1);
    assert(bridge_south_rail & COL_WALL_S);
    assert(rc_can_move(&resident, 3104, 3420, 1, 0, 0));
    assert(!rc_can_move(&resident, 3105, 3419, 0, 1, 0));
    assert(!rc_can_move(&resident, 3105, 3421, 0, 1, 0));

    (void)rc_collision_flags_at(0, 0, 0, &found);
    assert(found == 0);
    assert(rc_get_flags(&empty, 0, 0, 0)
           == (COL_BLOCK_WALK | COL_PROJ_BLOCK_FULL));
    assert(rc_get_flags(&empty, -1, 0, 0)
           == (COL_BLOCK_WALK | COL_PROJ_BLOCK_FULL));

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_REGIONS;
    cfg.collision_tiles_path = CTPI_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->enabled & RC_SUB_REGIONS);
    assert(g_rc_collision_region_count > 2000);
    rc_world_destroy(world);

    write_fixture(FIXTURE_PATH, 0);
    assert(rc_load_collision_tiles(FIXTURE_PATH) == 2);
    assert(rc_collision_set_cache_limit(0) == -1);
    assert(rc_collision_set_cache_limit(1) == 1);
    assert(rc_collision_set_cache_limit(CTPI_TEST_MAPSQUARES + 1) == 1);
    assert(rc_collision_set_cache_limit(1) == 1);

    RcWorldMap map = {0};
    RcCollisionLoadStats stats;
    assert(rc_collision_populate_map_rect_stats(
        &map, 3200, 3392, 3263, 3455, &stats) == 1);
    assert(stats.total_rows == 3);
    assert(stats.occupied_pages == 2);
    assert(stats.pages_requested == 1);
    assert(stats.pages_loaded == 1);
    assert(stats.rows_loaded == 2);
    assert(stats.pages_resident == 1);
    assert(stats.rows_resident == 2);
    assert(rc_get_flags(&map, 3200, 3395, 0) == COL_BLOCK_WALK);

    assert(rc_collision_populate_map_rect_stats(
        &map, 3200, 3392, 3263, 3455, &stats) == 1);
    assert(stats.pages_requested == 1);
    assert(stats.pages_loaded == 0);
    assert(stats.rows_loaded == 0);
    assert(rc_collision_populate_map_rect(
        &map, 3200, 3392, 3263, 3455) == 1);

    uint32_t distant = rc_collision_flags_at(4000, 4000, 0, &found);
    assert(found == 1 && distant == COL_LOC);
    assert(rc_collision_populate_map_rect_stats(
        &map, 3200, 3392, 3263, 3455, &stats) == 1);
    assert(stats.pages_loaded == 1);
    assert(stats.pages_resident == 1);
    assert(stats.rows_resident == 2);

    assert(rc_collision_populate_map_rect_stats(
        &map, 20000, 20000, 20063, 20063, &stats) == 0);
    assert(map.region_count == 0);
    assert(stats.pages_resident == 1);
    assert(rc_collision_populate_map_rect_stats(
        &map, 10, 10, 9, 9, &stats) == -1);

    write_fixture(BAD_PAGE_PATH, 1);
    assert(rc_load_collision_tiles(BAD_PAGE_PATH) == 2);
    assert(rc_collision_populate_map_rect_stats(
        &map, 3200, 3392, 3263, 3455, &stats) == -1);
    (void)rc_collision_flags_at(3200, 3395, 0, &found);
    assert(found == 0);

    remove(BAD_PATH);
    remove(FIXTURE_PATH);
    remove(BAD_PAGE_PATH);

    return 0;
}
