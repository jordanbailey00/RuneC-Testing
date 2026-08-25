#include "player_command.h"

#include "api.h"
#include "combat.h"
#include "interaction.h"
#include "pathfinding.h"
#include "skills.h"
#include "storage.h"
#include "traversal.h"

#include <string.h>

static int command_args_valid(RcPlayerCommandKind kind,
                              RcActionCategory category) {
    return kind > RC_PLAYER_COMMAND_NONE
        && kind <= RC_PLAYER_COMMAND_PICKUP_ITEM
        && category >= RC_ACTION_CATEGORY_SOFT
        && category <= RC_ACTION_CATEGORY_STRONG;
}

int rc_player_command_should_queue(const RcWorld *world) {
    return world && !world->in_tick;
}

int rc_player_command_submit(RcWorld *world, RcPlayerCommandKind kind,
                             RcActionCategory category,
                             const int args[8], uint64_t key) {
    if (!world || !command_args_valid(kind, category)) return 0;
    RcPlayerCommandQueue *queue = &world->player_commands;
    if (world->player.is_dead || world->player.current_hp <= 0) {
        queue->last_result = RC_COMMAND_RESULT_REJECTED_DEAD;
        queue->rejected_count++;
        return 0;
    }
    if (world->player_action.active
            && world->player_action.category == RC_ACTION_CATEGORY_STRONG
            && world->player_action.ready_tick > world->tick
            && category != RC_ACTION_CATEGORY_SOFT) {
        queue->last_result = RC_COMMAND_RESULT_REJECTED_BUSY;
        queue->rejected_count++;
        return 0;
    }
    if (queue->count >= RC_MAX_PLAYER_COMMANDS) {
        queue->last_result = RC_COMMAND_RESULT_REJECTED_FULL;
        queue->rejected_count++;
        return 0;
    }
    RcPlayerCommand *command =
        &world->player_command_storage[queue->count++];
    memset(command, 0, sizeof(*command));
    command->kind = (uint8_t)kind;
    command->category = (uint8_t)category;
    command->sequence = ++queue->next_sequence;
    if (args) memcpy(command->args, args, sizeof(command->args));
    command->key = key;
    queue->last_sequence = command->sequence;
    queue->last_result = RC_COMMAND_RESULT_QUEUED;
    return 1;
}

static void stop_player_combat(RcWorld *world) {
    int npc_uid = -1;
    if (world->player.combat.target.kind == RC_COMBAT_ACTOR_NPC)
        npc_uid = world->player.combat.target.uid;
    else if (world->player.attack_target >= 0)
        npc_uid = world->player.attack_target;
    RcCombatActorRef player = {RC_COMBAT_ACTOR_PLAYER, 0};
    rc_combat_stop_actor(world, player, RC_COMBAT_STATE_CANCELLED);
    if (npc_uid >= 0) {
        RcCombatActorRef npc = {RC_COMBAT_ACTOR_NPC, npc_uid};
        rc_combat_stop_actor(world, npc, RC_COMBAT_STATE_CANCELLED);
    }
}

static void cancel_player_activity(RcWorld *world,
                                   RcPlayerActionCancelReason reason,
                                   int clear_route) {
    if (!world) return;
    RcPlayerCommandQueue *queue = &world->player_commands;
    if (queue->count > 0) {
        queue->rejected_count += queue->count;
        queue->count = 0;
        queue->last_result = RC_COMMAND_RESULT_REJECTED_CANCELLED;
    }
    RcPlayer *player = &world->player;
    if (clear_route) rc_player_route_clear(player, RC_MOVEMENT_NONE);
    rc_interaction_cancel(player, RC_INTERACTION_FAIL_CANCELLED);
    stop_player_combat(world);
    player->manual_spell_cast = -1;
    player->pending_traversal_active = 0;
    player->pending_traversal_tick = 0;
    player->pending_traversal_x = -1;
    player->pending_traversal_y = -1;
    player->pending_traversal_plane = -1;
    player->skill_action = 0;
    player->skill_ready_tick = 0;
    player->storage_kind = RC_STORAGE_NONE;
    player->storage_target = -1;
    player->storage_option = -1;
    world->player_action.active = false;
    world->player_action.owner = RC_ACTION_OWNER_NONE;
    world->player_action.category = RC_ACTION_CATEGORY_SOFT;
    world->player_action.ready_tick = world->tick;
    world->player_action.last_cancel_reason = reason;
}

void rc_player_cancel_action(RcWorld *world,
                             RcPlayerActionCancelReason reason) {
    cancel_player_activity(world, reason, 1);
}

void rc_player_replace_action_with_movement(
    RcWorld *world, RcPlayerActionCancelReason reason) {
    cancel_player_activity(world, reason, 0);
}

static void set_action(RcWorld *world, RcPlayerActionOwner owner,
                       RcActionCategory category, RcTick ready_tick) {
    RcPlayerActionState *action = &world->player_action;
    if (!action->active || action->owner != owner)
        action->started_tick = world->tick;
    action->active = true;
    action->owner = owner;
    action->category = category;
    action->ready_tick = ready_tick;
}

void rc_player_action_refresh(RcWorld *world) {
    if (!world) return;
    RcPlayer *player = &world->player;
    RcPlayerActionState *action = &world->player_action;
    RcPlayerActionCancelReason last_reason = action->last_cancel_reason;
    if (player->pending_traversal_active) {
        set_action(world, RC_ACTION_OWNER_TRAVERSAL,
                   RC_ACTION_CATEGORY_STRONG,
                   player->pending_traversal_tick + 1);
        return;
    }
    if (action->active && action->category == RC_ACTION_CATEGORY_STRONG
            && action->ready_tick > world->tick) {
        return;
    }
    if (player->storage_kind != RC_STORAGE_NONE) {
        set_action(world, RC_ACTION_OWNER_MODAL,
                   RC_ACTION_CATEGORY_NORMAL, 0);
    } else if (player->interaction.active) {
        set_action(world, RC_ACTION_OWNER_INTERACTION,
                   RC_ACTION_CATEGORY_NORMAL, 0);
    } else if (player->combat.active || player->attack_target >= 0) {
        set_action(world, RC_ACTION_OWNER_COMBAT,
                   RC_ACTION_CATEGORY_NORMAL, 0);
    } else if (player->route_idx < player->route_len) {
        set_action(world, RC_ACTION_OWNER_MOVEMENT,
                   RC_ACTION_CATEGORY_NORMAL, 0);
    } else if (player->skill_action > 0
            && player->skill_ready_tick > world->tick) {
        set_action(world, RC_ACTION_OWNER_SKILL,
                   RC_ACTION_CATEGORY_NORMAL, player->skill_ready_tick);
    } else {
        memset(action, 0, sizeof(*action));
        action->last_cancel_reason = last_reason;
    }
}

static int execute_command(RcWorld *world, const RcPlayerCommand *command) {
    const int *a = command->args;
    switch ((RcPlayerCommandKind)command->kind) {
    case RC_PLAYER_COMMAND_WALK_TO:
        if (!rc_world_coord_valid(a[0]) || !rc_world_coord_valid(a[1]))
            return 0;
        return rc_player_walk_to(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_RUN_TO:
        if (!rc_world_coord_valid(a[0]) || !rc_world_coord_valid(a[1]))
            return 0;
        return rc_player_run_to(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_STEP:
        return rc_player_step(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_SET_RUNNING:
        return rc_player_set_running(world, a[0]);
    case RC_PLAYER_COMMAND_ATTACK_NPC:
        return rc_player_attack_npc(world, a[0]);
    case RC_PLAYER_COMMAND_SET_ATTACK_STYLE:
        rc_player_set_attack_style(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_TOGGLE_AUTO_RETALIATE:
        rc_combat_toggle_auto_retaliate(world); return 1;
    case RC_PLAYER_COMMAND_TOGGLE_SPECIAL:
        rc_combat_toggle_special(world); return 1;
    case RC_PLAYER_COMMAND_SET_PRAYER:
        rc_player_set_prayer(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_SET_SPELLBOOK:
        rc_player_set_spellbook(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_SELECT_SPELL:
        rc_player_select_spell(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_SET_AUTOCAST:
        rc_player_set_autocast_spell(world, a[0], a[1]); return 1;
    case RC_PLAYER_COMMAND_MOVE_INVENTORY:
        return rc_player_move_inventory_item(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_EQUIP:
        rc_player_equip(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_UNEQUIP:
        rc_player_unequip(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_INTERACT_NPC:
        rc_player_interact_npc(world, a[0], a[1]); return 1;
    case RC_PLAYER_COMMAND_INTERACT_OBJECT:
        return rc_player_interact_object_placement(
            world, a[0], a[1], a[2], a[3], command->key, a[4]);
    case RC_PLAYER_COMMAND_INTERACT_INVENTORY:
        return rc_player_interact_inventory_item(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_INTERACT_EQUIPMENT:
        return rc_player_interact_equipment_item(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_WIDGET_ACTION:
        return rc_player_widget_action(world, a[0], a[1], a[2]);
    case RC_PLAYER_COMMAND_USE_ITEM_ON_NPC:
        return rc_player_use_inventory_item_on_npc(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_USE_ITEM_ON_ITEM:
        return rc_player_use_inventory_item_on_inventory_item(
            world, a[0], a[1]);
    case RC_PLAYER_COMMAND_USE_ITEM_ON_OBJECT:
        return rc_player_use_inventory_item_on_object_placement(
            world, a[0], a[1], a[2], a[3], a[4], command->key);
    case RC_PLAYER_COMMAND_USE_ITEM_ON_GROUND:
        return rc_player_use_inventory_item_on_ground_item(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_USE_ITEM_ON_WIDGET:
        return rc_player_use_inventory_item_on_widget(world, a[0], a[1], a[2]);
    case RC_PLAYER_COMMAND_CAST_ON_NPC:
        return rc_player_cast_spell_on_npc(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_CAST_ON_ITEM:
        return rc_player_cast_spell_on_inventory_item(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_CAST_ON_OBJECT:
        return rc_player_cast_spell_on_object_placement(
            world, a[0], a[1], a[2], a[3], a[4], command->key);
    case RC_PLAYER_COMMAND_CAST_ON_GROUND:
        return rc_player_cast_spell_on_ground_item(world, a[0], a[1]);
    case RC_PLAYER_COMMAND_CAST_ON_WIDGET:
        return rc_player_cast_spell_on_widget(world, a[0], a[1], a[2]);
    case RC_PLAYER_COMMAND_OPEN_STORAGE_OBJECT:
        return rc_player_open_storage_object(world, a[0], a[1]) != 0;
    case RC_PLAYER_COMMAND_OPEN_STORAGE_NPC:
        return rc_player_open_storage_npc(world, a[0], a[1]) != 0;
    case RC_PLAYER_COMMAND_CLOSE_STORAGE:
        return rc_player_close_storage(world);
    case RC_PLAYER_COMMAND_BANK_DEPOSIT:
        return rc_bank_deposit_slot(world, a[0], a[1]) >= 0;
    case RC_PLAYER_COMMAND_BANK_WITHDRAW:
        return rc_bank_withdraw_slot(world, a[0], a[1]) >= 0;
    case RC_PLAYER_COMMAND_APPLY_TRAVERSAL: {
        if (!(world->enabled & RC_SUB_TRAVERSAL)
                || !rc_world_tile_valid(a[0], a[1], a[2])) {
            return 0;
        }
        RcPlayer *player = &world->player;
        player->pending_traversal_active = 1;
        player->pending_traversal_tick = world->tick;
        player->pending_traversal_x = a[0];
        player->pending_traversal_y = a[1];
        player->pending_traversal_plane = a[2];
        rc_player_route_clear(player, RC_MOVEMENT_NONE);
        world->player_action.active = true;
        world->player_action.owner = RC_ACTION_OWNER_TRAVERSAL;
        world->player_action.category = RC_ACTION_CATEGORY_STRONG;
        world->player_action.started_tick = world->tick;
        world->player_action.ready_tick = world->tick + 1;
        return 1;
    }
    case RC_PLAYER_COMMAND_APPLY_RECIPE: {
        const RcRecipe *recipe = rc_recipe_get(a[0]);
        return recipe && rc_player_apply_recipe(world, recipe);
    }
    case RC_PLAYER_COMMAND_DROP_ITEM:
        rc_player_drop_item(world, a[0]); return 1;
    case RC_PLAYER_COMMAND_PICKUP_ITEM:
        rc_player_pickup_item(world, a[0]); return 1;
    default:
        return 0;
    }
}

void rc_player_command_process(RcWorld *world) {
    if (!world) return;
    RcPlayerCommandQueue *queue = &world->player_commands;
    RcPlayerCommand pending[RC_MAX_PLAYER_COMMANDS];
    int count = queue->count;
    if (count > 0) memcpy(pending, world->player_command_storage,
                          (size_t)count * sizeof(*pending));
    queue->count = 0;
    for (int i = 0; i < count; i++) {
        if (world->player_action.active
                && world->player_action.category == RC_ACTION_CATEGORY_STRONG
                && world->player_action.ready_tick > world->tick
                && pending[i].category != RC_ACTION_CATEGORY_SOFT) {
            queue->last_sequence = pending[i].sequence;
            queue->last_result = RC_COMMAND_RESULT_REJECTED_BUSY;
            queue->rejected_count++;
            continue;
        }
        int movement_command = pending[i].kind == RC_PLAYER_COMMAND_WALK_TO
                            || pending[i].kind == RC_PLAYER_COMMAND_RUN_TO
                            || pending[i].kind == RC_PLAYER_COMMAND_STEP;
        if (pending[i].category >= RC_ACTION_CATEGORY_NORMAL
                && !movement_command) {
            rc_player_cancel_action(world, RC_ACTION_CANCEL_REPLACED);
        }
        int ok = execute_command(world, &pending[i]);
        queue->last_sequence = pending[i].sequence;
        queue->last_result = ok ? RC_COMMAND_RESULT_EXECUTED
                                : RC_COMMAND_RESULT_REJECTED_INVALID;
        if (!ok) queue->rejected_count++;
        rc_player_action_refresh(world);
    }
}
