#ifndef RC_WORLD_TEST_FIXTURE_H
#define RC_WORLD_TEST_FIXTURE_H

#include "api.h"

#include <string.h>

static void rc_test_open_mapsquare(RcWorld *world, int x, int y, int plane) {
    if (!world) return;
    int region_x = x / RC_MAPSQUARE_SIZE;
    int region_y = y / RC_MAPSQUARE_SIZE;
    memset(&world->map, 0, sizeof(world->map));
    world->map.base_region_x = region_x;
    world->map.base_region_y = region_y;
    world->map.region_width = 1;
    world->map.region_height = 1;
    world->map.region_count = 1;
    world->map.regions[0].region_x = region_x;
    world->map.regions[0].region_y = region_y;
    world->map.regions[0].loaded = 1;
    world->active_area.active = true;
    world->active_area.origin_x = region_x * RC_MAPSQUARE_SIZE;
    world->active_area.origin_y = region_y * RC_MAPSQUARE_SIZE;
    world->active_area.width = RC_MAPSQUARE_SIZE;
    world->active_area.height = RC_MAPSQUARE_SIZE;
    world->active_area.min_plane = plane;
    world->active_area.max_plane = plane;
}

#endif
