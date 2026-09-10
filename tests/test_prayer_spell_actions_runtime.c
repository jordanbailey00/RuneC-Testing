#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <string.h>

#include "api.h"
#include "combat.h"
#include "config.h"
#include "npc.h"
#include "player_actions.h"
#include "prayer.h"
#include "spells.h"
#include "combat_formula.h"
#include "varbits.h"

#define PRAY_PATH RC_TEST_SOURCE_DIR "/data/defs/prayers.bin"
#define SPEL_PATH RC_TEST_SOURCE_DIR "/data/defs/spells.bin"
#define PACT_PATH RC_TEST_SOURCE_DIR "/data/defs/player_actions.bin"
#define NDEF_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define BAD_PATH "/tmp/runec_bad_pray_spell_action.bin"

static void write_bad_header(void) {
    uint32_t bad[3] = {0, 1, 0};
    FILE *f = fopen(BAD_PATH, "wb");
    assert(f != NULL);
    assert(fwrite(bad, sizeof(bad), 1, f) == 1);
    fclose(f);
}

static RcWorld *prayer_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_PRAYER;
    cfg.npc_defs_path = NULL;
    cfg.prayers_path = PRAY_PATH;
    cfg.player_actions_path = PACT_PATH;
    cfg.varbits_path = RC_TEST_SOURCE_DIR "/data/defs/varbits.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    return w;
}

static void unlock_prayers(RcWorld *w) {
    w->player.skills.base_level[SKILL_PRAYER] = 99;
    w->player.skills.base_level[SKILL_DEFENCE] = 99;
    for (int i = 0; i < RC_MAX_PRAYER_DEFS; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        if (def && def->unlock_varbit)
            assert(rc_varbit_set(w, def->unlock_varbit, def->unlock_value) == 0);
    }
    rc_prayer_recharge(&w->player);
}

static RcPrayerResult toggle(RcWorld *w, int id) {
    assert(rc_player_set_prayer(w, id) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    return w->player.prayer_outcome.code;
}

static void observe_toggle(RcWorld *w, int event, const void *payload, void *ctx) {
    assert(event == RC_EVT_PRAYER_TOGGLED);
    const RcPayloadPrayerToggled *change = payload;
    assert(!!(w->player.active_prayers & rc_prayer_bit(change->prayer_id)) == change->enabled);
    (*(int *)ctx)++;
}

static void test_transition_edges(void) {
    RcWorld *w = prayer_world();
    RcPlayer *p = &w->player;
    unlock_prayers(w);
    int events = 0;
    assert(rc_event_subscribe(w, RC_EVT_PRAYER_TOGGLED, observe_toggle, &events) == 0);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK && events == 1);
    rc_world_tick(w);
    assert(p->prayer_drain_counter == 1);
    assert(toggle(w, RC_PRAYER_BURST_OF_STRENGTH) == RC_PRAYER_OK);
    assert(p->prayer_drain_counter == 3);
    assert(toggle(w, RC_PRAYER_BURST_OF_STRENGTH) == RC_PRAYER_OK);
    assert(p->prayer_drain_counter == 4 && events == 3);
    p->skills.base_level[SKILL_PRAYER] = 0;
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    assert(!p->active_prayers && !p->prayer_drain_counter && events == 4);
    unlock_prayers(w);
    assert(rc_player_select_quick_prayer(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(rc_player_select_quick_prayer(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(!p->quick_prayers && p->active_prayers == PRAYER_THICK_SKIN && p->quick_prayers_active);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(!p->active_prayers && !p->quick_prayers_active);
    assert(rc_player_select_quick_prayer(w, -1) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(p->prayer_outcome.code == RC_PRAYER_INVALID && !p->quick_prayers);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    p->equipment_bonuses[13] = INT_MAX;
    p->prayer_drain_counter = INT_MAX;
    rc_world_tick(w);
    assert(p->prayer_drain_counter == (int64_t)INT_MAX + 1);
    rc_prayer_disable_all(w, RC_PRAYER_FORCED_OFF);
    assert(!p->active_prayers && p->prayer_outcome.code == RC_PRAYER_FORCED_OFF);
    w->enabled = 0;
    assert(rc_prayer_available(w, 0) == RC_PRAYER_DISABLED);
    w->enabled = RC_SUB_PRAYER;
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    p->current_prayer_points = 0;
    rc_world_tick(w);
    assert(p->prayer_outcome.code == RC_PRAYER_DEPLETED && !p->active_prayers);
    p->current_prayer_points = 1000;
    rc_prayer_restore_points(p, 5);
    assert(p->current_prayer_points == 1000);
    rc_prayer_drain_points(w, 0);
    rc_prayer_drain_tenths(w, -1);
    rc_prayer_restore_points(p, -1);
    assert(p->current_prayer_points == 1000);
    assert(rc_player_set_prayer(NULL, 0) == RC_PRAYER_INVALID);
    assert(rc_player_select_quick_prayer(NULL, 0) == RC_PRAYER_INVALID);
    assert(rc_player_toggle_quick_prayers(NULL) == RC_PRAYER_INVALID);
    assert(rc_prayer_set_active(NULL, 0) == RC_PRAYER_INVALID);
    for (int i = 0; i < RC_MAX_PLAYER_COMMANDS; i++)
        assert(rc_player_set_prayer(w, 0) == RC_PRAYER_QUEUED);
    assert(rc_player_set_prayer(w, 0) == RC_PRAYER_QUEUE_REJECTED);
    rc_world_tick(w);
    for (int i = RC_PRAYER_OK; i <= RC_PRAYER_FORCED_OFF; i++)
        assert(rc_prayer_result_message(i)[0] || rc_prayer_result_accepted(i));
    assert(strcmp(rc_prayer_result_message(-1), "Invalid prayer result.") == 0);
    rc_world_destroy(w);
}

static void test_collision_matrix(void) {
    RcWorld *w = prayer_world();
    unlock_prayers(w);
    for (int a = 0; a < 31; a++) {
        if (rc_prayer_available(w, a) != RC_PRAYER_OK) continue;
        for (int b = 0; b < 31; b++) {
            if (a == b || rc_prayer_available(w, b) != RC_PRAYER_OK) continue;
            rc_prayer_recharge(&w->player);
            assert(rc_prayer_set_active(w, rc_prayer_bit(a)) == RC_PRAYER_OK);
            assert(toggle(w, b) == RC_PRAYER_OK);
            uint32_t expected = rc_prayer_bit(b);
            if (!(rc_prayer_def_get(a)->groups & rc_prayer_def_get(b)->groups))
                expected |= rc_prayer_bit(a);
            assert(w->player.active_prayers == expected);
        }
    }
    rc_world_destroy(w);
}

static void test_requirements_and_lifecycle(void) {
    RcWorld *w = prayer_world();
    RcPlayer *p = &w->player;
    assert(p->current_prayer_points == 10);
    assert(toggle(w, -1) == RC_PRAYER_INVALID);
    assert(rc_player_last_command_result(w, NULL) == RC_COMMAND_RESULT_REJECTED_INVALID);
    assert(toggle(w, RC_PRAYER_PIETY) == RC_PRAYER_LEVEL);
    p->skills.base_level[SKILL_PRAYER] = 99;
    assert(toggle(w, RC_PRAYER_PIETY) == RC_PRAYER_DEFENCE);
    p->skills.base_level[SKILL_DEFENCE] = 70;
    assert(toggle(w, RC_PRAYER_PIETY) == RC_PRAYER_LOCKED);
    unlock_prayers(w);
    w->members_world = false;
    assert(toggle(w, RC_PRAYER_PIETY) == RC_PRAYER_MEMBERS);
    w->members_world = true;
    assert(toggle(w, RC_PRAYER_PIETY) == RC_PRAYER_OK);
    assert(toggle(w, RC_PRAYER_MYSTIC_VIGOUR) == RC_PRAYER_OK);
    assert(p->active_prayers == PRAYER_MYSTIC_VIGOUR);
    assert(toggle(w, RC_PRAYER_MYSTIC_MIGHT) == RC_PRAYER_REPLACED);
    assert(toggle(w, RC_PRAYER_PROTECT_ITEM) == RC_PRAYER_UNSUPPORTED);
    assert(toggle(w, RC_PRAYER_REDEMPTION) == RC_PRAYER_UNSUPPORTED);
    assert(toggle(w, RC_PRAYER_RETRIBUTION) == RC_PRAYER_UNSUPPORTED);
    assert(toggle(w, RC_PRAYER_BURST_OF_STRENGTH) == RC_PRAYER_OK);
    assert(toggle(w, RC_PRAYER_SHARP_EYE) == RC_PRAYER_OK);
    assert(p->active_prayers == PRAYER_SHARP_EYE);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    assert(p->active_prayers == (PRAYER_SHARP_EYE | PRAYER_THICK_SKIN));
    uint32_t old = p->active_prayers;
    assert(rc_prayer_set_active(w, PRAYER_PIETY | PRAYER_MYSTIC_VIGOUR) == RC_PRAYER_CONFLICT);
    assert(p->active_prayers == old);
    assert(rc_prayer_set_active(w, 1u << 31) == RC_PRAYER_INVALID);
    assert(p->active_prayers == old);
    p->current_hp = 0;
    rc_world_tick(w);
    // Prayer-only worlds still clean up at zero HP without the combat death stage.
    assert(p->active_prayers == 0 && p->prayer_drain_counter == 0);
    int points = p->current_prayer_points;
    rc_world_tick(w);
    assert(p->current_prayer_points == points);
    assert(rc_player_set_prayer(w, 0) == RC_PRAYER_DEAD);
    assert(rc_prayer_set_active(w, PRAYER_THICK_SKIN) == RC_PRAYER_DEAD);
    assert(rc_world_reset(w));
    assert(p->current_prayer_points == 10 && !p->active_prayers && !p->quick_prayers);
    assert(w->members_world);
    rc_world_destroy(w);
}

static void test_drain_resource_and_quick(void) {
    RcWorld *w = prayer_world();
    RcPlayer *p = &w->player;
    unlock_prayers(w);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    assert(p->prayer_drain_counter == 0);
    for (int i = 0; i < 60; i++) rc_world_tick(w);
    assert(p->current_prayer_points == 990 && p->prayer_drain_counter == 60);
    rc_world_tick(w);
    assert(p->current_prayer_points == 980 && p->prayer_drain_counter == 1);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    assert(p->prayer_drain_counter == 0);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    assert(p->prayer_drain_counter == 0);
    rc_prayer_drain_points(w, 3);
    assert(p->current_prayer_points == 950);
    rc_prayer_restore_points(p, 3);
    assert(p->current_prayer_points == 980);
    rc_prayer_restore_points(p, INT_MAX);
    assert(p->current_prayer_points == 990);
    rc_prayer_restore_points(p, 5);
    assert(p->current_prayer_points == 990);
    rc_prayer_drain_tenths(w, 1);
    assert(p->current_prayer_points == 989);
    rc_prayer_drain_points(w, INT_MAX);
    assert(!p->current_prayer_points && !p->active_prayers && !p->prayer_drain_counter);
    uint64_t depleted = p->prayer_outcome.sequence;
    rc_prayer_drain_points(w, 1);
    assert(p->prayer_outcome.sequence == depleted);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_EMPTY);
    rc_prayer_recharge(p);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(p->prayer_outcome.code == RC_PRAYER_NO_SELECTION);
    assert(rc_player_select_quick_prayer(w, RC_PRAYER_PIETY) == RC_PRAYER_QUEUED);
    assert(rc_player_select_quick_prayer(w, RC_PRAYER_PROTECT_FROM_MELEE) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(p->quick_prayers == (PRAYER_PIETY | PRAYER_PROTECT_MELEE));
    assert(!p->active_prayers);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(p->quick_prayers_active && p->active_prayers == p->quick_prayers);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(!p->quick_prayers_active && !p->active_prayers && !p->prayer_drain_counter);
    assert(rc_varbit_set(w, 3909, 0) == 0);
    assert(rc_player_toggle_quick_prayers(w) == RC_PRAYER_QUEUED);
    rc_world_tick(w);
    assert(p->prayer_outcome.code == RC_PRAYER_LOCKED && !p->active_prayers);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    p->equipment_bonuses[13] = INT_MIN;
    rc_world_tick(w);
    rc_world_tick(w);
    assert(p->current_prayer_points == 980);
    assert(toggle(w, RC_PRAYER_THICK_SKIN) == RC_PRAYER_OK);
    rc_world_destroy(w);
}

static void test_prayer_stats(void) {
    RcWorld *w = prayer_world();
    RcPlayer *p = &w->player;
    unlock_prayers(w);
    p->current_hp = 50;
    p->skills.base_level[SKILL_ATTACK] = 50;
    p->skills.boosted_level[SKILL_ATTACK] = 40;
    p->skills.base_level[SKILL_STRENGTH] = 50;
    p->skills.boosted_level[SKILL_STRENGTH] = 60;
    assert(toggle(w, RC_PRAYER_RAPID_HEAL) == RC_PRAYER_OK);
    for (int i = 0; i < 49; i++) rc_world_tick(w);
    assert(p->current_hp == 50);
    rc_world_tick(w);
    assert(p->current_hp == 60);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 40);
    assert(toggle(w, RC_PRAYER_RAPID_RESTORE) == RC_PRAYER_OK);
    assert(toggle(w, RC_PRAYER_PRESERVE) == RC_PRAYER_OK);
    for (int i = 0; i < 100; i++) rc_world_tick(w);
    assert(p->skills.boosted_level[SKILL_ATTACK] == 42);
    assert(p->skills.boosted_level[SKILL_STRENGTH] == 60);
    assert(toggle(w, RC_PRAYER_PRESERVE) == RC_PRAYER_OK);
    for (int i = 0; i < 100; i++) rc_world_tick(w);
    assert(p->skills.boosted_level[SKILL_STRENGTH] == 59);
    p->skills.boosted_level[SKILL_STRENGTH] = 10;
    p->skills.boosted_level[SKILL_RANGED] = 10;
    assert(rc_prayer_set_active(w, 0) == RC_PRAYER_OK);
    int strength = rc_player_effective_strength_level(p);
    int ranged = rc_player_effective_ranged_strength_level(p);
    assert(toggle(w, RC_PRAYER_BURST_OF_STRENGTH) == RC_PRAYER_OK);
    assert(rc_player_effective_strength_level(p) == strength + 1);
    assert(toggle(w, RC_PRAYER_SHARP_EYE) == RC_PRAYER_OK);
    assert(rc_player_effective_ranged_strength_level(p) == ranged + 1);
    assert(rc_prayer_magic_damage_bonus(1u << RC_PRAYER_MYSTIC_LORE) == 1);
    assert(rc_prayer_magic_damage_bonus(PRAYER_MYSTIC_MIGHT) == 2);
    assert(rc_prayer_set_active(w, 0) == RC_PRAYER_OK);
    p->skills.boosted_level[SKILL_STRENGTH] = 60;
    p->stat_boost_counter = 0;
    assert(toggle(w, RC_PRAYER_PRESERVE) == RC_PRAYER_OK);
    for (int i = 0; i < 24; i++) rc_world_tick(w);
    assert(p->preserve_timer == 1);
    rc_world_tick(w);
    assert(p->preserve_timer == 0 && p->stat_boost_counter == 0);
    for (int i = 0; i < 149; i++) rc_world_tick(w);
    assert(p->skills.boosted_level[SKILL_STRENGTH] == 60);
    rc_world_tick(w);
    assert(p->skills.boosted_level[SKILL_STRENGTH] == 59);
    rc_world_destroy(w);
}

static void test_definition_validation(void) {
    RcPrayerDef defs[RC_MAX_PRAYER_DEFS];
    memset(defs, 0x55, sizeof(defs));
    for (int id = 0; id <= 32; id += 32) {
        FILE *f = fopen(BAD_PATH, "wb");
        assert(f);
        uint32_t header[] = {0x59415250u, 2, id == 0 ? 2 : 1};
        unsigned char row[21] = {0};
        row[0] = id; row[1] = 1; row[2] = 1; row[10] = 255;
        row[19] = 1; row[20] = 'X';
        assert(fwrite(header, sizeof(header), 1, f) == 1);
        for (unsigned i = 0; i < header[2]; i++) assert(fwrite(row, sizeof(row), 1, f) == 1);
        fclose(f);
        int count = 99;
        assert(rc_load_prayers_into(BAD_PATH, defs, RC_MAX_PRAYER_DEFS, &count) == -1);
        assert(count == 0 && defs[0].loaded == 0x55);
    }
    const int groups[31] = {1,2,4,6,6,1,2,4,0,0,0,6,6,1,2,4,8,8,8,6,6,8,8,8,0,7,6,6,7,7,7};
    for (int i = 0; i < 31; i++) {
        const RcPrayerDef *def = rc_prayer_def_get(i);
        assert(def && def->groups == groups[i]);
    }
    // Independently encoded v2 row: malformed header, fields, name and replacement.
    for (int problem = 0; problem < 11; problem++) {
        uint32_t header[] = {0x59415250u, 2, 1};
        unsigned char row[21] = {0, 1, 1};
        row[10] = 255; row[19] = 1; row[20] = 'X';
        int bytes = sizeof(row);
        if (problem == 0) header[1] = 1;
        if (problem == 1) header[2] = 33;
        if (problem == 2) bytes = 8;
        if (problem == 3) bytes = 20;
        if (problem == 4) row[1] = 0;
        if (problem == 5) row[3] = 128;
        if (problem == 6) row[10] = 0;
        if (problem == 7) row[10] = 31;
        if (problem == 8) row[20] = 0;
        if (problem == 9) row[11] = 255;
        FILE *f = fopen(BAD_PATH, "wb");
        assert(f && fwrite(header, sizeof(header), 1, f) == 1);
        assert(fwrite(row, 1, bytes, f) == (size_t)bytes);
        if (problem == 10) assert(fputc(0, f) != EOF);
        fclose(f);
        assert(rc_load_prayers_into(BAD_PATH, defs, 32, NULL) == -1);
        assert(defs[0].loaded == 0x55);
    }
    assert(rc_load_prayers_into(PRAY_PATH, NULL, 32, NULL) == -1);
    assert(rc_load_prayers_into(PRAY_PATH, defs, 0, NULL) == -1);
    assert(rc_load_prayers_into(PRAY_PATH, defs, 1, NULL) == -1);
}

int main(void) {
    write_bad_header();
    assert(rc_load_prayers(NULL) == -1);
    assert(rc_load_spells(NULL) == -1);
    assert(rc_load_player_actions(NULL) == -1);
    assert(rc_load_prayers(BAD_PATH) == -1);
    assert(rc_load_spells(BAD_PATH) == -1);
    assert(rc_load_player_actions(BAD_PATH) == -1);

    assert(rc_load_prayers(PRAY_PATH) == 31);
    const RcPrayerDef *augury = rc_prayer_def_get(RC_PRAYER_AUGURY);
    const RcPrayerDef *deadeye = rc_prayer_def_get(RC_PRAYER_DEADEYE);
    const RcPrayerDef *thick_skin = rc_prayer_def_get(RC_PRAYER_THICK_SKIN);
    assert(augury && augury->level == 77 && augury->magic_damage == 4);
    assert(deadeye && deadeye->defence == 5 && deadeye->ranged_strength == 18);
    assert(thick_skin && thick_skin->drain == 1);
    assert(rc_prayer_attack_bonus(PRAYER_PIETY) == 20);
    assert(rc_prayer_strength_bonus(PRAYER_PIETY) == 23);
    assert(rc_prayer_ranged_attack_bonus(PRAYER_RIGOUR) == 20);
    assert(rc_prayer_ranged_strength_bonus(PRAYER_RIGOUR) == 23);
    assert(rc_prayer_magic_attack_bonus(PRAYER_AUGURY) == 25);
    assert(rc_prayer_magic_damage_bonus(PRAYER_AUGURY) == 4);
    assert(rc_prayer_magic_defence_bonus(PRAYER_AUGURY) == 25);
    test_definition_validation();
    test_requirements_and_lifecycle();
    test_drain_resource_and_quick();
    test_transition_edges();
    test_collision_matrix();
    test_prayer_stats();

    assert(rc_load_spells(SPEL_PATH) == 201);
    int fire_blast = rc_spell_find("Fire Blast");
    int lumbridge = rc_spell_find("Lumbridge Home Teleport");
    assert(fire_blast >= 0);
    assert(rc_spell_find("Definitely Missing Spell") == -1);
    assert(rc_spell_def_get(fire_blast)->max_hit == 16);
    assert(lumbridge >= 0);
    assert(rc_spell_def_get(lumbridge)->effect_flags & RC_SPELL_EFFECT_TELEPORT);

    assert(rc_load_player_actions(PACT_PATH) == 13);
    assert(!rc_player_action_allowed(0, RC_PLAYER_ACTION_SET_PRAYER));
    assert(rc_player_action_allowed(RC_SUB_PRAYER, RC_PLAYER_ACTION_SET_PRAYER));
    assert(rc_player_action_allowed(RC_SUB_COMBAT, RC_PLAYER_ACTION_SELECT_SPELL));

    RcWorldConfig denied_cfg = rc_preset_base_only();
    denied_cfg.player_actions_path = PACT_PATH;
    RcWorld *denied = rc_world_create_config(&denied_cfg);
    assert(denied != NULL);
    rc_player_select_spell(denied, fire_blast);
    rc_world_tick(denied);
    assert(denied->player.selected_spell == -1);
    rc_world_destroy(denied);

    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_PRAYER | RC_SUB_INVENTORY;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = NDEF_PATH;
    cfg.spawns_path = NULL;
    cfg.prayers_path = PRAY_PATH;
    cfg.spells_path = SPEL_PATH;
    cfg.player_actions_path = PACT_PATH;
    cfg.varbits_path = RC_TEST_SOURCE_DIR "/data/defs/varbits.bin";
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);

    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    rc_world_tick(world);
    assert(world->player.active_prayers == 0);

    world->player.current_prayer_points = 1000;
    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    rc_world_tick(world);
    assert(world->player.active_prayers == 0);

    unlock_prayers(world);
    world->player.current_prayer_points = 1000;
    rc_player_set_prayer(world, RC_PRAYER_THICK_SKIN);
    rc_player_set_prayer(world, RC_PRAYER_DEADEYE);
    rc_world_tick(world);
    assert(world->player.active_prayers & PRAYER_THICK_SKIN);
    assert(world->player.active_prayers & PRAYER_DEADEYE);
    assert(rc_prayer_defence_bonus(world->player.active_prayers) == 10);

    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    rc_world_tick(world);
    assert(world->player.active_prayers == PRAYER_RIGOUR);
    for (int i = 0; i < 3; i++) rc_world_tick(world);
    assert(world->player.current_prayer_points == 990);
    rc_player_set_prayer(world, RC_PRAYER_RIGOUR);
    rc_world_tick(world);
    assert(world->player.active_prayers == 0);

    rc_player_select_spell(world, fire_blast);
    rc_world_tick(world);
    assert(world->player.selected_spell == fire_blast);
    rc_refresh_player_combat_style(&world->player);
    assert(world->player.combat_style != COMBAT_MAGIC);
    const RcSpellDef *fire_blast_def = rc_spell_def_get(fire_blast);
    assert(fire_blast_def != NULL);
    for (int i = 0; i < fire_blast_def->rune_count; i++) {
        world->player.inventory[i].item_id =
            (int)fire_blast_def->runes[i].item_id;
        world->player.inventory[i].quantity =
            fire_blast_def->runes[i].qty + 1;
    }

    int jad_def = rc_npc_def_find(3127);
    assert(jad_def >= 0);
    world->player.skills.boosted_level[SKILL_RANGED] = 99;
    world->player.equipment_bonuses[EQ_RANGED_STR] = 64;
    world->player.active_prayers = PRAYER_RIGOUR;
    int npc_idx = rc_npc_spawn(world, jad_def, world->player.x, world->player.y, 0);
    assert(npc_idx >= 0);
    RcCombatCalc ranged = rc_calc_ranged(&world->player, &world->npcs[npc_idx], true);
    assert(ranged.attack_roll > 0 && ranged.max_hit > 0);
    world->player.attack_target = world->npcs[npc_idx].uid;
    world->player.manual_spell_cast = fire_blast;
    world->player.skills.boosted_level[SKILL_MAGIC] = 99;
    world->player.active_prayers = PRAYER_AUGURY;
    rc_combat_tick_player(world);
    assert(world->npcs[npc_idx].num_pending_hits == 1);

    assert(rc_prayer_set_active(world, PRAYER_PROTECT_MELEE) == RC_PRAYER_OK);
    world->player.current_hp = 0;
    rc_world_tick(world);
    assert(world->player.is_dead && !world->player.active_prayers);
    assert(world->player.prayer_outcome.code == RC_PRAYER_DEAD);
    assert(rc_world_respawn_player(world, 3184, 3440, 0));
    assert(world->player.current_prayer_points == 990 && !world->player.active_prayers);

    rc_world_destroy(world);
    return 0;
}
