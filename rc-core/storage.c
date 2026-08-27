#include "storage.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "objects.h"
#include "player_actions.h"
#include "player_command.h"

#include <limits.h>
#include <string.h>

static int has_word(const char *s, const char *word) {
    return s && strstr(s, word) != NULL;
}

int rc_storage_kind_for_object(int obj_id, int option) {
    if (option < 0 || option >= RC_OBJECT_ACTIONS) return RC_STORAGE_NONE;
    const RcObjectBehavior *behavior = rc_object_behavior_get(obj_id);
    const RcObjectDef *def = rc_object_def_get(obj_id);
    if (!behavior || !def || !(behavior->action_mask & (1u << option))) {
        return RC_STORAGE_NONE;
    }
    const char *action = def->actions[option];
    if (has_word(action, "Deposit") || has_word(action, "deposit")) {
        return RC_STORAGE_DEPOSIT_BOX;
    }
    if (has_word(action, "Collect") || has_word(action, "collect")) {
        return RC_STORAGE_COLLECTION;
    }
    if (behavior->flags & RC_OBJ_BEHAVIOR_BANK) return RC_STORAGE_BANK;
    if (behavior->flags & RC_OBJ_BEHAVIOR_STORAGE) return RC_STORAGE_CONTAINER;
    return RC_STORAGE_NONE;
}

int rc_storage_kind_for_npc(const RcNpcDef *def, int option) {
    if (!def || option < 0 || option >= RC_NPC_OPTION_COUNT)
        return RC_STORAGE_NONE;
    const char *action = rc_npc_def_option(def, option);
    if (!action || !action[0])
        return RC_STORAGE_NONE;
    if (has_word(action, "Bank") || has_word(action, "bank"))
        return RC_STORAGE_BANK;
    if (has_word(action, "Collect") || has_word(action, "collect"))
        return RC_STORAGE_COLLECTION;
    return RC_STORAGE_NONE;
}

int rc_player_open_storage_object(RcWorld *world, int obj_id, int option) {
    if (rc_player_command_should_queue(world)) {
        int args[8] = {obj_id, option, 0, 0, 0, 0, 0, 0};
        return rc_player_command_submit(world,
                                        RC_PLAYER_COMMAND_OPEN_STORAGE_OBJECT,
                                        RC_ACTION_CATEGORY_NORMAL, args, 0);
    }
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return RC_STORAGE_NONE;
    if (!rc_player_action_allowed(world->enabled,
                                  RC_PLAYER_ACTION_INTERACT_OBJECT)) {
        return RC_STORAGE_NONE;
    }
    int kind = rc_storage_kind_for_object(obj_id, option);
    if (!kind) return RC_STORAGE_NONE;
    world->player.storage_kind = kind;
    world->player.storage_target = obj_id;
    world->player.storage_option = option;
    world->player.interact_type = RC_INTERACT_OBJECT;
    world->player.interact_target = obj_id;
    world->player.interact_option = option;
    return kind;
}

int rc_player_open_storage_npc(RcWorld *world, int npc_uid, int option) {
    if (rc_player_command_should_queue(world)) {
        int args[8] = {npc_uid, option, 0, 0, 0, 0, 0, 0};
        return rc_player_command_submit(world,
                                        RC_PLAYER_COMMAND_OPEN_STORAGE_NPC,
                                        RC_ACTION_CATEGORY_NORMAL, args, 0);
    }
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return RC_STORAGE_NONE;
    if (!rc_player_action_allowed(world->enabled,
                                  RC_PLAYER_ACTION_INTERACT_NPC)) {
        return RC_STORAGE_NONE;
    }
    RcNpc *npc = rc_npc_resolve(world, npc_uid);
    const RcNpcDef *def = rc_npc_def_for_npc(world, npc);
    if (!npc || npc->is_dead || !def) {
        return RC_STORAGE_NONE;
    }
    int kind = rc_storage_kind_for_npc(def, option);
    if (!kind) return RC_STORAGE_NONE;
    world->player.storage_kind = kind;
    world->player.storage_target = npc_uid;
    world->player.storage_option = option;
    world->player.interact_type = RC_INTERACT_NPC;
    world->player.interact_target = npc_uid;
    world->player.interact_option = option;
    return kind;
}

int rc_player_close_storage(RcWorld *world) {
    if (rc_player_command_should_queue(world)) {
        int args[8] = {0};
        return rc_player_command_submit(world,
                                        RC_PLAYER_COMMAND_CLOSE_STORAGE,
                                        RC_ACTION_CATEGORY_SOFT, args, 0);
    }
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return 0;
    world->player.storage_kind = RC_STORAGE_NONE;
    world->player.storage_target = -1;
    world->player.storage_option = -1;
    if (world->player.interact_type == RC_INTERACT_OBJECT ||
            world->player.interact_type == RC_INTERACT_NPC) {
        world->player.interact_type = RC_INTERACT_NONE;
        world->player.interact_target = -1;
        world->player.interact_option = -1;
    }
    return 1;
}

static int unnoted_item_id(int item_id) {
    const RcItemDef *def = rc_item_def_get(item_id);
    if (def && def->noted && def->linked_id_item >= 0) {
        const RcItemDef *linked = rc_item_def_get(def->linked_id_item);
        if (linked && !linked->noted && !linked->placeholder)
            return linked->id;
    }
    return item_id;
}

static int bank_add_global(RcInvSlot *bank, uint8_t *tabs,
                           int item_id, int quantity, uint32_t state_id,
                           int new_tab) {
    item_id = unnoted_item_id(item_id);
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == item_id && bank[i].state_id == state_id) {
            if (bank[i].quantity > INT_MAX - quantity) return -1;
            bank[i].quantity += quantity;
            return i;
        }
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == -1) {
            bank[i].item_id = item_id;
            bank[i].quantity = quantity;
            bank[i].state_id = state_id;
            if (tabs) tabs[i] = (uint8_t)(new_tab < 0 ? 0 : new_tab);
            return i;
        }
    }
    return -1;
}

static int bank_add_tabbed(RcInvSlot *bank, uint8_t *tabs,
                           int item_id, int quantity, uint32_t state_id,
                           int tab) {
    item_id = unnoted_item_id(item_id);
    if (tab < 0) tab = 0;
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == item_id && bank[i].state_id == state_id
                && tabs && tabs[i] == (uint8_t)tab) {
            if (bank[i].quantity > INT_MAX - quantity) return -1;
            bank[i].quantity += quantity;
            return i;
        }
    }
    for (int i = 0; i < RC_BANK_SIZE; i++) {
        if (bank[i].item_id == -1) {
            bank[i].item_id = item_id;
            bank[i].quantity = quantity;
            bank[i].state_id = state_id;
            if (tabs) tabs[i] = (uint8_t)tab;
            return i;
        }
    }
    return -1;
}

int rc_bank_add_item(RcWorld *world, int item_id, int quantity) {
    if (!world || !(world->enabled & RC_SUB_STORAGE) || item_id < 0 ||
            quantity <= 0) {
        return -1;
    }
    int slot = bank_add_global(world->player.bank, world->player.bank_tab,
                               item_id, quantity, 0, 0);
    if (slot >= 0) world->player.bank_revision++;
    return slot;
}

int rc_bank_add_item_tab(RcWorld *world, int item_id, int quantity, int tab) {
    if (!world || !(world->enabled & RC_SUB_STORAGE) || item_id < 0 ||
            quantity <= 0) {
        return -1;
    }
    int slot = bank_add_tabbed(world->player.bank, world->player.bank_tab,
                               item_id, quantity, 0, tab);
    if (slot >= 0) world->player.bank_revision++;
    return slot;
}

int rc_bank_deposit_slot(RcWorld *world, int inv_slot, int quantity) {
    if (rc_player_command_should_queue(world)) {
        uint32_t generation = world && inv_slot >= 0
                            && inv_slot < RC_INVENTORY_SIZE
                            ? world->player.inventory[inv_slot].generation : 0;
        int args[8] = {
            inv_slot, quantity, (int)generation, 0, 0, 0, 0, 0
        };
        return rc_player_command_submit(world, RC_PLAYER_COMMAND_BANK_DEPOSIT,
                                        RC_ACTION_CATEGORY_BACKGROUND,
                                        args, 0);
    }
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return -1;
    if (world->player.storage_kind == RC_STORAGE_NONE
            || world->player.storage_kind == RC_STORAGE_COLLECTION) {
        return -1;
    }
    if (inv_slot < 0 || inv_slot >= RC_INVENTORY_SIZE) return -1;
    RcInvSlot item = world->player.inventory[inv_slot];
    if (item.item_id < 0 || item.quantity <= 0) return -1;
    int move = quantity <= 0 || quantity > item.quantity
             ? item.quantity : quantity;
    RcInvSlot staged_bank[RC_BANK_SIZE];
    uint8_t staged_tabs[RC_BANK_SIZE];
    memcpy(staged_bank, world->player.bank, sizeof(staged_bank));
    memcpy(staged_tabs, world->player.bank_tab, sizeof(staged_tabs));
    if (bank_add_global(staged_bank, staged_tabs, item.item_id, move,
                        item.state_id, 0) < 0) {
        return -1;
    }
    RcItemTransaction tx;
    RcItemActionResult result = rc_item_tx_begin(&tx, world);
    if (result.code == RC_ITEM_RESULT_OK) {
        result = rc_item_tx_remove_slot(
            &tx, inv_slot, move, item.generation);
    }
    if (result.code != RC_ITEM_RESULT_OK
            || rc_item_tx_commit(&tx).code != RC_ITEM_RESULT_OK) {
        return -1;
    }
    memcpy(world->player.bank, staged_bank, sizeof(staged_bank));
    memcpy(world->player.bank_tab, staged_tabs, sizeof(staged_tabs));
    world->player.bank_revision++;
    return move;
}

int rc_bank_withdraw_slot(RcWorld *world, int bank_slot, int quantity) {
    if (rc_player_command_should_queue(world)) {
        uint32_t revision = world ? world->player.bank_revision : 0;
        int item_id = world && bank_slot >= 0 && bank_slot < RC_BANK_SIZE
                    ? world->player.bank[bank_slot].item_id : -1;
        int args[8] = {
            bank_slot, quantity, (int)revision, item_id, 0, 0, 0, 0
        };
        return rc_player_command_submit(world,
                                        RC_PLAYER_COMMAND_BANK_WITHDRAW,
                                        RC_ACTION_CATEGORY_BACKGROUND,
                                        args, 0);
    }
    if (!world || !(world->enabled & RC_SUB_STORAGE)) return -1;
    if (world->player.storage_kind != RC_STORAGE_BANK
            && world->player.storage_kind != RC_STORAGE_CONTAINER) {
        return -1;
    }
    if (bank_slot < 0 || bank_slot >= RC_BANK_SIZE) return -1;
    RcInvSlot item = world->player.bank[bank_slot];
    if (item.item_id < 0 || item.quantity <= 0) return -1;
    int move = quantity <= 0 || quantity > item.quantity
             ? item.quantity : quantity;
    const RcItemDef *def = rc_item_def_get(item.item_id);
    if (!def) return -1;
    if (!def->stackable) {
        int free_slots = 0;
        for (int i = 0; i < RC_INVENTORY_SIZE; i++) {
            if (world->player.inventory[i].item_id < 0) free_slots++;
        }
        if (move > free_slots) move = free_slots;
    }
    if (move <= 0) return -1;

    RcItemTransaction tx;
    RcItemActionResult result = rc_item_tx_begin(&tx, world);
    if (result.code == RC_ITEM_RESULT_OK)
        result = rc_item_tx_add(&tx, item.item_id, move, item.state_id);
    if (result.code != RC_ITEM_RESULT_OK) return -1;

    RcInvSlot staged = item;
    staged.quantity -= move;
    if (staged.quantity == 0) staged = (RcInvSlot){.item_id = -1};
    if (rc_item_tx_commit(&tx).code != RC_ITEM_RESULT_OK) return -1;
    world->player.bank[bank_slot] = staged;
    if (staged.item_id < 0) world->player.bank_tab[bank_slot] = 0;
    world->player.bank_revision++;
    return move;
}
