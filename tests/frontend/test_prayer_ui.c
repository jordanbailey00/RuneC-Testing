#include "../../rc-viewer/prayer_ui.h"
#include "api.h"
#include "varbits.h"
#include "io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void sprite(const char *name) {
    char path[128];
    snprintf(path, sizeof(path), "data/sprites/ui/%s.png", name);
    FILE *f = rc_asset_fopen(path, "rb");
    unsigned char header[24];
    if (!f) fprintf(stderr, "Missing Prayer sprite: %s\n", path);
    assert(f && fread(header, 1, sizeof(header), f) == sizeof(header));
    assert(!memcmp(header, "\211PNG\r\n\032\n", 8));
    assert(header[19] > 0 && header[23] > 0);
    rc_asset_close(f);
}

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_PRAYER;
    cfg.prayers_path = "data/defs/prayers.bin";
    cfg.player_actions_path = "data/defs/player_actions.bin";
    cfg.varbits_path = "data/defs/varbits.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    RuneCPrayerUiState ui = {0};
    assert(!runec_prayer_ui_sync(NULL, w) && !runec_prayer_ui_sync(&ui, NULL));
    assert(!runec_prayer_ui_ref(-1) && !runec_prayer_ui_ref(31));
    assert(!runec_prayer_overhead_asset(0));
    assert(!runec_prayer_overhead_asset(1u << RC_PRAYER_BURST_OF_STRENGTH));
    assert(runec_prayer_ui_sync(&ui, w) && ui.count == 29);
    assert(ui.available == 1u << RC_PRAYER_THICK_SKIN);
    for (int i = 0; i < 31; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        const RuneCPrayerUiRef *ref = runec_prayer_ui_ref(i);
        assert(ref && !strcmp(ref->name, def->name));
        sprite(ref->available_asset);
        sprite(ref->locked_asset);
        if (*ref->overhead_asset) sprite(ref->overhead_asset);
        if (def->unlock_varbit) assert(rc_varbit_set(w, def->unlock_varbit, def->unlock_value) == 0);
    }
    sprite("prayer_glow");
    w->player.skills.base_level[SKILL_PRAYER] = 99;
    w->player.skills.base_level[SKILL_DEFENCE] = 99;
    rc_prayer_recharge(&w->player);
    assert(runec_prayer_ui_sync(&ui, w) && ui.count == 29);
    uint32_t displayed = 0;
    for (int i = 0; i < ui.count; i++) {
        assert(!(displayed & (1u << ui.ids[i])));
        displayed |= 1u << ui.ids[i];
        if (i) assert(ui.levels[ui.ids[i-1]] <= ui.levels[ui.ids[i]]);
    }
    assert(!(displayed & (1u << RC_PRAYER_EAGLE_EYE)));
    assert(!(displayed & (1u << RC_PRAYER_MYSTIC_MIGHT)));
    assert(displayed & (1u << RC_PRAYER_DEADEYE));
    assert(displayed & (1u << RC_PRAYER_MYSTIC_VIGOUR));
    assert(!(ui.available & (1u << RC_PRAYER_REDEMPTION)));
    assert(strstr(ui.reasons[RC_PRAYER_REDEMPTION], "not implemented"));
    assert(rc_player_select_quick_prayer(w, RC_PRAYER_PROTECT_FROM_MELEE) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(runec_prayer_ui_sync(&ui, w));
    assert(ui.quick_active && ui.active == PRAYER_PROTECT_MELEE && ui.quick == ui.active);
    assert(!strcmp(runec_prayer_overhead_asset(ui.active), "headicons_prayer_0"));
    RcPrayerDef unmapped[32] = {0};
    unmapped[31].loaded = 1;
    rc_prayer_use_defs(unmapped, 32);
    assert(!runec_prayer_ui_sync(&ui, w));
    rc_prayer_use_defs(NULL, 0);
    rc_world_destroy(w);
    puts("Prayer UI: B237 assets, 29 slots, unlock replacements, requirements and quick-prayer projection passed.");
    return 0;
}
