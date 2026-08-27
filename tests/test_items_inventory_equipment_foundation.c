#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "events.h"
#include "items.h"
#include "storage.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"

typedef struct {
    int equipped;
    int unequipped;
} ItemEvents;

static void count_item_event(RcWorld *world, int evt, const void *payload,
                             void *ctx) {
    (void)world;
    (void)payload;
    ItemEvents *events = ctx;
    if (evt == RC_EVT_ITEM_EQUIPPED) events->equipped++;
    if (evt == RC_EVT_ITEM_UNEQUIPPED) events->unequipped++;
}

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_INVENTORY | RC_SUB_EQUIPMENT | RC_SUB_LOOT
                   | RC_SUB_STORAGE;
    cfg.items_path = ITEM_PATH;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world);
    world->player.skills.base_level[SKILL_ATTACK] = 99;
    world->player.skills.base_level[SKILL_DEFENCE] = 99;
    world->player.skills.base_level[SKILL_STRENGTH] = 99;
    return world;
}

static void assert_ok(RcItemActionResult result) {
    assert(result.code == RC_ITEM_RESULT_OK);
}

static void metadata_is_truthful(void) {
    RcWorld *world = make_world();
    const RcItemDef *ags = rc_item_def_get(11802);
    const RcItemDef *whip = rc_item_def_get(4151);
    const RcItemDef *spotted_cape = rc_item_def_get(10073);
    assert(ags && whip && spotted_cape);
    assert((ags->metadata_flags & RC_ITEM_META_CACHE) != 0);
    assert((ags->metadata_flags & RC_ITEM_META_EQUIP) != 0);
    assert((ags->equip_conflicts & (1u << EQUIP_WEAPON)) != 0);
    assert((ags->equip_conflicts & (1u << EQUIP_SHIELD)) != 0);
    assert(strcmp(whip->inventory_actions[1], "Wield") == 0);
    assert(whip->examine[0] != '\0');
    assert(spotted_cape->weight_grams < 0);
    rc_world_destroy(world);
}

static void arithmetic_and_atomicity_are_checked(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    uint32_t before_revision = player->inventory_revision;

    assert_ok(rc_player_inventory_add(world, 995, INT_MAX, 0));
    int coins = rc_inv_find(player->inventory, 995);
    assert(coins >= 0 && player->inventory[coins].quantity == INT_MAX);
    uint32_t coins_generation = player->inventory[coins].generation;
    RcItemActionResult overflow = rc_player_inventory_add(world, 995, 1, 0);
    assert(overflow.code == RC_ITEM_RESULT_STACK_LIMIT);
    assert(player->inventory[coins].quantity == INT_MAX);
    assert(player->inventory[coins].generation == coins_generation);
    assert(player->inventory_revision == before_revision + 1);

    RcItemTransaction split;
    assert_ok(rc_item_tx_begin(&split, world));
    assert_ok(rc_item_tx_move_slot(
        &split, coins, 2, 10, coins_generation));
    assert_ok(rc_item_tx_commit(&split));
    assert(player->inventory[coins].quantity == INT_MAX - 10);
    assert(player->inventory[2].quantity == 10);
    assert(player->inventory[2].state_id == 0);

    RcItemTransaction tx;
    assert_ok(rc_item_tx_begin(&tx, world));
    assert_ok(rc_item_tx_add(&tx, 4151, 1, 0));
    RcItemActionResult missing = rc_item_tx_remove_item(&tx, 554, 1);
    assert(missing.code == RC_ITEM_RESULT_EMPTY);
    assert(rc_item_tx_commit(&tx).code == RC_ITEM_RESULT_CONFLICT);
    assert(rc_inv_find(player->inventory, 4151) < 0);

    rc_world_destroy(world);
}

static void state_and_slot_identity_are_preserved(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    assert_ok(rc_player_inventory_add(world, 995, 5, 11));
    assert_ok(rc_player_inventory_add(world, 995, 7, 22));
    assert(player->inventory[0].item_id == 995);
    assert(player->inventory[1].item_id == 995);
    assert(player->inventory[0].state_id == 11);
    assert(player->inventory[1].state_id == 22);

    uint32_t stale_generation = player->inventory[0].generation;
    RcItemActionResult queued = rc_player_move_inventory_item(world, 0, 2);
    assert(queued.code == RC_ITEM_RESULT_QUEUED);
    world->in_tick = true;
    RcItemTransaction tx;
    assert_ok(rc_item_tx_begin(&tx, world));
    assert_ok(rc_item_tx_remove_slot(&tx, 0, 5, stale_generation));
    assert_ok(rc_item_tx_add(&tx, 995, 5, 11));
    assert_ok(rc_item_tx_commit(&tx));
    world->in_tick = false;
    assert(player->inventory[0].item_id == 995);
    assert(player->inventory[0].generation != stale_generation);

    rc_world_tick(world);
    assert(player->last_item_action.code == RC_ITEM_RESULT_STALE);
    assert(player->inventory[2].item_id < 0);

    uint32_t interaction_generation = player->inventory[0].generation;
    assert(rc_player_interact_inventory_item(world, 0, 0));
    world->in_tick = true;
    assert_ok(rc_item_tx_begin(&tx, world));
    assert_ok(rc_item_tx_remove_slot(
        &tx, 0, 5, interaction_generation));
    assert_ok(rc_item_tx_add(&tx, 995, 5, 11));
    assert_ok(rc_item_tx_commit(&tx));
    world->in_tick = false;
    rc_world_tick(world);
    assert(player->last_item_action.code == RC_ITEM_RESULT_STALE);
    assert(!player->interaction.active);
    rc_world_destroy(world);
}

static void multi_slot_displacement_is_atomic(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    assert_ok(rc_player_inventory_add(world, 4151, 1, 0));
    assert_ok(rc_player_inventory_add(world, 10352, 1, 0));
    assert_ok(rc_player_inventory_add(world, 11802, 1, 0));
    world->in_tick = true;
    assert_ok(rc_player_equip(world, rc_inv_find(player->inventory, 4151)));
    assert_ok(rc_player_equip(world, rc_inv_find(player->inventory, 10352)));
    assert_ok(rc_player_inventory_add(world, 1351, 27, 0));
    int ags_slot = rc_inv_find(player->inventory, 11802);
    assert(ags_slot >= 0);
    uint32_t inventory_revision = player->inventory_revision;
    uint32_t equipment_revision = player->equipment_revision;

    RcItemActionResult result = rc_player_equip(world, ags_slot);
    world->in_tick = false;
    assert(result.code == RC_ITEM_RESULT_CAPACITY);
    assert(player->inventory_revision == inventory_revision);
    assert(player->equipment_revision == equipment_revision);
    assert(player->inventory[ags_slot].item_id == 11802);
    assert(player->equipment[EQUIP_WEAPON].item_id == 4151);
    assert(player->equipment[EQUIP_SHIELD].item_id == 10352);
    rc_world_destroy(world);
}

static void equipment_conflicts_are_symmetric(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    ItemEvents events = {0};
    assert(rc_event_subscribe(world, RC_EVT_ITEM_EQUIPPED,
                              count_item_event, &events) == 0);
    assert(rc_event_subscribe(world, RC_EVT_ITEM_UNEQUIPPED,
                              count_item_event, &events) == 0);

    assert_ok(rc_player_inventory_add(world, 10352, 1, 0));
    assert_ok(rc_player_inventory_add(world, 11802, 1, 0));
    world->in_tick = true;
    assert_ok(rc_player_equip(world, rc_inv_find(player->inventory, 10352)));
    assert_ok(rc_player_equip(world, rc_inv_find(player->inventory, 11802)));
    assert(player->equipment[EQUIP_WEAPON].item_id == 11802);
    assert(player->equipment[EQUIP_SHIELD].item_id < 0);

    assert_ok(rc_player_equip(world, rc_inv_find(player->inventory, 10352)));
    world->in_tick = false;
    assert(player->equipment[EQUIP_SHIELD].item_id == 10352);
    assert(player->equipment[EQUIP_WEAPON].item_id < 0);
    assert(rc_inv_find(player->inventory, 11802) >= 0);
    assert(events.equipped == 3);
    assert(events.unequipped == 2);
    rc_world_destroy(world);
}

static void failures_are_structured(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    player->skills.base_level[SKILL_ATTACK] = 1;
    assert_ok(rc_player_inventory_add(world, 4151, 1, 0));
    world->in_tick = true;
    RcItemActionResult result = rc_player_equip(
        world, rc_inv_find(player->inventory, 4151));
    world->in_tick = false;
    assert(result.code == RC_ITEM_RESULT_REQUIREMENT);
    assert(result.required_skill == SKILL_ATTACK);
    assert(result.required_level == 70);
    assert(player->equipment[EQUIP_WEAPON].item_id < 0);
    assert(player->last_item_action.code == RC_ITEM_RESULT_REQUIREMENT);
    rc_world_destroy(world);
}

static void state_survives_ground_handoff(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    assert_ok(rc_player_inventory_add(world, 4151, 1, 77));
    int slot = rc_inv_find(player->inventory, 4151);
    rc_player_drop_item(world, slot);
    rc_world_tick(world);
    assert(world->ground_item_count == 1);
    assert(world->ground_items[0].state_id == 77);
    rc_player_pickup_item(world, 0);
    rc_world_tick(world);
    slot = rc_inv_find(player->inventory, 4151);
    assert(slot >= 0 && player->inventory[slot].state_id == 77);
    rc_world_destroy(world);
}

static void storage_handoff_preserves_state_and_rolls_back(void) {
    RcWorld *world = make_world();
    RcPlayer *player = &world->player;
    player->storage_kind = RC_STORAGE_BANK;
    assert_ok(rc_player_inventory_add(world, 995, 1, 0));
    int coins = rc_inv_find(player->inventory, 995);
    player->bank[0] = (RcInvSlot){995, INT_MAX, 0, 0};
    uint32_t inventory_revision = player->inventory_revision;
    world->in_tick = true;
    assert(rc_bank_deposit_slot(world, coins, 1) == -1);
    assert(player->inventory[coins].quantity == 1);
    assert(player->inventory_revision == inventory_revision);
    assert(player->bank[0].quantity == INT_MAX);

    player->bank[0] = (RcInvSlot){.item_id = -1};
    assert_ok(rc_player_inventory_add(world, 4151, 1, 77));
    int whip = rc_inv_find(player->inventory, 4151);
    assert(rc_bank_deposit_slot(world, whip, 1) == 1);
    assert(player->bank[0].item_id == 4151);
    assert(player->bank[0].state_id == 77);
    assert(rc_bank_withdraw_slot(world, 0, 1) == 1);
    world->in_tick = false;
    whip = rc_inv_find(player->inventory, 4151);
    assert(whip >= 0 && player->inventory[whip].state_id == 77);
    rc_world_destroy(world);
}

int main(void) {
    metadata_is_truthful();
    arithmetic_and_atomicity_are_checked();
    state_and_slot_identity_are_preserved();
    multi_slot_displacement_is_atomic();
    equipment_conflicts_are_symmetric();
    failures_are_structured();
    state_survives_ground_handoff();
    storage_handoff_preserves_state_and_rolls_back();
    printf("test_items_inventory_equipment_foundation: item foundation OK.\n");
    return 0;
}
