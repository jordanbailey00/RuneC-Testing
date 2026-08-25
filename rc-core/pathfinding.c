#include "pathfinding.h"
#include "collision.h"
#include <stdlib.h>
#include <string.h>

static const RcRegion *map_region_at(const RcWorldMap *map, int region_x,
                                     int region_y) {
    if (!map || map->region_count <= 0)
        return NULL;

    enum { CACHE_SIZE = 8 };
    typedef struct {
        const RcWorldMap *map;
        int region_x;
        int region_y;
        int index;
    } RegionCacheEntry;
    static _Thread_local RegionCacheEntry cache[CACHE_SIZE];

    unsigned slot = ((unsigned)region_x * 31u + (unsigned)region_y)
                  & (CACHE_SIZE - 1u);
    RegionCacheEntry *entry = &cache[slot];
    if (entry->map == map && entry->region_x == region_x
            && entry->region_y == region_y
            && entry->index >= 0 && entry->index < map->region_count) {
        const RcRegion *reg = &map->regions[entry->index];
        if (reg->loaded && reg->region_x == region_x
                && reg->region_y == region_y) {
            return reg;
        }
    }

    for (int i = 0; i < map->region_count; i++) {
        const RcRegion *reg = &map->regions[i];
        if (reg->loaded && reg->region_x == region_x
                && reg->region_y == region_y) {
            *entry = (RegionCacheEntry){map, region_x, region_y, i};
            return reg;
        }
    }
    entry->map = map;
    entry->region_x = region_x;
    entry->region_y = region_y;
    entry->index = -1;
    return NULL;
}

uint32_t rc_get_flags(const RcWorldMap *map, int x, int y, int plane) {
    if (!rc_world_tile_valid(x, y, plane)) {
        return COL_BLOCK_WALK;
    }
    // Convert world coords to region + local coords
    int region_x = x / RC_REGION_SIZE;
    int region_y = y / RC_REGION_SIZE;
    int local_x = x % RC_REGION_SIZE;
    int local_y = y % RC_REGION_SIZE;

    const RcRegion *reg = map_region_at(map, region_x, region_y);
    if (reg)
        return reg->tiles[plane][local_x][local_y].collision_flags;

    int found = 0;
    uint32_t flags = rc_collision_flags_at(x, y, plane, &found);
    if (found) return flags;
    return rc_collision_is_loaded() ? COL_BLOCK_WALK : 0;
}

bool rc_tile_blocked(const RcWorldMap *map, int x, int y, int plane) {
    uint32_t f = rc_get_flags(map, x, y, plane);
    return (f & (COL_LOC | COL_BLOCK_WALK)) != 0;
}

// Movement checks matching RSMod RouteFinding.kt routeFindSize1().
// For each direction, check the DESTINATION tile for the appropriate composite block flag.
// Diagonals also check the two adjacent cardinal tiles.
bool rc_can_move(const RcWorldMap *map, int x, int y, int dx, int dy, int plane) {
    if (!rc_world_tile_valid(x, y, plane)
            || dx < -1 || dx > 1 || dy < -1 || dy > 1
            || (dx == 0 && dy == 0)) {
        return false;
    }
    int nx = x + dx, ny = y + dy;
    if (!rc_world_coord_valid(nx) || !rc_world_coord_valid(ny)) return false;

    // Cardinals: check destination tile only
    if (dx == 0 && dy == 1)  // North
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_N);
    if (dx == 0 && dy == -1) // South
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_S);
    if (dx == 1 && dy == 0)  // East
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_E);
    if (dx == -1 && dy == 0) // West
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_W);

    // Diagonals: check destination + both adjacent cardinal tiles
    if (dx == 1 && dy == 1)  // NE
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_NE)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_E)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_N);
    if (dx == -1 && dy == 1) // NW
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_NW)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_W)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_N);
    if (dx == 1 && dy == -1) // SE
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_SE)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_E)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_S);
    if (dx == -1 && dy == -1) // SW
        return !(rc_get_flags(map, nx, ny, plane) & COL_BLOCK_SW)
            && !(rc_get_flags(map, nx, y,  plane) & COL_BLOCK_W)
            && !(rc_get_flags(map, x,  ny, plane) & COL_BLOCK_S);

    return false;
}

bool rc_has_los(const RcWorldMap *map, int x0, int y0, int x1, int y1, int plane) {
    if (!rc_world_tile_valid(x0, y0, plane)
            || !rc_world_tile_valid(x1, y1, plane)) {
        return false;
    }
    // Bresenham line — check proj blocker flags
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;

    while (x0 != x1 || y0 != y1) {
        uint32_t flags = rc_get_flags(map, x0, y0, plane);
        if (flags & COL_PROJ_BLOCK_FULL) return false;

        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
    return true;
}

RcRoute rc_find_path(const RcWorldMap *map, int start_x, int start_y,
                     int dest_x, int dest_y, int entity_size, int plane,
                     bool allow_alternative) {
    RcRoute route = {0};
    RcTileBounds start_bounds;
    if (!map || entity_size <= 0
            || !rc_tile_bounds_from_origin_size(start_x, start_y,
                                                entity_size, entity_size,
                                                plane, &start_bounds)
            || !rc_world_tile_valid(dest_x, dest_y, plane)) {
        return route;
    }
    (void)start_bounds; // Multi-size routing remains movement-audit scope.

    // BFS on collision grid
    // Search area: 512x512 centered on start. The viewer streams a 320x320
    // local slice, so a visible clicked tile can be more than 128 tiles away.
    #define SEARCH_SIZE 512
    #define SEARCH_HALF 256

    // Thread-local scratch — each thread has its own copy, so the RL
    // use case (one env per thread, thousands in parallel) is
    // race-free. Reused across every pathfind call
    // on that thread (no per-call alloc).
    static _Thread_local uint8_t visited[SEARCH_SIZE][SEARCH_SIZE];
    static _Thread_local int8_t dir_x[SEARCH_SIZE][SEARCH_SIZE];
    static _Thread_local int8_t dir_y[SEARCH_SIZE][SEARCH_SIZE];
    static _Thread_local uint16_t queue_x[SEARCH_SIZE * SEARCH_SIZE];
    static _Thread_local uint16_t queue_y[SEARCH_SIZE * SEARCH_SIZE];

    memset(visited, 0, sizeof(visited));

    int origin_x = start_x - SEARCH_HALF;
    int origin_y = start_y - SEARCH_HALF;

    int sx = start_x - origin_x;
    int sy = start_y - origin_y;
    int dx = dest_x - origin_x;
    int dy = dest_y - origin_y;

    if (dx < 0 || dx >= SEARCH_SIZE || dy < 0 || dy >= SEARCH_SIZE) {
        route.success = false;
        return route;
    }

    visited[sx][sy] = 1;
    int head = 0, tail = 0;
    queue_x[tail] = sx;
    queue_y[tail] = sy;
    tail++;

    bool found = false;
    int best_x = sx, best_y = sy;
    int best_dist = abs(dx - sx) + abs(dy - sy);

    while (head < tail) {
        int cx = queue_x[head];
        int cy = queue_y[head];
        head++;

        if (cx == dx && cy == dy) { found = true; break; }

        static const RcDirection route_order[] = {
            RC_DIRECTION_NORTH, RC_DIRECTION_EAST,
            RC_DIRECTION_SOUTH, RC_DIRECTION_WEST,
            RC_DIRECTION_NORTH_EAST, RC_DIRECTION_SOUTH_EAST,
            RC_DIRECTION_SOUTH_WEST, RC_DIRECTION_NORTH_WEST,
        };
        for (size_t d = 0; d < sizeof(route_order) / sizeof(route_order[0]);
             d++) {
            RcDirection direction = route_order[d];
            int step_x = rc_direction_dx[direction];
            int step_y = rc_direction_dy[direction];
            int nx = cx + step_x;
            int ny = cy + step_y;
            if (nx < 0 || nx >= SEARCH_SIZE || ny < 0 || ny >= SEARCH_SIZE) continue;
            if (visited[nx][ny]) continue;

            int world_x = cx + origin_x;
            int world_y = cy + origin_y;
            if (!rc_can_move(map, world_x, world_y,
                             step_x, step_y, plane)) continue;

            visited[nx][ny] = 1;
            dir_x[nx][ny] = (int8_t)-step_x;
            dir_y[nx][ny] = (int8_t)-step_y;
            queue_x[tail] = (uint16_t)nx;
            queue_y[tail] = (uint16_t)ny;
            tail++;

            int dist = abs(dx - nx) + abs(dy - ny);
            if (dist < best_dist) {
                best_dist = dist;
                best_x = nx;
                best_y = ny;
            }
        }
    }

    int end_x, end_y;
    if (found) {
        end_x = dx; end_y = dy;
        route.success = true;
    } else if (allow_alternative) {
        end_x = best_x; end_y = best_y;
        route.success = false;
        route.alternative = true;
    } else {
        route.success = false;
        return route;
    }

    // Trace backwards and keep only direction changes. Runtime movement
    // walks one tile at a time toward each waypoint, so a long straight
    // segment only needs its end tile.
    int path_x[RC_MAX_ROUTE], path_y[RC_MAX_ROUTE];
    int path_len = 0;
    int tx = end_x, ty = end_y;
    if (tx != sx || ty != sy) {
        path_x[path_len] = tx + origin_x;
        path_y[path_len] = ty + origin_y;
        path_len++;
    }
    while ((tx != sx || ty != sy) && path_len < RC_MAX_ROUTE) {
        int bx = dir_x[tx][ty];
        int by = dir_y[tx][ty];
        int px = tx + bx;
        int py = ty + by;
        if (px == sx && py == sy) break;
        int nbx = dir_x[px][py];
        int nby = dir_y[px][py];
        if (nbx != bx || nby != by) {
            path_x[path_len] = px + origin_x;
            path_y[path_len] = py + origin_y;
            path_len++;
        }
        tx = px;
        ty = py;
    }

    // Reverse into route
    route.length = path_len;
    for (int i = 0; i < path_len; i++) {
        route.waypoints_x[i] = path_x[path_len - 1 - i];
        route.waypoints_y[i] = path_y[path_len - 1 - i];
    }

    return route;
    #undef SEARCH_SIZE
    #undef SEARCH_HALF
}
