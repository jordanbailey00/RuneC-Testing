#include "context_menu.h"
#include "assets.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_osrs_dimensions_and_anchor(void) {
    assert(RUNEC_CONTEXT_MENU_FONT_SIZE == 16.0f);
    assert(RUNEC_CONTEXT_MENU_DISMISS_MARGIN == 10);
    RuneCContextMenuLayout layout = runec_context_menu_layout(
        400, 200, 800, 600, 92, 4);
    assert(layout.width == 104);
    assert(layout.height == 104);
    assert(layout.x == 348);
    assert(layout.y == 200);
}

static void test_osrs_dismiss_margin(void) {
    RuneCContextMenuLayout layout = runec_context_menu_layout(
        400, 200, 800, 600, 92, 4);
    assert(runec_context_menu_contains_margin(&layout, 338, 190, 10));
    assert(runec_context_menu_contains_margin(&layout, 462, 314, 10));
    assert(!runec_context_menu_contains_margin(&layout, 337, 200, 10));
    assert(!runec_context_menu_contains_margin(&layout, 348, 315, 10));
}

static void test_screen_clamping(void) {
    RuneCContextMenuLayout top_left = runec_context_menu_layout(
        2, -4, 200, 100, 92, 4);
    assert(top_left.x == 0);
    assert(top_left.y == 0);

    RuneCContextMenuLayout bottom_right = runec_context_menu_layout(
        198, 135, 200, 140, 92, 4);
    assert(bottom_right.x == 96);
    assert(bottom_right.y == 36);
}

static void test_action_hit_rows(void) {
    RuneCContextMenuLayout layout = runec_context_menu_layout(
        400, 200, 800, 600, 92, 4);
    assert(runec_context_menu_contains(&layout, 348, 200));
    assert(!runec_context_menu_contains(&layout, 347, 200));
    assert(runec_context_menu_action_at(&layout, 4, 360, 223) == -1);
    assert(runec_context_menu_action_at(&layout, 4, 360, 224) == 0);
    assert(runec_context_menu_action_at(&layout, 4, 360, 242) == 0);
    assert(runec_context_menu_action_at(&layout, 4, 360, 243) == 1);
    assert(runec_context_menu_action_at(&layout, 4, 360, 299) == 3);
    assert(runec_context_menu_action_at(&layout, 4, 360, 300) == -1);
}

static void test_bold_font_matches_loose_and_packed_data(void) {
    const char *path = "data/fonts/runescape_bold.ttf";
    assert(rc_asset_reset());
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_LOOSE));
    RcAssetBytes loose = rc_asset_read_all(path);
    assert(rc_asset_reset());
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_PACK));
    RcAssetBytes packed = rc_asset_read_all(path);
    if (!loose.data || !packed.data || loose.size != packed.size
            || memcmp(loose.data, packed.data, loose.size) != 0) {
        fprintf(stderr,
                "context menu font mismatch: %s loose=%zu packed=%zu\n",
                path, loose.size, packed.size);
        rc_asset_bytes_free(&loose);
        rc_asset_bytes_free(&packed);
        assert(0);
    }
    rc_asset_bytes_free(&loose);
    rc_asset_bytes_free(&packed);
    assert(rc_asset_reset());
}

int main(void) {
    test_osrs_dimensions_and_anchor();
    test_osrs_dismiss_margin();
    test_screen_clamping();
    test_action_hit_rows();
    test_bold_font_matches_loose_and_packed_data();
    puts("context menu tests passed");
    return 0;
}
