#include "../rc-viewer/health_bars.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static RcCombatRecentHit hit(uint64_t sequence) {
    return (RcCombatRecentHit){
        .damage = 4,
        .timer = 4,
        .sequence = sequence,
        .hit_type = RC_HIT_TYPE_NORMAL,
    };
}

static void test_hit_trigger_and_timing(void) {
    RuneCHealthBarState state = {0};
    RcCombatRecentHit hits[2] = {hit(1), hit(2)};

    runec_health_bar_sync(&state, hits, 2, 17, 25);
    assert(runec_health_bar_visible(&state));
    assert(state.last_sequence == 2);
    assert(state.remaining_cycles == RUNEC_HEALTH_BAR_DISPLAY_CYCLES);
    assert(state.current_hp == 17 && state.maximum_hp == 25);

    runec_health_bar_advance(&state, 299.0f);
    assert(runec_health_bar_visible(&state));
    runec_health_bar_advance(&state, 1.0f);
    assert(!runec_health_bar_visible(&state));

    runec_health_bar_sync(&state, hits, 2, 1, 25);
    assert(!runec_health_bar_visible(&state));
    hits[0] = hit(3);
    runec_health_bar_sync(&state, hits, 1, 1, 25);
    assert(runec_health_bar_visible(&state));
}

static void test_fill_width(void) {
    RuneCHealthBarState state = {
        .remaining_cycles = 1.0f,
        .maximum_hp = 25,
    };
    state.current_hp = 25;
    assert(runec_health_bar_fill_width(&state) == 30);
    state.current_hp = 17;
    assert(runec_health_bar_fill_width(&state) == 20);
    state.current_hp = 1;
    assert(runec_health_bar_fill_width(&state) == 1);
    state.current_hp = 0;
    assert(runec_health_bar_fill_width(&state) == 0);
}

static void test_model_anchor(void) {
    float anchor = runec_health_bar_model_anchor(0.0f, 2.0f, 1.0f);
    assert(fabsf(anchor - (2.0f + 15.0f / 128.0f)) < 0.0001f);
    anchor = runec_health_bar_model_anchor(2.0f, 0.0f, 0.5f);
    assert(fabsf(anchor - (1.0f + 15.0f / 128.0f)) < 0.0001f);
}

int main(void) {
    test_hit_trigger_and_timing();
    test_fill_width();
    test_model_anchor();
    puts("health bar tests passed");
    return 0;
}
