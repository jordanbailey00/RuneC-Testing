#include <assert.h>
#include <limits.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "items.h"
#include "interaction.h"
#include "object_runtime.h"
#include "player_command.h"
#include "storage_fixture.h"
#include "world_test_fixture.h"

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_STORAGE | RC_SUB_INVENTORY | RC_SUB_OBJECTS;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.object_defs_path = RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin";
    cfg.object_behaviors_path = RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    int npc_count = 0;
    assert(w && rc_npc_defs_all(&npc_count) && npc_count > 0
           && "bankers must load without Shops/Combat");
    rc_test_open_mapsquare(w, w->player.x, w->player.y, w->player.plane);
    test_storage_open(w);
    return w;
}

static void result(RcWorld *w, RcBankResultCode code, int moved) {
    rc_world_tick(w);
    if (w->player.bank_result.code != code || w->player.bank_result.moved != moved)
        fprintf(stderr, "bank result: expected %d/%d, got %d/%" PRId64 " (%s)\n",
                code, moved, w->player.bank_result.code, w->player.bank_result.moved,
                rc_bank_result_message(&w->player.bank_result));
    assert(w->player.bank_result.code == code);
    assert(w->player.bank_result.moved == moved);
}

static int inv_add(RcWorld *w, int id, int qty, uint32_t state) {
    assert(rc_player_inventory_add(w, id, qty, state).code == RC_ITEM_RESULT_OK);
    return rc_inv_find(w->player.inventory, id);
}

static int find_bank(RcWorld *w, int id, uint32_t state) {
    for (int i = 0; i < RC_BANK_SIZE; i++)
        if (w->player.bank[i].item_id == id && w->player.bank[i].state_id == state) return i;
    return -1;
}

static void deposited_equipment(RcWorld *w, int event, const void *payload, void *ctx) {
    (void)event;
    const RcPayloadItemEvent *item = payload;
    assert(w->player.equipment[item->slot].item_id < 0);
    assert(find_bank(w, (int)item->item_id, item->item_id == 11905 ? 7 : 0) >= 0);
    (*(int *)ctx)++;
}

static void deposit_containers(void) {
    RcWorld *w = make_world();
    assert(rc_bank_deposit_all(w,false)); result(w,RC_BANK_OK,0);
    assert(!strcmp(rc_bank_result_message(&w->player.bank_result),"You have nothing to deposit."));
    inv_add(w,995,INT_MAX,0);
    inv_add(w,556,INT_MAX,0);
    inv_add(w,1351,2,0);
    inv_add(w,rc_item_def_get(1351)->linked_id_noted,3,0);
    assert(rc_bank_deposit_all(w,false));
    rc_world_tick(w);
    assert(w->player.bank_result.code == RC_BANK_OK);
    assert(w->player.bank_result.moved == (int64_t)INT_MAX*2+5);
    assert(w->player.bank[find_bank(w,1351,0)].quantity == 5);
    assert(rc_inv_find(w->player.inventory,995) < 0);
    inv_add(w,8936,1,0);
    inv_add(w,385,3,0);
    assert(rc_bank_deposit_all(w,false)); result(w,RC_BANK_UNBANKABLE,3);
    assert(rc_inv_find(w->player.inventory,8936) >= 0);
    assert(strstr(rc_bank_result_message(&w->player.bank_result),"Some items remain"));
    assert(rc_bank_deposit_all(w,true)); result(w,RC_BANK_DISABLED,0);
    assert(rc_player_inventory_remove_item(w,8936,1).code == RC_ITEM_RESULT_OK);
    w->enabled |= RC_SUB_EQUIPMENT;
    for (int i = 0; i < SKILL_COUNT; i++) w->player.skills.base_level[i] = 99;
    int weapon = inv_add(w,11905,1,7);
    assert(rc_item_result_accepted(rc_player_equip(w,weapon)));
    rc_world_tick(w);
    int ammo = inv_add(w,892,50,0);
    assert(rc_item_result_accepted(rc_player_equip(w,ammo)));
    rc_world_tick(w);
    assert(w->player.equipment[EQUIP_WEAPON].item_id == 11905);
    assert(w->player.equipment[EQUIP_AMMO].quantity == 50);
    assert(w->player.equipment_bonuses[3] != 0);
    inv_add(w,385,RC_INVENTORY_SIZE,0);
    int events = 0;
    assert(rc_event_subscribe(w,RC_EVT_ITEM_UNEQUIPPED,deposited_equipment,&events) == 0);
    assert(rc_bank_deposit_all(w,true)); result(w,RC_BANK_OK,51);
    assert(events == 2 && w->player.equipment_bonuses[3] == 0);
    assert(w->player.bank[find_bank(w,11905,7)].quantity == 1);
    assert(w->player.bank[find_bank(w,892,0)].quantity == 50);
    assert(rc_inv_free_slot(w->player.inventory) < 0 && "worn deposits bypass inventory");
    assert(rc_bank_deposit_all(w,true)); result(w,RC_BANK_OK,0);
    assert(events == 2);
    assert(rc_bank_deposit_all(w,false));
    rc_storage_close(w);
    result(w,RC_BANK_CLOSED,0);
    assert(rc_inv_free_slot(w->player.inventory) < 0);
    test_storage_open(w);
    w->player.storage_kind = RC_STORAGE_DEPOSIT_BOX;
    // Model a deposit-only capability on the same admitted banker fixture.
    RcStorageBinding binding = {w->player.storage_source.definition_id,0,
                                (uint8_t)(1u << w->player.storage_option)};
    RcStoragePolicy policy = *w->storage_policy;
    policy.npcs = &binding; policy.npc_count = 1; w->storage_policy = &policy;
    assert(rc_bank_deposit_all(w,false)); result(w,RC_BANK_OK,RC_INVENTORY_SIZE);
    rc_world_destroy(w);

    w = make_world();
    int coins = rc_bank_add_item(w,995,INT_MAX-2);
    for (int i = 1; i < RC_BANK_SIZE; i++) w->player.bank[i] = (RcInvSlot){.item_id=385,.quantity=1};
    inv_add(w,995,5,0); inv_add(w,1351,1,0);
    assert(rc_bank_deposit_all(w,false)); result(w,RC_BANK_PARTIAL,2);
    assert(w->player.bank[coins].quantity == INT_MAX);
    assert(w->player.inventory[0].quantity == 3 && w->player.inventory[1].item_id == 1351);
    w->player.is_dead = true;
    assert(!rc_bank_deposit_all(w,false) && w->player.bank_result.code == RC_BANK_QUEUE_REJECTED);
    rc_world_destroy(w);
}

static void queued_identity(void) {
    RcWorld *w = make_world();
    int coins = rc_bank_add_item(w, 995, 20), runes = rc_bank_add_item(w, 556, 20);
    assert(coins >= 0 && runes >= 0);
    assert(rc_bank_withdraw_slot(w, coins, 1));
    assert(rc_bank_withdraw_slot(w, runes, 1));
    assert(rc_bank_withdraw_slot(w, coins, 5));
    result(w, RC_BANK_OK, 5);
    assert(w->player.bank[coins].quantity == 14 && w->player.bank[runes].quantity == 19);
    assert(w->player.bank_result.sequence == 3);
    assert(rc_bank_withdraw_slot(w, coins, 0));
    assert(rc_bank_withdraw_slot(w, coins, 1));
    result(w, RC_BANK_STALE, 0);
    assert(w->player.inventory[rc_inv_find(w->player.inventory,995)].quantity == 20);

    assert(rc_bank_add_item(w, 995, 1) == coins);
    assert(rc_bank_withdraw_slot(w, coins, 1));
    w->in_tick = true;
    assert(rc_bank_withdraw_slot(w, coins, 1) == 1);
    assert(rc_bank_add_item(w, 995, 4) == coins);
    w->in_tick = false;
    result(w, RC_BANK_STALE, 0);
    assert(w->player.bank[coins].quantity == 4 && "clear/reuse invalidates the old click");

    int inv = rc_inv_find(w->player.inventory,995);
    assert(rc_bank_deposit_slot(w, inv, 1));
    assert(rc_player_inventory_remove_item(w,995,21).code == RC_ITEM_RESULT_OK);
    inv_add(w,556,1,0);
    result(w, RC_BANK_STALE, 0);
    rc_world_destroy(w);
}

static void quantities_notes_state(void) {
    RcWorld *w = make_world();
    int axe = inv_add(w,1351,3,0);
    assert(rc_bank_deposit_slot(w,axe,2));
    result(w,RC_BANK_OK,2);
    assert(w->player.bank[0].quantity == 2 && rc_inv_find(w->player.inventory,1351) >= 0);
    assert(rc_bank_deposit_slot(w,rc_inv_find(w->player.inventory,1351),0));
    result(w,RC_BANK_OK,1);
    assert(rc_inv_find(w->player.inventory,1351) < 0);
    assert(rc_bank_withdraw_slot_mode(w,0,0,true,false));
    result(w,RC_BANK_OK,3);
    int note = rc_item_def_get(1351)->linked_id_noted;
    assert(w->player.inventory[0].item_id == note && w->player.inventory[0].quantity == 3);
    assert(rc_bank_deposit_slot(w,0,0)); result(w,RC_BANK_OK,3);
    assert(w->player.bank[0].item_id == 1351);

    int whip = inv_add(w,4151,1,7);
    inv_add(w,4151,1,9);
    assert(rc_bank_deposit_slot(w,whip,0)); result(w,RC_BANK_OK,2);
    int a = find_bank(w,4151,7), b = find_bank(w,4151,9);
    assert(a >= 0 && b >= 0 && a != b);
    assert(rc_bank_withdraw_slot_mode(w,a,1,true,false)); result(w,RC_BANK_OK,1);
    assert(w->player.bank_result.cannot_note);
    assert(w->player.inventory[0].item_id == 4151 && w->player.inventory[0].state_id == 7);
    assert(rc_bank_withdraw_slot(w,b,1)); result(w,RC_BANK_OK,1);
    assert(w->player.inventory[1].state_id == 9);
    assert(rc_bank_withdraw_slot(w,0,-1)); result(w,RC_BANK_INVALID,0);
    assert(w->player.bank[0].quantity == 3);
    assert(rc_bank_withdraw_slot(w,0,1));
    assert(rc_bank_withdraw_slot_mode(w,0,0,false,true));
    result(w,RC_BANK_OK,1);
    assert(w->player.bank[0].quantity == 1 && "All-but-1 uses the current stack after prior clicks");
    assert(rc_bank_withdraw_slot_mode(w,0,0,false,true)); result(w,RC_BANK_OK,0);
    assert(rc_player_last_command_result(w,NULL) == RC_COMMAND_RESULT_EXECUTED);
    rc_world_destroy(w);
}

static void capacity_conservation(void) {
    RcWorld *w = make_world();
    int coins = rc_bank_add_item(w,995,INT_MAX-2);
    int inv = inv_add(w,995,5,0);
    assert(rc_bank_deposit_slot(w,inv,0)); result(w,RC_BANK_PARTIAL,2);
    assert(w->player.inventory[inv].quantity == 3 && w->player.bank[coins].quantity == INT_MAX);
    assert(rc_bank_deposit_slot(w,inv,1)); result(w,RC_BANK_CAPACITY,0);
    assert(rc_player_inventory_remove_item(w,995,3).code == RC_ITEM_RESULT_OK);
    inv = inv_add(w,995,INT_MAX-2,0);
    assert(rc_bank_withdraw_slot(w,coins,5)); result(w,RC_BANK_PARTIAL,2);
    assert(w->player.inventory[inv].quantity == INT_MAX && w->player.bank[coins].quantity == INT_MAX-2);
    assert(rc_bank_withdraw_slot(w,coins,1)); result(w,RC_BANK_CAPACITY,0);
    int axes = rc_bank_add_item(w,1351,100);
    inv_add(w,385,25,0);
    assert(rc_bank_withdraw_slot(w,axes,0)); result(w,RC_BANK_PARTIAL,2);
    assert(w->player.bank[axes].quantity == 98);
    assert(rc_bank_withdraw_slot(w,axes,1)); result(w,RC_BANK_CAPACITY,0);
    assert(rc_player_inventory_remove_item(w,995,INT_MAX).code == RC_ITEM_RESULT_OK);
    inv = inv_add(w,556,5,0);
    for (int i = 0; i < RC_BANK_SIZE; i++)
        if (w->player.bank[i].item_id < 0) w->player.bank[i] = (RcInvSlot){.item_id=385,.quantity=1};
    assert(rc_bank_deposit_slot(w,inv,0)); result(w,RC_BANK_CAPACITY,0);
    assert(w->player.inventory[inv].quantity == 5);
    rc_inv_remove(w->player.inventory,inv);
    inv = inv_add(w,995,2,0);
    assert(rc_bank_deposit_slot(w,inv,0)); result(w,RC_BANK_OK,2);
    assert(w->player.bank[coins].quantity == INT_MAX);
    rc_world_destroy(w);
}

static void policies_and_sessions(void) {
    RcWorld *w = make_world();
    const RcStoragePolicy *policy = w->storage_policy;
    assert(policy->item_bankable(8936) == 0 && "B237 Trouble Brewing flowers cannot be banked");
    assert(policy->item_bankable(24254) == 0 && policy->item_bankable(27543) == 0);
    assert(policy->item_bankable(-1) < 0 && policy->item_bankable(INT_MAX) < 0);
    int flower = inv_add(w,8936,1,0);
    assert(rc_bank_deposit_slot(w,flower,1)); result(w,RC_BANK_UNBANKABLE,0);
    assert(w->player.inventory[flower].item_id == 8936);
    assert(!rc_storage_kind_for_object(w,10735,0)); // food chute
    assert(!rc_storage_kind_for_object(w,30987,0)); // fossil store
    assert(!rc_storage_kind_for_object(w,10355,2)); // collection
    assert(rc_storage_kind_for_object(w,10355,1) == RC_STORAGE_BANK);
    assert(rc_storage_kind_for_object(w,10529,0) == RC_STORAGE_DEPOSIT_BOX);

    int coins = rc_bank_add_item(w,995,10);
    int banker_uid = w->player.storage_source.entity_uid;
    RcNpc *banker = rc_npc_resolve(w,banker_uid);
    int option = w->player.storage_option;
    rc_storage_close(w);
    banker->plane++;
    rc_player_interact_npc(w,banker_uid,option);
    rc_world_tick(w);
    assert(!w->player.storage_session && "other-plane banker cannot grant access");
    banker->plane--;
    banker->x += 10;
    rc_player_interact_npc(w,banker_uid,option);
    rc_world_tick(w);
    assert(!w->player.storage_session && "must arrive, not just submit a remote bank click");
    rc_player_cancel_action(w,RC_ACTION_CANCEL_REPLACED);
    banker->x = w->player.x+1;
    test_storage_open(w);
    banker->y++; // A harmless step must not revoke an already valid session.
    assert(rc_bank_withdraw_slot(w,coins,1)); result(w,RC_BANK_OK,1);
    banker->is_dead = true;
    assert(rc_bank_withdraw_slot(w,coins,1)); result(w,RC_BANK_CLOSED,0);
    assert(!w->player.storage_session);
    test_storage_open(w);
    assert(rc_player_close_storage(w));
    assert(rc_bank_withdraw_slot(w,coins,1)); result(w,RC_BANK_CLOSED,0);

    test_storage_open(w);
    assert(rc_bank_withdraw_slot(w,coins,1));
    uint64_t session = w->player.storage_session;
    rc_storage_close(w);
    // Same bank reopened by a different accepted interaction cannot inherit old input.
    w->player.storage_session = session + 1;
    result(w,RC_BANK_CLOSED,0);
    rc_storage_close(w);
    w->player.storage_kind = RC_STORAGE_BANK;
    w->in_tick = true;
    assert(rc_bank_withdraw_slot(w,coins,1) < 0 && "an open flag is not banking authority");
    w->in_tick = false;
    test_storage_open(w);
    banker_uid = w->player.storage_source.entity_uid;
    assert(rc_npc_remove(w,banker_uid));
    assert(!w->player.storage_session);
    test_storage_open(w);
    assert(rc_world_relocate_player(w,w->player.x,w->player.y,w->player.plane));
    assert(!w->player.storage_session);
    assert(rc_world_reset(w));
    assert(!w->storage_policy && !w->player.storage_session && w->player.bank[coins].item_id == -1);
    rc_content_storage_register(w);
    assert(w->storage_policy == policy);
    rc_world_destroy(w);
}

static void object_access(void) {
    RcWorld *w = make_world();
    rc_storage_close(w);
    rc_player_interact_object(w,10355,1);
    rc_world_tick(w);
    assert(!w->player.storage_session && "definition-only open without a placement is rejected");
    RcObjectPlacement box = {.obj_id=10529,.x=w->player.x+1,.y=w->player.y,.plane=w->player.plane,.type=10};
    uint64_t key;
    assert(rc_world_object_add(w,&box,0,&key) == RC_OBJECT_MUTATION_OK);
    assert(rc_player_interact_object_placement(w,10529,box.x,box.y,box.plane,key,0));
    for (int i = 0; i < 10 && !w->player.storage_session; i++) rc_world_tick(w);
    if (w->player.storage_kind != RC_STORAGE_DEPOSIT_BOX) {
        const RcInteractionOutcome *outcome = rc_interaction_last_outcome(&w->player);
        fprintf(stderr, "deposit box interaction: %s; active=%d flags=%u\n",
                outcome->message, w->player.interaction.active, w->player.interaction.flags);
    }
    assert(w->player.storage_kind == RC_STORAGE_DEPOSIT_BOX);
    int inv = inv_add(w,995,7,0);
    assert(rc_bank_deposit_slot(w,inv,0)); result(w,RC_BANK_OK,7);
    assert(rc_bank_withdraw_slot(w,0,1)); result(w,RC_BANK_INVALID,0);
    assert(rc_world_object_revert(w,key) == RC_OBJECT_MUTATION_OK);
    assert(!w->player.storage_session);
    rc_world_destroy(w);
}

static void replacement_closes_source(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_STORAGE | RC_SUB_INVENTORY | RC_SUB_OBJECTS;
    cfg.items_path = RC_TEST_SOURCE_DIR "/data/defs/items.bin";
    cfg.object_defs_path = RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin";
    cfg.object_behaviors_path = RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin";
    cfg.object_placements_path = RC_TEST_SOURCE_DIR "/data/regions/world.object-placements.indexed.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    rc_content_storage_register(w);
    int count = 0, index = -1, option = -1;
    const RcObjectPlacement *rows = rc_object_region_placements((49 << 8) | 53,&count);
    for (int i = 0; i < count && index < 0; i++)
        for (int op = 0; op < 5; op++)
            if (rows[i].plane == 0 && rc_storage_kind_for_object(w,rows[i].obj_id,op) == RC_STORAGE_BANK) {
                index = i; option = op; break;
            }
    assert(index >= 0);
    RcObjectPlacement base = rows[index];
    w->player.x = base.x-1; w->player.y = base.y; w->player.plane = base.plane;
    rc_test_open_mapsquare(w,w->player.x,w->player.y,w->player.plane);
    assert(rc_player_interact_object_placement(w,base.obj_id,base.x,base.y,base.plane,base.key,option));
    for (int i = 0; i < 10 && !w->player.storage_session; i++) rc_world_tick(w);
    assert(w->player.storage_session);
    assert(rc_world_object_replace(w,&base,&base,0,0) == RC_OBJECT_MUTATION_OK);
    assert(!w->player.storage_session);
    rc_world_destroy(w);
}

static int unreviewed(int id) { (void)id; return -1; }

static void failures_and_replay(void) {
    assert(rc_bank_deposit_slot(NULL,0,1) < 0);
    assert(!rc_player_close_storage(NULL) && !rc_storage_open_interaction(NULL));
    rc_storage_close(NULL);
    assert(!rc_storage_kind_for_object(NULL,10355,1));
    assert(!rc_storage_kind_for_npc(NULL,NULL,0));
    RcWorld *w = make_world();
    int coins = rc_bank_add_item(w,995,100);
    assert(rc_bank_add_item(w,-1,1) < 0 && rc_bank_add_item(w,995,0) < 0);
    assert(rc_bank_add_item_tab(w,995,1,-2) < 0 && rc_bank_add_item_tab(w,995,1,256) < 0);
    assert(!rc_storage_kind_for_object(w,10355,5));
    assert(!rc_storage_kind_for_object(w,-1,0));
    assert(!rc_storage_kind_for_npc(w,NULL,0));
    assert(rc_bank_withdraw_slot(w,RC_BANK_SIZE,1)); result(w,RC_BANK_INVALID,0);
    assert(rc_bank_withdraw_slot(w,1,1)); result(w,RC_BANK_STALE,0);
    w->in_tick = true;
    assert(rc_bank_withdraw_slot(w,-1,1) < 0 && w->player.bank_result.code == RC_BANK_INVALID);
    assert(rc_bank_withdraw_slot(w,1,1) < 0 && w->player.bank_result.code == RC_BANK_STALE);
    w->enabled &= ~RC_SUB_INVENTORY;
    assert(rc_bank_withdraw_slot(w,coins,1) < 0 && w->player.bank_result.code == RC_BANK_DISABLED);
    w->enabled |= RC_SUB_INVENTORY;
    int inv = inv_add(w,995,1,0);
    // A corrupt unrelated slot must reject the whole transaction, not lose the source.
    w->player.inventory[27] = (RcInvSlot){.item_id=1351,.quantity=2};
    assert(rc_bank_deposit_slot(w,inv,1) < 0 && w->player.bank_result.code == RC_BANK_CONFLICT);
    assert(rc_bank_withdraw_slot(w,coins,1) < 0 && w->player.bank_result.code == RC_BANK_CONFLICT);
    assert(w->player.inventory[inv].quantity == 1 && w->player.bank[coins].quantity == 100);
    rc_inv_remove(w->player.inventory,27);
    const RcStoragePolicy *original = w->storage_policy;
    RcStoragePolicy policy = *original;
    policy.item_bankable = unreviewed;
    w->storage_policy = &policy;
    assert(rc_bank_deposit_slot(w,inv,1) < 0 && w->player.bank_result.code == RC_BANK_UNREVIEWED);
    policy.item_bankable = NULL;
    assert(rc_bank_deposit_slot(w,inv,1) < 0 && w->player.bank_result.code == RC_BANK_UNREVIEWED);
    w->storage_policy = original;
    int note = rc_item_def_get(1351)->linked_id_noted;
    w->player.inventory[2] = (RcInvSlot){.item_id=note,.quantity=1,.state_id=1};
    assert(rc_bank_deposit_slot(w,2,1) < 0 && w->player.bank_result.code == RC_BANK_BAD_NOTE);
    w->player.inventory[2].state_id = 0;
    w->player.inventory[3] = (RcInvSlot){.item_id=note,.quantity=1,.state_id=1};
    assert(rc_bank_deposit_slot(w,2,0) < 0 && w->player.bank_result.code == RC_BANK_BAD_NOTE);
    assert(w->player.inventory[2].quantity == 1 && find_bank(w,1351,0) < 0);
    rc_inv_remove(w->player.inventory,2);
    rc_inv_remove(w->player.inventory,3);
    w->player.current_hp = 0;
    assert(rc_bank_withdraw_slot(w,coins,1) < 0 && w->player.bank_result.code == RC_BANK_CLOSED);
    w->player.current_hp = w->player.max_hp;
    test_storage_open(w);
    w->player.x++;
    assert(rc_bank_withdraw_slot(w,coins,1) < 0 && w->player.bank_result.code == RC_BANK_CLOSED);
    w->player.x--;
    test_storage_open(w);
    w->in_tick = false;
    for (int i = 0; i < 1000; i++) {
        assert(rc_bank_withdraw_slot(w,coins,1)); result(w,RC_BANK_OK,1);
        assert(rc_bank_deposit_slot(w,inv,1)); result(w,RC_BANK_OK,1);
        assert(w->player.bank[coins].quantity == 100 && w->player.inventory[inv].quantity == 1);
    }
    w->player.is_dead = true;
    assert(!rc_bank_withdraw_slot(w,coins,1));
    assert(w->player.bank_result.code == RC_BANK_QUEUE_REJECTED);
    for (int code = RC_BANK_PARTIAL; code <= RC_BANK_QUEUE_REJECTED; code++) {
        RcBankResult r = {.code=code};
        assert(rc_bank_result_message(&r));
    }
    RcBankResult r = {.code=RC_BANK_OK};
    assert(!rc_bank_result_message(&r));
    r.cannot_note = true;
    assert(rc_bank_result_message(&r));
    r.code = (RcBankResultCode)999;
    assert(rc_bank_result_message(&r));
    rc_world_destroy(w);
}

static void malformed_notes(void) {
    RcWorld *w = make_world();
    RcItemDef defs[3] = {0};
    defs[1] = *rc_item_def_get(1351);
    defs[1].id = 1;
    defs[1].linked_id_noted = 2;
    defs[2] = *rc_item_def_get(rc_item_def_get(1351)->linked_id_noted);
    defs[2].id = 2;
    defs[2].linked_id_item = -1;
    rc_item_use_defs(defs,3);
    w->in_tick = true;
    assert(rc_bank_add_item(w,1,1) == 0);
    assert(rc_bank_withdraw_slot_mode(w,0,1,true,false) < 0 && w->player.bank_result.code == RC_BANK_BAD_NOTE);
    w->player.inventory[0] = (RcInvSlot){.item_id=2,.quantity=1};
    assert(rc_bank_deposit_slot(w,0,1) < 0 && w->player.bank_result.code == RC_BANK_BAD_NOTE);
    assert(w->player.bank[0].quantity == 1 && w->player.inventory[0].quantity == 1);
    rc_item_use_defs(g_item_defs,g_item_def_count);
    rc_world_destroy(w);
}

int main(void) {
    queued_identity();
    quantities_notes_state();
    capacity_conservation();
    policies_and_sessions();
    object_access();
    replacement_closes_source();
    failures_and_replay();
    malformed_notes();
    deposit_containers();
    puts("Banking: queued identity, amounts/notes/state, capacity, policy, source/session and reset passed.");
    return 0;
}
