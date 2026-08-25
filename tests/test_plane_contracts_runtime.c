#include <assert.h>
#include <string.h>

#include "api.h"
#include "config.h"
#include "items.h"
#include "npc.h"
#include "objects.h"
#include "player_actions.h"

#define ITEM_PATH RC_TEST_SOURCE_DIR "/data/defs/items.bin"
#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define ODEF_PATH RC_TEST_SOURCE_DIR "/data/defs/object_defs.bin"
#define OBHV_PATH RC_TEST_SOURCE_DIR "/data/defs/object_behaviors.bin"

static RcWorld *make_world(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = RC_SUB_COMBAT | RC_SUB_INVENTORY | RC_SUB_LOOT
                   | RC_SUB_OBJECTS;
    cfg.items_path = ITEM_PATH;
    cfg.npc_defs_path = NPC_PATH;
    cfg.object_defs_path = ODEF_PATH;
    cfg.object_behaviors_path = OBHV_PATH;
    cfg.player_actions_path = NULL;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    world->player.prev_x = world->player.x;
    world->player.prev_y = world->player.y;
    return world;
}

static void test_npc_interaction_rejects_other_plane(void) {
    RcWorld *world = make_world();
    int def_count = 0;
    const RcNpcDef *defs = rc_npc_defs_all(&def_count);
    int def_idx = -1;
    for (int i = 0; defs && i < def_count; i++) {
        if (rc_npc_def_option(&defs[i], 0)[0]) {
            def_idx = i;
            break;
        }
    }
    assert(def_idx >= 0);
    int idx = rc_npc_spawn(world, def_idx, world->player.x,
                           world->player.y, 1);
    assert(idx >= 0);
    int uid = world->npcs[idx].uid;

    rc_player_attack_npc(world, uid);
    assert(!world->player.interaction.active);
    rc_world_tick(world);
    assert(!world->player.interaction.active);
    rc_player_interact_npc(world, uid, 0);
    rc_world_tick(world);
    assert(!world->player.interaction.active);
    assert(!world->player.combat.active);

    world->npcs[idx].plane = world->player.plane;
    rc_player_interact_npc(world, uid, 0);
    rc_world_tick(world);
    assert(world->player.interaction.target.plane == world->player.plane);
    rc_world_destroy(world);
}

static void test_object_and_ground_item_reject_other_plane(void) {
    RcWorld *world = make_world();
    const int obj_id = 409;
    assert(rc_object_def_get(obj_id) != NULL);
    assert(rc_object_behavior_get(obj_id) != NULL);
    assert(g_rc_player_action_count == 0 ||
           rc_player_action_allowed(world->enabled,
                                    RC_PLAYER_ACTION_INTERACT_OBJECT));

    assert(rc_player_interact_object_at(world, obj_id, world->player.x,
                                        world->player.y, 1, 0) == 1);
    rc_world_tick(world);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_INVALID);
    assert(!world->player.interaction.active);
    world->player.inventory[0].item_id = 995;
    world->player.inventory[0].quantity = 1;
    world->ground_item_count = 1;
    world->ground_items[0] = (RcGroundItem){
        .uid = 77,
        .version = 1,
        .item_id = 995,
        .quantity = 1,
        .x = world->player.x,
        .y = world->player.y,
        .plane = 1,
        .visibility = RC_GROUND_VIS_PUBLIC,
        .owner_uid = RC_GROUND_OWNER_NONE,
        .active = true,
    };
    assert(rc_player_use_inventory_item_on_ground_item(world, 0, 0) == 1);
    rc_world_tick(world);
    assert(rc_player_last_command_result(world, NULL)
           == RC_COMMAND_RESULT_REJECTED_INVALID);
    rc_player_pickup_item(world, 0);
    rc_world_tick(world);
    assert(!world->player.interaction.active);
    rc_world_destroy(world);
}

int main(void) {
    test_npc_interaction_rejects_other_plane();
    test_object_and_ground_item_reject_other_plane();
    return 0;
}
