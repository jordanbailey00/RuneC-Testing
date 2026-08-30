#include "route_overlay.h"

#include <assert.h>
#include <stdio.h>

static void expect_tile(RuneCRouteOverlayCursor *cursor, int x, int y) {
    int actual_x = 0;
    int actual_y = 0;
    assert(runec_route_overlay_next(cursor, &actual_x, &actual_y));
    assert(actual_x == x);
    assert(actual_y == y);
}

static void test_cardinal_segment(void) {
    int x[] = {4};
    int y[] = {1};
    RuneCRouteOverlayCursor cursor;
    runec_route_overlay_begin(&cursor, 1, 1, x, y, 0, 1);
    expect_tile(&cursor, 2, 1);
    expect_tile(&cursor, 3, 1);
    expect_tile(&cursor, 4, 1);
    assert(!runec_route_overlay_next(&cursor, &x[0], &y[0]));
}

static void test_diagonal_and_turns(void) {
    int x[] = {3, 3, 1};
    int y[] = {3, 5, 5};
    RuneCRouteOverlayCursor cursor;
    runec_route_overlay_begin(&cursor, 1, 1, x, y, 0, 3);
    expect_tile(&cursor, 2, 2);
    expect_tile(&cursor, 3, 3);
    expect_tile(&cursor, 3, 4);
    expect_tile(&cursor, 3, 5);
    expect_tile(&cursor, 2, 5);
    expect_tile(&cursor, 1, 5);
    int out_x = 0;
    int out_y = 0;
    assert(!runec_route_overlay_next(&cursor, &out_x, &out_y));
}

static void test_remaining_route_skips_reached_waypoint(void) {
    int x[] = {2, 5};
    int y[] = {2, 2};
    RuneCRouteOverlayCursor cursor;
    runec_route_overlay_begin(&cursor, 2, 2, x, y, 0, 2);
    expect_tile(&cursor, 3, 2);
    expect_tile(&cursor, 4, 2);
    expect_tile(&cursor, 5, 2);
    int out_x = 0;
    int out_y = 0;
    assert(!runec_route_overlay_next(&cursor, &out_x, &out_y));
}

static void test_long_route_is_linear_and_complete(void) {
    int x[] = {320};
    int y[] = {320};
    RuneCRouteOverlayCursor cursor;
    runec_route_overlay_begin(&cursor, 0, 0, x, y, 0, 1);
    int count = 0;
    int tile_x = 0;
    int tile_y = 0;
    while (runec_route_overlay_next(&cursor, &tile_x, &tile_y))
        count++;
    assert(count == 320);
    assert(tile_x == 320);
    assert(tile_y == 320);
}

int main(void) {
    test_cardinal_segment();
    test_diagonal_and_turns();
    test_remaining_route_skips_reached_waypoint();
    test_long_route_is_linear_and_complete();
    puts("route overlay tests passed");
    return 0;
}
