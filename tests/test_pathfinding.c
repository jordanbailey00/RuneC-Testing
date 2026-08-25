#include "../rc-core/pathfinding.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void init_window(RcWorldMap *map, int base_x, int base_y,
                        int width, int height) {
    memset(map, 0, sizeof(*map));
    map->base_region_x = base_x;
    map->base_region_y = base_y;
    map->region_width = width;
    map->region_height = height;
}

static RcRegion *add_region(RcWorldMap *map, int region_x, int region_y) {
    assert(map->region_count < RC_MAX_REGIONS);
    RcRegion *region = &map->regions[map->region_count++];
    memset(region, 0, sizeof(*region));
    region->region_x = region_x;
    region->region_y = region_y;
    region->loaded = 1;
    return region;
}

static uint32_t *flags_at(RcWorldMap *map, int x, int y, int plane) {
    for (int i = 0; i < map->region_count; i++) {
        RcRegion *region = &map->regions[i];
        if (region->region_x == x / RC_REGION_SIZE
                && region->region_y == y / RC_REGION_SIZE) {
            return &region->tiles[plane][x % RC_REGION_SIZE]
                                  [y % RC_REGION_SIZE].collision_flags;
        }
    }
    assert(0 && "test attempted to mutate an absent region");
    return NULL;
}

static void test_map_owned_missing_policy(void) {
    static RcWorldMap unknown;
    memset(&unknown, 0, sizeof(unknown));
    assert(rc_tile_blocked(&unknown, 10, 10, 0));
    assert(!rc_can_move(&unknown, 10, 10, 1, 0, 0));
    assert(!rc_has_los(&unknown, 10, 10, 12, 10, 0));

    static RcWorldMap empty_window;
    init_window(&empty_window, 0, 0, 1, 1);
    assert(!rc_tile_blocked(&empty_window, 10, 10, 0));
    assert(rc_can_move(&empty_window, 10, 10, 1, 0, 0));

    static RcWorldMap blocked, open;
    init_window(&blocked, 0, 0, 1, 1);
    init_window(&open, 0, 0, 1, 1);
    add_region(&blocked, 0, 0);
    add_region(&open, 0, 0);
    *flags_at(&blocked, 11, 10, 0) = COL_BLOCK_WALK;
    assert(!rc_can_move(&blocked, 10, 10, 1, 0, 0));
    assert(rc_can_move(&open, 10, 10, 1, 0, 0));
}

static void test_footprint_steps(void) {
    static RcWorldMap map;
    init_window(&map, 0, 0, 1, 1);
    add_region(&map, 0, 0);

    assert(rc_can_move_rect(&map, 10, 10, 2, 2, 1, 0, 0));
    *flags_at(&map, 12, 11, 0) = COL_BLOCK_WALK;
    assert(!rc_can_move_rect(&map, 10, 10, 2, 2, 1, 0, 0));
    *flags_at(&map, 12, 11, 0) = 0;

    *flags_at(&map, 11, 12, 0) = COL_WALL_S;
    assert(!rc_can_move_rect(&map, 10, 10, 3, 2, 0, 1, 0));
    *flags_at(&map, 11, 12, 0) = 0;
    assert(rc_can_move_rect(&map, 10, 10, 3, 2, 0, 1, 0));

    *flags_at(&map, 12, 12, 0) = COL_BLOCK_WALK;
    assert(!rc_can_move_rect(&map, 10, 10, 2, 2, 1, 1, 0));
    *flags_at(&map, 12, 12, 0) = 0;
    assert(rc_can_move_rect(&map, 10, 10, 2, 2, 1, 1, 0));

    assert(!rc_can_move_rect(&map, 0, 0, 2, 2, -1, 0, 0));
    assert(!rc_can_move_rect(&map, RC_WORLD_MAX, RC_WORLD_MAX,
                             2, 2, 1, 0, 0));
}

static void test_directional_and_footprint_los(void) {
    static RcWorldMap map;
    init_window(&map, 0, 0, 1, 1);
    add_region(&map, 0, 0);

    *flags_at(&map, 11, 10, 0) = COL_PROJ_WALL_W;
    assert(!rc_has_los(&map, 10, 10, 12, 10, 0));
    assert(rc_has_los(&map, 12, 10, 10, 10, 0));

    *flags_at(&map, 11, 10, 0) = COL_PROJ_BLOCK_FULL;
    assert(!rc_has_los(&map, 10, 10, 12, 10, 0));
    assert(!rc_has_los(&map, 12, 10, 10, 10, 0));

    *flags_at(&map, 11, 10, 0) = 0;
    *flags_at(&map, 12, 10, 0) = COL_PROJ_BLOCK_FULL;
    assert(rc_has_los(&map, 10, 10, 12, 10, 0));

    *flags_at(&map, 12, 10, 0) = 0;
    *flags_at(&map, 10, 10, 0) = COL_LOC;
    assert(!rc_has_los(&map, 10, 10, 12, 10, 0));
    *flags_at(&map, 10, 10, 0) = 0;

    *flags_at(&map, 11, 10, 0) = COL_PROJ_BLOCK_FULL;
    assert(rc_has_los_rect(&map, 10, 10, 2, 2,
                           12, 11, 1, 1, 0));
}

static void walk_route(const RcWorldMap *map, int start_x, int start_y,
                       int width, int height, const RcRoute *route) {
    int x = start_x, y = start_y;
    for (int i = 0; i < route->length; i++) {
        while (x != route->waypoints_x[i] || y != route->waypoints_y[i]) {
            int dx = route->waypoints_x[i] - x;
            int dy = route->waypoints_y[i] - y;
            dx = dx > 0 ? 1 : dx < 0 ? -1 : 0;
            dy = dy > 0 ? 1 : dy < 0 ? -1 : 0;
            assert(rc_can_move_rect(map, x, y, width, height, dx, dy, 0));
            x += dx;
            y += dy;
        }
    }
    assert(x == route->reached_x && y == route->reached_y);
}

static void test_route_status_cost_and_reach(void) {
    static RcWorldMap map;
    init_window(&map, 0, 0, 1, 1);
    add_region(&map, 0, 0);
    *flags_at(&map, 5, 5, 0) = COL_BLOCK_WALK;

    RcRouteTarget point = rc_route_target_point(7, 5);
    RcRoute route = rc_find_route(&map, 3, 5, 1, 1, 0, &point, false);
    assert(route.status == RC_ROUTE_EXACT);
    assert(route.length > 0 && route.cost >= 4);
    assert(route.cost > route.length);
    assert(route.reached_x == 7 && route.reached_y == 5);
    walk_route(&map, 3, 5, 1, 1, &route);

    RcRoute arrived = rc_find_route(&map, 7, 5, 1, 1, 0, &point, false);
    assert(arrived.status == RC_ROUTE_ALREADY_ARRIVED);
    assert(arrived.length == 0 && arrived.cost == 0);

    RcRouteTarget blocked_target = rc_route_target_point(5, 5);
    RcRoute blocked = rc_find_route(
        &map, 3, 5, 1, 1, 0, &blocked_target, false);
    assert(blocked.status == RC_ROUTE_BLOCKED && blocked.length == 0);
    RcRoute alternative = rc_find_route(
        &map, 3, 5, 1, 1, 0, &blocked_target, true);
    assert(alternative.status == RC_ROUTE_ALTERNATIVE);
    assert(alternative.length > 0);
    assert(!(alternative.reached_x == 5 && alternative.reached_y == 5));

    RcRouteTarget rectangle = rc_route_target_rectangle(
        30, 30, 3, 2, 1, 1, false, false);
    RcRoute approach = rc_find_route(
        &map, 20, 30, 1, 1, 0, &rectangle, false);
    assert(approach.status == RC_ROUTE_EXACT);
    assert(approach.reached_x == 29 && approach.reached_y == 30);

    RcRouteTarget wall = rc_route_target_point(40, 40);
    wall.kind = RC_ROUTE_REACH_WALL;
    wall.shape = 0;
    wall.rotation = 0;
    wall.max_distance = 1;
    RcRoute wall_route = rc_find_route(
        &map, 35, 40, 1, 1, 0, &wall, false);
    assert(wall_route.status == RC_ROUTE_EXACT);
    assert(wall_route.reached_x == 39 && wall_route.reached_y == 40);

    RcRoute invalid = rc_find_route(
        &map, -1, 0, 1, 1, 0, &point, false);
    assert(invalid.status == RC_ROUTE_FAILED);
}

static void test_source_connected_route_limit(void) {
    static RcWorldMap map;
    init_window(&map, 0, 0, 1, 2);
    RcRegion *south = add_region(&map, 0, 0);
    RcRegion *north = add_region(&map, 0, 1);
    for (int plane = 0; plane < RC_MAX_PLANES; plane++) {
        for (int x = 0; x < RC_REGION_SIZE; x++) {
            for (int y = 0; y < RC_REGION_SIZE; y++) {
                south->tiles[plane][x][y].collision_flags = COL_BLOCK_WALK;
                north->tiles[plane][x][y].collision_flags = COL_BLOCK_WALK;
            }
        }
    }

    const int rows = 36;
    for (int row = 0; row < rows; row++) {
        int y = 10 + row * 2;
        for (int x = 10; x <= 40; x++) *flags_at(&map, x, y, 0) = 0;
        if (row + 1 < rows) {
            int connector_x = row % 2 == 0 ? 40 : 10;
            *flags_at(&map, connector_x, y + 1, 0) = 0;
        }
    }
    int target_x = (rows - 1) % 2 == 0 ? 40 : 10;
    int target_y = 10 + (rows - 1) * 2;
    RcRouteTarget target = rc_route_target_point(target_x, target_y);
    RcRoute route = rc_find_route(&map, 10, 10, 1, 1, 0, &target, false);
    assert(route.status == RC_ROUTE_PARTIAL);
    assert(route.length == RC_MAX_ROUTE);
    assert(route.segment_cost < route.cost);
    assert(!(route.reached_x == target_x && route.reached_y == target_y));
    walk_route(&map, 10, 10, 1, 1, &route);
}

int main(void) {
    test_map_owned_missing_policy();
    test_footprint_steps();
    test_directional_and_footprint_los();
    test_route_status_cost_and_reach();
    test_source_connected_route_limit();
    puts("All pathfinding contract tests passed.");
    return 0;
}
