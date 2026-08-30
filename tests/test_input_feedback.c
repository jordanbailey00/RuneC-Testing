#include "input_feedback.h"
#include "assets.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_click_animation_timing(void) {
    RuneCInputFeedback feedback;
    runec_input_feedback_reset(&feedback);
    assert(runec_input_feedback_click_frame(&feedback) == -1);

    runec_input_feedback_start_click(
        &feedback, RUNEC_CLICK_FEEDBACK_YELLOW, 320, 240);
    assert(feedback.click_screen_x == 320);
    assert(feedback.click_screen_y == 240);
    assert(runec_input_feedback_click_frame(&feedback) == 0);

    runec_input_feedback_advance(&feedback, 0.099f);
    assert(runec_input_feedback_click_frame(&feedback) == 0);
    runec_input_feedback_advance(&feedback, 0.002f);
    assert(runec_input_feedback_click_frame(&feedback) == 1);
    runec_input_feedback_advance(&feedback, 0.2f);
    assert(runec_input_feedback_click_frame(&feedback) == 3);
    runec_input_feedback_advance(&feedback, 0.1f);
    assert(runec_input_feedback_click_frame(&feedback) == -1);
}

static void test_new_click_replaces_animation(void) {
    RuneCInputFeedback feedback;
    runec_input_feedback_reset(&feedback);
    runec_input_feedback_start_click(
        &feedback, RUNEC_CLICK_FEEDBACK_YELLOW, 10, 20);
    runec_input_feedback_advance(&feedback, 0.25f);
    runec_input_feedback_start_click(
        &feedback, RUNEC_CLICK_FEEDBACK_RED, 30, 40);
    assert(feedback.click_style == RUNEC_CLICK_FEEDBACK_RED);
    assert(feedback.click_screen_x == 30);
    assert(feedback.click_screen_y == 40);
    assert(runec_input_feedback_click_frame(&feedback) == 0);
}

static void test_destination_waits_for_authoritative_tick(void) {
    RuneCInputFeedback feedback;
    runec_input_feedback_reset(&feedback);
    runec_input_feedback_start_destination(&feedback, 3200, 3400, 2, 41);
    assert(feedback.pending_destination_active);
    assert(feedback.pending_destination_x == 3200);
    assert(feedback.pending_destination_y == 3400);
    assert(feedback.pending_destination_plane == 2);

    runec_input_feedback_reconcile_tick(&feedback, 41);
    assert(feedback.pending_destination_active);
    runec_input_feedback_reconcile_tick(&feedback, 42);
    assert(!feedback.pending_destination_active);
}

static void test_click_assets_match_loose_and_packed_data(void) {
    for (int frame = 0; frame < 8; frame++) {
        char path[64];
        snprintf(path, sizeof(path),
                 "data/sprites/ui/clickcross_%d.png", frame);

        assert(rc_asset_reset());
        assert(rc_asset_set_backend(RC_ASSET_BACKEND_LOOSE));
        RcAssetBytes loose = rc_asset_read_all(path);
        assert(rc_asset_reset());
        assert(rc_asset_set_backend(RC_ASSET_BACKEND_PACK));
        RcAssetBytes packed = rc_asset_read_all(path);

        if (!loose.data || !packed.data || loose.size != packed.size
                || memcmp(loose.data, packed.data, loose.size) != 0) {
            fprintf(stderr,
                    "input feedback asset mismatch: %s loose=%zu packed=%zu\n",
                    path, loose.size, packed.size);
            rc_asset_bytes_free(&loose);
            rc_asset_bytes_free(&packed);
            abort();
        }
        rc_asset_bytes_free(&loose);
        rc_asset_bytes_free(&packed);
    }
    assert(rc_asset_reset());
}

int main(void) {
    test_click_animation_timing();
    test_new_click_replaces_animation();
    test_destination_waits_for_authoritative_tick();
    test_click_assets_match_loose_and_packed_data();
    puts("input feedback tests passed");
    return 0;
}
