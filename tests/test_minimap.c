#include "minimap.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_PI 3.14159265358979323846f

static void fill_world_tile(Color *pixels, int region_x, int region_y,
                            int world_x, int world_y, Color color) {
    int tile_x = world_x - region_x * RUNEC_MINIMAP_MAPSQUARE_SIZE;
    int tile_y = world_y - region_y * RUNEC_MINIMAP_MAPSQUARE_SIZE;
    int left = ((int)RUNEC_MINIMAP_SOURCE_BORDER_TILES + tile_x) * 4;
    int top = ((int)RUNEC_MINIMAP_SOURCE_BORDER_TILES
            + RUNEC_MINIMAP_MAPSQUARE_SIZE - 1 - tile_y) * 4;
    for (int y = top; y < top + 4; y++) {
        for (int x = left; x < left + 4; x++)
            pixels[x + y * RUNEC_MINIMAP_SOURCE_SIZE] = color;
    }
}

int main(void) {
    size_t count = (size_t)RUNEC_MINIMAP_SOURCE_SIZE
                 * RUNEC_MINIMAP_SOURCE_SIZE;
    Color *west = calloc(count, sizeof(*west));
    Color *east = calloc(count, sizeof(*east));
    assert(west && east);

    Color brown = {120, 80, 40, 255};
    Color green = {20, 180, 40, 255};
    Color blue = {30, 60, 210, 255};
    fill_world_tile(west, 49, 53, 3198, 3427, brown);
    fill_world_tile(west, 49, 53, 3199, 3427, green);
    fill_world_tile(east, 50, 53, 3200, 3427, blue);

    RuneCMinimapChunk chunks[] = {
        {49, 53, 0, west, RUNEC_MINIMAP_SOURCE_SIZE,
         RUNEC_MINIMAP_SOURCE_SIZE},
        {50, 53, 0, east, RUNEC_MINIMAP_SOURCE_SIZE,
         RUNEC_MINIMAP_SOURCE_SIZE},
    };
    RuneCMinimapScene scene = {chunks, 2, 0};
    Color output[RUNEC_MINIMAP_DISPLAY_SIZE * RUNEC_MINIMAP_DISPLAY_SIZE];

    runec_minimap_render(&scene, 3198.5f, 3427.5f, 0.0f, output);
    Color center = output[76 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    Color east_one_tile = output[80 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    assert(center.r == brown.r && center.g == brown.g && center.b == brown.b);
    assert(east_one_tile.r == green.r && east_one_tile.g == green.g);

    runec_minimap_render(&scene, 3199.5f, 3427.5f, 0.0f, output);
    center = output[76 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    east_one_tile = output[80 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    assert(center.r == green.r && center.g == green.g);
    assert(east_one_tile.r == blue.r && east_one_tile.b == blue.b);

    fill_world_tile(west, 49, 53, 3198, 3428, blue);
    runec_minimap_render(&scene, 3198.5f, 3427.5f, TEST_PI * 0.5f,
                         output);
    Color north_at_right = output[80 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    assert(north_at_right.r == blue.r && north_at_right.b == blue.b);

    Vector2 north = runec_minimap_rotate_offset(0.0f, 1.0f,
                                                TEST_PI * 0.5f);
    assert(fabsf(north.x - 1.0f) < 0.0001f);
    assert(fabsf(north.y) < 0.0001f);

    int tile_x = -1;
    int tile_y = -1;
    assert(runec_minimap_click_to_world(80.0f, 76.0f, 3198.5f, 3427.5f,
                                        TEST_PI * 0.5f,
                                        &tile_x, &tile_y));
    assert(tile_x == 3198 && tile_y == 3428);
    assert(!runec_minimap_click_to_world(0.0f, 0.0f, 3198.5f, 3427.5f,
                                         0.0f, &tile_x, &tile_y));
    assert(!runec_minimap_click_to_world(76.0f, 76.0f, 3198.5f, 3427.5f,
                                         0.0f, NULL, &tile_y));
    assert(!runec_minimap_click_to_world(76.0f, 76.0f, 3198.5f, 3427.5f,
                                         0.0f, &tile_x, NULL));

    runec_minimap_render(NULL, 3198.5f, 3427.5f, 0.0f, output);
    center = output[76 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    assert(center.a == 0);

    RuneCMinimapChunk invalid_chunk = {
        49, 53, 1, west, RUNEC_MINIMAP_SOURCE_SIZE,
        RUNEC_MINIMAP_SOURCE_SIZE,
    };
    RuneCMinimapScene invalid_scene = {&invalid_chunk, 1, 0};
    runec_minimap_render(&invalid_scene, 3198.5f, 3427.5f, 0.0f, output);
    center = output[76 + 76 * RUNEC_MINIMAP_DISPLAY_SIZE];
    assert(center.a == 0);
    runec_minimap_render(&scene, 3198.5f, 3427.5f, 0.0f, NULL);

    free(west);
    free(east);
    puts("minimap tests passed");
    return 0;
}
