#include <assert.h>
#include <string.h>

#include "../rc-core/storage.h"
#include "../rc-viewer/dev_validation.c"
#include "world_test_fixture.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"
#define SPELL_PATH RC_TEST_SOURCE_DIR "/data/defs/spells.bin"
#define VISUALS_PATH RC_TEST_SOURCE_DIR "/data/defs/combat_visuals.tsv"

static int find_item_by_name(const char *name) {
    return find_unnoted_item_id_by_name(name);
}

static void clear_player_items(RcWorld *world) {
    for (int i = 0; i < RC_INVENTORY_SIZE; i++)
        world->player.inventory[i] = (RcInvSlot){.item_id = -1};
    for (int i = 0; i < RC_EQUIP_COUNT; i++)
        world->player.equipment[i] = (RcInvSlot){.item_id = -1};
}

static void assert_validation_item_equips(RcWorld *world, const char *name,
                                          int equip_slot) {
    int item_id = find_item_by_name(name);
    assert(item_id >= 0);
    const RcItemDef *def = rc_item_def_get(item_id);
    assert(def);
    assert(def->equippable);
    assert(def->equip_slot == equip_slot);
    clear_player_items(world);
    assert(rc_inv_add(world->player.inventory, item_id, 1) == 0);
    rc_player_equip(world, 0);
    rc_world_tick(world);
    assert(world->player.inventory[0].item_id == -1);
    assert(world->player.equipment[equip_slot].item_id == item_id);
}

static void test_god_mode(RcWorld *world) {
    RcPlayer *p = &world->player;
    const RcPendingHit hit = {.source_idx = RC_HIT_SOURCE_STATUS};
    assert(!runec_dev_validation_set_god_mode(NULL, true));
    assert(runec_dev_validation_set_god_mode(world, true));
    assert(!runec_dev_validation_set_god_mode(world, true));
    for (int i = 0; i < 6; i++) {
        rc_combat_apply_player_hit(world, &hit, 100000);
        assert(p->current_hp == 10 && !p->is_dead);
    }
    rc_world_tick(world);
    assert(!p->is_dead && p->current_hp >= 10);
    assert(runec_dev_validation_set_god_mode(world, false));
    rc_combat_apply_player_hit(world, &hit, 100000);
    rc_world_tick(world);
    assert(p->is_dead);
    int x = p->x;
    p->x = -1;
    assert(!runec_dev_validation_set_god_mode(world, true));
    assert(p->is_dead);
    p->x = x;
    assert(runec_dev_validation_set_god_mode(world, true));
    assert(!p->is_dead && p->current_hp > 0);
    assert(runec_dev_validation_set_god_mode(world, false));
}

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT |
                     RC_SUB_STORAGE | RC_SUB_COMBAT;
    cfg.items_path = ITEM_PATH;
    cfg.spells_path = SPELL_PATH;
    cfg.combat_profiles_path = VISUALS_PATH;
    cfg.seed = 12345;
    cfg.npc_capacity = 2048;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    for (int i = 0; i < SKILL_COUNT; i++)
        world->player.skills.base_level[i] = 99;
    rc_test_open_mapsquare(world, world->player.x, world->player.y,
                           world->player.plane);
    test_god_mode(world);

    runec_dev_validation_seed_bank(world);
    const char *runes[] = {"Air rune", "Water rune", "Earth rune", "Fire rune",
        "Mind rune", "Body rune", "Cosmic rune", "Chaos rune", "Nature rune",
        "Law rune", "Death rune", "Astral rune", "Blood rune", "Soul rune",
        "Wrath rune", "Mist rune", "Dust rune", "Mud rune", "Smoke rune",
        "Steam rune", "Lava rune", "Sunfire rune", "Aether rune"};
    for (size_t i = 0; i < sizeof(runes) / sizeof(runes[0]); i++) {
        int id = find_item_by_name(runes[i]);
        bool found = false;
        assert(id > 0);
        for (int slot = 0; slot < RC_BANK_SIZE; slot++) {
            if (world->player.bank[slot].item_id != id) continue;
            assert(!found && "rune should be stocked only once");
            found = true;
            assert(world->player.bank_tab[slot] == RUNEC_DEV_BANK_TAB_MAGE);
            assert(world->player.bank[slot].quantity == 100000);
            assert(!rc_item_def_get(id)->noted);
        }
        assert(found && "rune missing from testing bank");
    }
    assert(find_item_by_name("Amethyst arrows") == 21326);
    const int finite_sources[] = {20714, 25574, 30064, 22370};
    for (unsigned i = 0; i < sizeof(finite_sources) / sizeof(finite_sources[0]); i++) {
        bool found = false;
        for (int slot = 0; slot < RC_BANK_SIZE; slot++) {
            if (world->player.bank[slot].item_id != finite_sources[i]) continue;
            found = true;
            assert(world->player.bank[slot].state_id == 100);
        }
        assert(found && "finite rune source missing from test bank");
    }
    assert(find_item_by_name("Granite maul (ornate handle)") == 24225);

    int tab_seen[5] = {0};
    int used = 0;
    int stack_slot = -1;
    int gear_slot = -1;
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        RcInvSlot *slot = &world->player.bank[i];
        if (slot->item_id < 0)
            continue;
        const RcItemDef *def = rc_item_def_get(slot->item_id);
        assert(def);
        assert(!def->noted);
        assert(!def->placeholder);
        assert(slot->quantity >= (def->stackable ? 1000 : 2));
        assert(world->player.bank_tab[i] < 5);
        tab_seen[world->player.bank_tab[i]]++;
        used++;
        if (def->stackable && stack_slot < 0)
            stack_slot = i;
        if (!def->stackable && gear_slot < 0)
            gear_slot = i;
    }
    assert(used > 300);
    const int magic_pairs[][2] = {{27275,27277}, {22323,22481}, {25731,25733},
        {28585,28583}, {31113,31115}, {12899,12900}, {22288,22290},
        {22292,22294}, {22555,22552}, {27665,27662}};
    for (unsigned i = 0; i < sizeof(magic_pairs) / sizeof(magic_pairs[0]); i++) {
        for (int form = 0; form < 2; form++) {
            bool found = false;
            for (int slot = 0; slot < RC_BANK_SIZE; slot++) {
                const RcInvSlot *item = &world->player.bank[slot];
                if (item->item_id != magic_pairs[i][form]) continue;
                found = true;
                assert(item->state_id == (form ? 0 : rc_content_magic_charge_capacity(item->item_id)));
            }
            assert(found && "charged and empty magic weapon must both be stocked");
        }
    }
    for (int i = 0; i < 5; i++)
        assert(tab_seen[i] > 0);

    world->player.storage_kind = RC_STORAGE_BANK;
    assert(stack_slot >= 0);
    int stack_before = world->player.bank[stack_slot].quantity;
    int stack_withdraw =
        runec_dev_validation_bank_withdraw_quantity(world, stack_slot);
    assert(stack_withdraw == stack_before - 1);
    assert(rc_bank_withdraw_slot(world, stack_slot, stack_withdraw) == 1);
    rc_world_tick(world);
    assert(world->player.bank[stack_slot].quantity == 1);

    clear_player_items(world);
    assert(gear_slot >= 0);
    int gear_before = world->player.bank[gear_slot].quantity;
    int gear_withdraw =
        runec_dev_validation_bank_withdraw_quantity(world, gear_slot);
    assert(gear_withdraw == gear_before - 1);
    assert(rc_bank_withdraw_slot(world, gear_slot, gear_withdraw) == 1);
    rc_world_tick(world);
    assert(world->player.bank[gear_slot].quantity == 1);

    assert_validation_item_equips(world, "Oathplate helm", EQUIP_HEAD);
    assert_validation_item_equips(world, "Oathplate chest", EQUIP_BODY);
    assert_validation_item_equips(world, "Oathplate legs", EQUIP_LEGS);
    assert_validation_item_equips(world, "Avernic treads", EQUIP_BOOTS);
    assert_validation_item_equips(world, "Confliction gauntlets", EQUIP_GLOVES);
    assert_validation_item_equips(world, "Twinflame staff", EQUIP_WEAPON);
    assert_validation_item_equips(world, "Amethyst arrows", EQUIP_AMMO);
    assert(rc_item_result_accepted(rc_player_unequip(world, EQUIP_AMMO)));
    rc_world_tick(world);
    assert(world->player.equipment[EQUIP_AMMO].item_id == -1);
    assert(rc_inv_find(world->player.inventory, 21326) >= 0);

    int equipment_checked = 0;
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        const RcItemDef *def = rc_item_def_get(world->player.bank[i].item_id);
        if (!def || !def->equippable || !def->equipable_by_player) continue;
        clear_player_items(world);
        assert(rc_bank_withdraw_slot(world, i, 1) == 1);
        rc_world_tick(world);
        int inv = rc_inv_find(world->player.inventory, def->id);
        assert(inv >= 0);
        assert(rc_item_result_accepted(rc_player_equip(world, inv)));
        rc_world_tick(world);
        if (world->player.equipment[def->equip_slot].item_id != def->id)
            fprintf(stderr, "bank equipment regression: %d %s\n", def->id, def->name);
        assert(world->player.equipment[def->equip_slot].item_id == def->id);
        assert(rc_item_result_accepted(rc_player_unequip(world, def->equip_slot)));
        rc_world_tick(world);
        assert(rc_inv_find(world->player.inventory, def->id) >= 0);
        equipment_checked++;
    }
    printf("validation bank: %d gear entries withdraw/equip/unequip correctly\n", equipment_checked);

    rc_test_open_mapsquare(world, 3182, 3443, 0);
    int dummy_idx = runec_dev_validation_spawn_varrock_bank_dummy(world);
    assert(dummy_idx >= 0);
    RcNpc *dummy = &world->npcs[dummy_idx];
    int dummy_x = dummy->x;
    int dummy_y = dummy->y;
    assert(dummy->disable_wander);
    assert(dummy->force_player_max_hit);
    for (int i = 0; i < 64; i++)
        rc_npc_tick(world, dummy);
    assert(dummy->x == dummy_x);
    assert(dummy->y == dummy_y);

    const RuneCDevTransport *kbd =
        runec_dev_validation_find_transport("kbd");
    assert(kbd && kbd->npc_id == 2266);

    const RuneCDevTransport *graardor =
        runec_dev_validation_find_transport("graardor");
    assert(graardor);
    int encounter_count = 0;
    const RuneCDevEncounterNpc *encounter =
        runec_dev_validation_encounter_npcs(graardor, &encounter_count);
    assert(encounter && encounter_count == 4);
    assert(encounter[0].npc_id == 2215);
    assert(encounter[1].npc_id == 2216);
    assert(encounter[2].npc_id == 2217);
    assert(encounter[3].npc_id == 2218);

    world->player.x = 2872;
    world->player.y = 5350;
    world->player.plane = 2;
    rc_test_open_mapsquare(world, world->player.x, world->player.y,
                           world->player.plane);
    int prepared = runec_dev_validation_prepare_encounter(world, graardor);
    assert(prepared == 4);
    assert(rc_combat_is_multi_combat(world));
    for (int i = 0; i < encounter_count; i++) {
        int idx = rc_world_find_npc_near(world, encounter[i].npc_id,
                                         encounter[i].x, encounter[i].y,
                                         encounter[i].plane, 0);
        assert(idx >= 0);
        RcNpc *npc = &world->npcs[idx];
        assert(npc->disable_wander);
        assert(npc->target_uid == 0);
        assert(npc->attack_timer == 0);
    }

    rc_world_destroy(world);
    return 0;
}
