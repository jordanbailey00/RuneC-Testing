#include "api.h"
#include "combat.h"
#include "items.h"
#include "npc.h"
#include "pathfinding.h"
#include "spells.h"
#include "world_test_fixture.h"
#include "../rc-content/content.h"
#include "../rc-content/combat/magic.h"
#include "../rc-viewer/dev_validation.c"
#include "storage.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void reset_attack(RcWorld *w, RcNpc *n) {
    rc_combat_stop_actor(w, (RcCombatActorRef){RC_COMBAT_ACTOR_PLAYER, 0}, 0);
    w->player.attack_timer = 0;
    w->player.manual_spell_cast = w->player.autocast_spell = -1;
    w->player.attack_style_idx = 0;
    n->num_pending_hits = 0;
    n->current_hp = 100000;
    n->is_dead = false;
    n->target_uid = -1;
    n->x = w->player.x + 3;
    n->y = w->player.y;
    memset(&n->combat, 0, sizeof(n->combat));
    w->combat_attack_event_count = 0;
}

static void equip(RcWorld *w, int id, uint32_t charges) {
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = -1};
    w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    assert(rc_inv_add_state(w->player.inventory, id, 1, charges) == 0);
    assert(rc_item_result_accepted(rc_player_equip(w, 0)));
    rc_world_tick(w);
    if (w->player.equipment[EQUIP_WEAPON].item_id != id)
        fprintf(stderr, "Failed to equip magic weapon %d\n", id);
    assert(w->player.equipment[EQUIP_WEAPON].item_id == id);
    if (w->player.equipment[EQUIP_WEAPON].state_id != charges)
        fprintf(stderr, "Equipping %d changed charges: %u -> %u\n", id, charges,
                w->player.equipment[EQUIP_WEAPON].state_id);
    assert(w->player.equipment[EQUIP_WEAPON].state_id == charges);
}

static void rune_sources(RcWorld *w) {
    equip(w, 1381, 0);
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = -1};
    RcSpellDef cost = {.loaded = 1, .rune_count = 2,
        .runes = {{556, 2}, {555, 1}}};
    assert(rc_inv_add(w->player.inventory, 4695, 1) >= 0);
    assert(!w->combat_hooks.player_has_spell_runes(w, &w->player, &cost)
        && "One mist rune cannot supply two air runes");
    assert(!w->combat_hooks.player_consume_spell_runes(w, &w->player, &cost));
    assert(w->player.inventory[0].quantity == 1);
    assert(rc_inv_add(w->player.inventory, 556, 1) >= 0);
    assert(w->combat_hooks.player_has_spell_runes(w, &w->player, &cost));
    assert(w->combat_hooks.player_consume_spell_runes(w, &w->player, &cost));
    assert(rc_inv_find(w->player.inventory, 4695) < 0);
    assert(rc_inv_find(w->player.inventory, 556) < 0);
    cost = (RcSpellDef){.loaded = 1, .rune_count = 1, .runes = {{554, 1}}};
    w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = 20716, .quantity = 1};
    assert(!w->combat_hooks.player_has_spell_runes(w, &w->player, &cost)
        && "An empty tome must not grant runes");
    w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = -1};
    w->player.rune_pouch[0] = (RcInvSlot){.item_id = 554, .quantity = 1};
    assert(!w->combat_hooks.player_has_spell_runes(w, &w->player, &cost)
        && "Detached pouch contents must not pay a spell");
    assert(rc_inv_add(w->player.inventory, 12791, 1) >= 0);
    assert(w->combat_hooks.player_has_spell_runes(w, &w->player, &cost));
    assert(w->combat_hooks.player_consume_spell_runes(w, &w->player, &cost));
    assert(w->player.rune_pouch[0].quantity == 0);
    w->player.rune_pouch[3] = (RcInvSlot){.item_id = 554, .quantity = 1};
    assert(!w->combat_hooks.player_has_spell_runes(w, &w->player, &cost)
        && "A regular pouch cannot expose a fourth slot");
    w->player.rune_pouch[3] = (RcInvSlot){.item_id = -1};
    int pouch = rc_inv_find(w->player.inventory, 12791);
    int rune = rc_inv_add(w->player.inventory, 554, 9);
    assert(rc_player_use_inventory_item_on_inventory_item(w, rune, pouch));
    rc_world_tick(w);
    assert(w->player.rune_pouch[0].quantity == 9);
    assert(rc_inv_find(w->player.inventory, 554) < 0);
    RcItemTransaction stale;
    assert(rc_item_tx_begin(&stale, w).code == RC_ITEM_RESULT_OK);
    assert(w->combat_hooks.player_consume_spell_runes(w, &w->player, &cost));
    assert(rc_item_tx_commit(&stale).code == RC_ITEM_RESULT_STALE);
    assert(w->player.rune_pouch[0].quantity == 8);
    assert(rc_player_interact_inventory_item(w, pouch, 0));
    rc_world_tick(w);
    assert(w->player.rune_pouch[0].quantity == 8);
    int coin = rc_inv_add(w->player.inventory, 995, 1);
    assert(rc_player_use_inventory_item_on_inventory_item(w, coin, pouch));
    rc_world_tick(w);
    assert(w->player.rune_pouch[0].quantity == 8 && w->player.inventory[coin].quantity == 1);
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (w->player.inventory[i].quantity == 0) w->player.inventory[i] = (RcInvSlot){.item_id = 1205, .quantity = 1};
    assert(rc_player_interact_inventory_item(w, pouch, 3));
    rc_world_tick(w);
    assert(w->player.rune_pouch[0].quantity == 8);
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        if (i != pouch) w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    assert(rc_player_interact_inventory_item(w, pouch, 3));
    rc_world_tick(w);
    assert(w->player.rune_pouch[0].quantity == 0);
    assert(w->player.inventory[rc_inv_find(w->player.inventory, 554)].quantity == 8);
    for (int i = 0; i < 4; i++) w->player.rune_pouch[i] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_INVENTORY_SIZE; i++) w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    assert(rc_inv_add(w->player.inventory, 27281, 1) >= 0);
    w->player.rune_pouch[3] = (RcInvSlot){.item_id = 554, .quantity = 16001};
    assert(!rc_content_has_spell_runes(w, &w->player, &cost));
    w->player.rune_pouch[3].quantity = 1;
    assert(rc_content_consume_spell_runes(w, &w->player, &cost));
    assert(w->player.rune_pouch[3].quantity == 0);
    strcpy(cost.name, "Fire Strike");
    w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = 20714, .quantity = 1, .state_id = 1};
    assert(rc_content_has_spell_runes(w, &w->player, &cost));
    assert(w->player.equipment[EQUIP_SHIELD].state_id == 1);
    assert(rc_content_consume_spell_runes(w, &w->player, &cost));
    assert(w->player.equipment[EQUIP_SHIELD].item_id == 20716);
    assert(!rc_content_has_spell_runes(w, &w->player, &cost));
    cost.runes[0].item_id = 561;
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = 22370, .quantity = 1, .state_id = 1};
    assert(rc_content_consume_spell_runes(w, &w->player, &cost));
    assert(w->player.equipment[EQUIP_WEAPON].item_id == 22368);
    assert(!rc_content_has_spell_runes(w, &w->player, &cost));
}

static int64_t magic_xp(const RcPlayer *p) {
    return (int64_t)p->skills.xp[SKILL_MAGIC] * 100 + p->skills.xp_hundredths[SKILL_MAGIC];
}

static void admission_and_xp(RcWorld *w, RcNpc *n) {
    const int tomes[] = {20714, 25574, 30064};
    const char *elements[] = {"Fire Surge", "Water Surge", "Earth Surge"};
    w->player.equipment_bonuses[EQ_MAGIC_DMG] = 0;
    for (int i = 0; i < 3; i++) {
        int id = rc_spell_find(elements[i]);
        const RcSpellDef *spell = rc_spell_def_get(id);
        RcCombatCalc calc, plain;
        const char *failure = NULL;
        int speed = 5;
        w->player.manual_spell_cast = id;
        w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = -1};
        assert(rc_content_prepare_magic(w, n, spell, &plain, &speed, &failure));
        w->player.equipment[EQUIP_SHIELD] = (RcInvSlot){.item_id = tomes[i], .quantity = 1, .state_id = 1};
        assert(rc_content_prepare_magic(w, n, spell, &calc, &speed, &failure));
        assert(calc.max_hit == plain.max_hit * 11 / 10);
        for (int r = 0; r < spell->rune_count; r++)
            assert(rc_inv_add(w->player.inventory, spell->runes[r].item_id, spell->runes[r].qty) >= 0);
        assert(rc_content_consume_spell_runes(w, &w->player, spell));
        assert(w->player.equipment[EQUIP_SHIELD].state_id == 0);
        w->player.skills.boosted_level[SKILL_MAGIC] = 1;
        assert(!rc_content_prepare_magic(w, n, spell, &calc, &speed, &failure));
        w->player.skills.boosted_level[SKILL_MAGIC] = 99;
    }
    const char *names[] = {"Crumble Undead", "Inferior Demonbane"};
    for (int i = 0; i < 2; i++) {
        const RcSpellDef *spell = rc_spell_def_get(rc_spell_find(names[i]));
        RcCombatCalc calc;
        const char *failure = NULL;
        int speed = 5;
        w->player.manual_spell_cast = rc_spell_find(names[i]);
        assert(!rc_content_prepare_magic(w, n, spell, &calc, &speed, &failure));
        assert(failure && strstr(failure, "target"));
    }
    const char *casts[] = {"Wind Strike", "Bind"};
    for (int i = 0; i < 2; i++) {
        equip(w, 1381, 0);
        reset_attack(w, n);
        int id = rc_spell_find(casts[i]);
        const RcSpellDef *spell = rc_spell_def_get(id);
        for (int r = 0; r < spell->rune_count; r++)
            assert(rc_inv_add(w->player.inventory, spell->runes[r].item_id, spell->runes[r].qty) >= 0);
        w->player.current_spellbook = spell->book;
        w->player.manual_spell_cast = id;
        assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
        w->player.equipment_bonuses[EQ_MAGIC_ATK] = -10000;
        n->force_player_max_hit = false;
        int64_t before = magic_xp(&w->player);
        rc_combat_tick_player(w);
        assert(w->combat_attack_event_count == 1 && !w->combat_attack_events[0].accurate);
        assert(magic_xp(&w->player) - before == (int64_t)spell->xp_q1 * 10);
        assert(n->pending_hits[0].apply_tick == w->tick + w->combat_attack_events[0].hit_delays[0]);
        reset_attack(w, n);
        w->player.manual_spell_cast = id;
        assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
        uint32_t rng = w->rng_state;
        before = magic_xp(&w->player);
        rc_combat_tick_player(w);
        assert(!w->combat_attack_event_count && !n->num_pending_hits);
        assert(w->rng_state == rng && magic_xp(&w->player) == before);
    }
    n->force_player_max_hit = true;
    int id = rc_spell_find("Ice Barrage");
    w->player.current_spellbook = RC_SPELL_BOOK_STANDARD;
    assert(rc_spell_available(w, id, 0) == RC_SPELL_WRONG_BOOK);
    w->player.current_spellbook = RC_SPELL_BOOK_ANCIENT;
    w->members_world = false;
    assert(rc_spell_available(w, id, 0) == RC_SPELL_MEMBERS);
    w->members_world = true;
    w->player.skills.boosted_level[SKILL_MAGIC] = 1;
    assert(rc_spell_available(w, id, 0) == RC_SPELL_LEVEL);
    w->player.skills.boosted_level[SKILL_MAGIC] = 99;
    assert(rc_spell_available(w, RC_MAX_SPELL_DEFS, 0) == RC_SPELL_INVALID);
    assert(!rc_spell_result_accepted(RC_SPELL_RUNES));
    w->player.current_spellbook = RC_SPELL_BOOK_STANDARD;
    assert(rc_spell_available(NULL, id, 0) == RC_SPELL_INVALID);
    uint32_t enabled = w->enabled;
    w->enabled &= ~RC_SUB_COMBAT;
    assert(rc_spell_available(w, id, 0) == RC_SPELL_DISABLED);
    w->enabled = enabled;
    w->player.current_hp = 0;
    assert(rc_player_select_spell(w, id) == RC_SPELL_DEAD);
    assert(rc_player_set_spellbook(w, 1) == RC_SPELL_DEAD);
    assert(rc_player_set_autocast_spell(w, id, 0) == RC_SPELL_DEAD);
    assert(rc_spell_available(w, id, 0) == RC_SPELL_DEAD);
    w->player.current_hp = w->player.max_hp;
    for (int i = 0; i < RC_MAX_PLAYER_COMMANDS; i++)
        assert(rc_player_set_spellbook(w, RC_SPELL_BOOK_STANDARD) == RC_SPELL_QUEUED);
    assert(rc_player_select_spell(w, id) == RC_SPELL_QUEUE_FULL);
    assert(rc_player_set_spellbook(w, 1) == RC_SPELL_QUEUE_FULL);
    assert(rc_player_set_autocast_spell(w, id, 0) == RC_SPELL_QUEUE_FULL);
    rc_world_tick(w);
    assert(!w->player.combat.failure_reason);
    for (int result = RC_SPELL_OK; result <= RC_SPELL_DEAD; result++)
        assert(rc_spell_result_accepted(result) || rc_spell_result_message(result)[0]);
    assert(rc_spell_result_message(-1)[0]);
    reset_attack(w, n);
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        w->player.inventory[i] = (RcInvSlot){.item_id = -1};
    int64_t xp = magic_xp(&w->player);
    assert(rc_player_cast_spell_on_npc(w, rc_spell_find("Wind Strike"), n->uid));
    rc_world_tick(w);
    assert(!n->num_pending_hits && magic_xp(&w->player) == xp);
    assert(w->player.interaction.last_failure == RC_INTERACTION_FAIL_INVALID_SOURCE);
}

int main(void) {
    assert(sizeof(RcPendingHit) == 56 && "hit metadata must use existing padding");
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_STORAGE;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.npc_defs_path = RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin";
    cfg.spells_path = RC_TEST_SOURCE_DIR "/data/defs/spells.bin";
    cfg.player_actions_path = RC_TEST_SOURCE_DIR "/data/defs/player_actions.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    rc_content_combat_register(w);
    runec_dev_validation_seed_bank(w);
    for (int i = 0; i < SKILL_COUNT; i++)
        w->player.skills.base_level[i] = w->player.skills.boosted_level[i] = 99;
    w->player.x = w->player.y = 3208;
    rune_sources(w);
    rc_test_open_mapsquare(w, 3208, 3208, 0);
    int ni = rc_npc_spawn(w, rc_npc_def_find(3014), 3211, 3208, 0);
    assert(ni >= 0);
    RcNpc *n = &w->npcs[ni];
    n->disable_wander = n->force_player_max_hit = true;
    admission_and_xp(w, n);
    int tested = 0;
    for (int id = 0; rc_spell_def_get(id); id++) {
        const RcSpellDef *spell = rc_spell_def_get(id);
        if (spell->type != RC_SPELL_TYPE_COMBAT || !spell->effect_flags) continue;
        n->def_id = !strcmp(spell->name, "Crumble Undead") ? rc_npc_def_find(70)
            : strstr(spell->name, "Demonbane") ? rc_npc_def_find(2005) : rc_npc_def_find(3014);
        assert(n->def_id >= 0);
        reset_attack(w, n);
        int weapon = !strcmp(spell->name, "Iban Blast") ? 12658 :
                     !strcmp(spell->name, "Magic Dart") ? 4170 :
                     !strcmp(spell->name, "Saradomin Strike") ? 2415 :
                     !strcmp(spell->name, "Claws of Guthix") ? 2416 :
                     !strcmp(spell->name, "Flames of Zamorak") ? 2417 : 1387;
        equip(w, weapon, rc_content_magic_charge_capacity(weapon));
        reset_attack(w, n);
        w->player.storage_kind = RC_STORAGE_BANK;
        for (int r = 0; r < spell->rune_count; r++) {
            int slot = -1;
            for (int b = 0; b < RC_BANK_SIZE; b++)
                if (w->player.bank[b].item_id == (int)spell->runes[r].item_id) slot = b;
            if (slot < 0) fprintf(stderr, "%s requires missing bank rune %u\n",
                                  spell->name, spell->runes[r].item_id);
            assert(slot >= 0 && w->player.bank_tab[slot] == RUNEC_DEV_BANK_TAB_MAGE);
            assert(rc_bank_withdraw_slot(w, slot, spell->runes[r].qty * 10));
            rc_world_tick(w);
        }
        assert(rc_player_close_storage(w));
        rc_world_tick(w);
        rc_player_set_spellbook(w, spell->book);
        rc_world_tick(w);
        assert(rc_player_cast_spell_on_npc(w, id, n->uid));
        rc_world_tick(w);
        if (!w->combat_attack_event_count)
            fprintf(stderr, "Spell %s failed: combat=%s interaction=%s\n", spell->name,
                w->player.combat.failure_reason ? w->player.combat.failure_reason : "none",
                w->player.interaction_outcome.message);
        assert(w->combat_attack_event_count == 1);
        assert(w->combat_attack_events[0].spell_idx == id);
        assert(n->num_pending_hits == 1);
        assert(n->pending_hits[0].spell_key == id + 1);
        assert(w->player.attack_target == -1 && "manual cast must not become a weapon attack");
        assert(w->player.attack_timer == 5);
        tested++;
    }
    assert(tested == 57);

    // Effects belong to an accurate landed spell, not to its launch or damage value.
    for (int id = 0; rc_spell_def_get(id); id++) {
        const RcSpellDef *s = rc_spell_def_get(id);
        if (s->type != RC_SPELL_TYPE_COMBAT || !s->effect_flags) continue;
        const RcNpcDef *nd = rc_npc_def_for_npc(w, n);
        memcpy(n->stats, nd->stats, sizeof(n->stats));
        n->immobilized_until = n->immobilize_immune_until = 0;
        w->player.current_hp = 500;
        w->player.max_hp = 990;
        RcPendingHit hit = {.spell_key = id + 1, .attack_style = COMBAT_MAGIC};
        rc_content_magic_hit(w, n, &hit, 20);
        assert(w->player.current_hp == 500 && n->immobilized_until == 0);
        hit.accurate = true;
        rc_content_magic_hit(w, n, &hit, 20);
        if (s->effect_flags & RC_SPELL_EFFECT_HEAL) assert(w->player.current_hp == 550);
        if ((s->effect_flags & RC_SPELL_EFFECT_FREEZE) && !strstr(s->name, "Grasp")) {
            assert(n->immobilized_until > w->tick);
            RcTick until = n->immobilized_until;
            rc_content_magic_hit(w, n, &hit, 0);
            assert(n->immobilized_until == until);
            int x = n->x, y = n->y;
            rc_npc_movement_tick(w, n);
            assert(n->x == x && n->y == y);
        }
    }
    n->immobilized_until = n->immobilize_immune_until = 0;
    reset_attack(w, n);
    assert(rc_combat_start_npc_vs_player(w, n->uid, 0));
    RcRouteTarget chase = rc_route_target_point(w->player.x + 1, w->player.y);
    assert(rc_npc_route_request(w, n, &chase, RC_NPC_ROUTE_CHASE, false));
    n->immobilized_until = w->tick + 8;
    int frozen_x = n->x, frozen_y = n->y;
    rc_npc_movement_tick(w, n);
    assert(n->x == frozen_x && n->y == frozen_y);
    n->immobilized_until = 0;
    rc_npc_movement_tick(w, n);
    assert(n->x != frozen_x || n->y != frozen_y);
    for (int form = 0; form < 2; form++) {
        RcPendingHit sanguinesti = {.weapon_id = form ? 25731 : 22323,
                                    .attack_style = COMBAT_MAGIC, .accurate = true};
        int heals = 0;
        for (int i = 0; i < 60; i++) {
            w->player.current_hp = 500;
            rc_content_magic_hit(w, n, &sanguinesti, 20);
            if (w->player.current_hp == 600) heals++;
        }
        assert(heals > 0 && heals < 60);
        w->player.current_hp = 1100;
        rc_content_magic_hit(w, n, &sanguinesti, 20);
        assert(w->player.current_hp == 1100);
    }

    const int staves[] = {11905,11907,12899,22288,22292,22323,25731,27275,
        28547,22555,27665,28585,31113,22516,33314,33318,33322,33323,33326};
    for (unsigned i = 0; i < sizeof(staves) / sizeof(staves[0]); i++) {
        reset_attack(w, n);
        int id = staves[i];
        uint32_t capacity = rc_content_magic_charge_capacity(id);
        equip(w, id, capacity ? 1 : 0);
        reset_attack(w, n);
        assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
        rc_combat_tick_player(w);
        if (!w->combat_attack_event_count) fprintf(stderr, "Powered weapon %d (type %d, style %d, timer %d): %s\n", id,
            rc_item_def_get(id)->weapon_type, w->player.combat_style, w->player.attack_timer,
            w->player.combat.failure_reason ? w->player.combat.failure_reason : "no cast");
        assert(w->combat_attack_event_count == 1);
        assert(w->combat_attack_events[0].accurate);
        assert(n->pending_hits[0].damage > 0);
        assert(w->combat_attack_events[0].spell_idx == -1);
        assert(w->player.equipment[EQUIP_WEAPON].state_id == 0);
        if (capacity) {
            reset_attack(w, n);
            assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
            rc_combat_tick_player(w);
            assert(w->combat_attack_event_count == 0);
            assert(w->player.combat.failure_reason);
        }
    }

    reset_attack(w, n);
    equip(w, 27275, 20001);
    reset_attack(w, n);
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_combat_tick_player(w);
    assert(w->combat_attack_event_count == 0);
    assert(w->player.equipment[EQUIP_WEAPON].state_id == 20001);

    const RcSpellDef *ice = rc_spell_def_get(rc_spell_find("Ice Barrage"));
    const RcSpellDef *fire_def = rc_spell_def_get(rc_spell_find("Fire Blast"));
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = 1387, .quantity = 1};
    assert(!rc_content_can_autocast(&w->player, ice));
    assert(rc_content_can_autocast(&w->player, fire_def));
    assert(!rc_content_can_autocast(&w->player, rc_spell_def_get(rc_spell_find("Saradomin Strike"))));
    RcCombatCalc rejected;
    int speed = 5;
    const char *failure = NULL;
    w->player.manual_spell_cast = rc_spell_find("Saradomin Strike");
    assert(!rc_content_prepare_magic(w, n, rc_spell_def_get(w->player.manual_spell_cast),
                                     &rejected, &speed, &failure));
    assert(failure && strstr(failure, "corresponding staff"));
    w->player.equipment[EQUIP_WEAPON].item_id = 4675;
    assert(rc_content_can_autocast(&w->player, ice));
    w->player.equipment[EQUIP_WEAPON].item_id = 24423;
    assert(!rc_content_can_autocast(&w->player, ice));
    assert(rc_content_can_autocast(&w->player, fire_def));
    w->player.equipment[EQUIP_WEAPON] = (RcInvSlot){.item_id = 12658, .quantity = 1, .state_id = 5};
    w->player.combat_style = COMBAT_MELEE_CRUSH;
    assert(rc_content_magic_consume_charge(w, 12658));
    assert(w->player.equipment[EQUIP_WEAPON].state_id == 5);
    w->player.combat_style = COMBAT_MAGIC;
    assert(rc_content_magic_consume_charge(w, 12658));
    assert(w->player.equipment[EQUIP_WEAPON].state_id == 4);
    w->player.equipment[EQUIP_WEAPON].state_id = 0;
    assert(!rc_content_magic_consume_charge(w, 12658));

    const struct { int weapon; const char *spell; bool allowed; } autocasts[] = {
        {4170, "Magic Dart", true}, {1387, "Magic Dart", false},
        {12658, "Iban Blast", true}, {1387, "Iban Blast", false},
        {22296, "Saradomin Strike", true}, {2415, "Saradomin Strike", false},
        {24144, "Claws of Guthix", true}, {8841, "Claws of Guthix", true},
        {11791, "Flames of Zamorak", true}, {1387, "Flames of Zamorak", false},
        {4170, "Crumble Undead", true}, {1387, "Crumble Undead", false},
        {4170, "Undead Grasp", true}, {1387, "Undead Grasp", false},
        {1387, "Bind", false}, {27275, "Fire Blast", false},
    };
    for (unsigned i = 0; i < sizeof(autocasts) / sizeof(autocasts[0]); i++) {
        w->player.equipment[EQUIP_WEAPON].item_id = autocasts[i].weapon;
        const RcSpellDef *s = rc_spell_def_get(rc_spell_find(autocasts[i].spell));
        assert(s);
        if (!!rc_content_can_autocast(&w->player, s) != autocasts[i].allowed)
            fprintf(stderr, "Unexpected autocast permission: %d / %s\n",
                    autocasts[i].weapon, s->name);
        assert(!!rc_content_can_autocast(&w->player, s) == autocasts[i].allowed);
    }
    w->player.equipment[EQUIP_WEAPON].item_id = 1387;
    const char *invalid[] = {"Magic Dart", "Iban Blast", "Ice Barrage"};
    for (int i = 0; i < 3; i++) {
        w->player.manual_spell_cast = i == 2 ? -1 : rc_spell_find(invalid[i]);
        assert(!rc_content_prepare_magic(w, n, rc_spell_def_get(rc_spell_find(invalid[i])),
                                         &rejected, &speed, &failure));
        assert(failure);
    }
    w->player.equipment[EQUIP_WEAPON].item_id = 24423;
    w->player.manual_spell_cast = -1;
    assert(rc_content_prepare_magic(w, n, fire_def, &rejected, &speed, &failure));
    assert(speed == 4);

    reset_attack(w, n);
    equip(w, 1387, 0);
    reset_attack(w, n);
    int fire = rc_spell_find("Fire Blast");
    const RcSpellDef *spell = rc_spell_def_get(fire);
    rc_player_set_spellbook(w, RC_SPELL_BOOK_STANDARD);
    rc_world_tick(w);
    for (int r = 0; r < spell->rune_count; r++)
        assert(rc_inv_add(w->player.inventory, spell->runes[r].item_id, 100) >= 0);
    rc_player_set_autocast_spell(w, fire, 0);
    rc_world_tick(w);
    assert(w->player.autocast_spell == fire);
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == 1 && w->player.attack_target == n->uid);
    w->player.skills.boosted_level[SKILL_MAGIC] = 1;
    w->player.attack_timer = 0;
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == 1 && w->player.combat.failure_reason);
    w->player.skills.boosted_level[SKILL_MAGIC] = 99;
    w->player.inventory[rc_inv_find(w->player.inventory, 560)].quantity = 0;
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    rc_combat_tick_player(w);
    assert(n->num_pending_hits == 1 && w->player.combat.failure_reason);
    assert(strstr(w->player.combat.failure_reason, "enough runes"));
    reset_attack(w, n);
    equip(w, 27275, 100);
    reset_attack(w, n);
    w->player.combat_style = COMBAT_MAGIC;
    w->player.manual_spell_cast = rc_spell_find("Wind Strike");
    assert(w->player.manual_spell_cast >= 0);
    assert(rc_combat_start_player_vs_npc(w, 0, n->uid));
    w->player.manual_spell_cast = RC_MAX_SPELL_DEFS;
    rc_combat_tick_player(w);
    assert(w->combat_attack_event_count == 0);
    assert(w->player.combat.failure_reason
        && strstr(w->player.combat.failure_reason, "not supported"));
    rc_world_destroy(w);
    puts("Magic: 57 manual spells, 19 powered variants, autocast and resource failures passed.");
    return 0;
}
