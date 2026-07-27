#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "api.h"
#include "config.h"
#include "items.h"
#include "npc.h"

#define NPC_PATH RC_TEST_SOURCE_DIR "/data/defs/npc_defs.bin"
#define SPAWN_PATH \
    RC_TEST_SOURCE_DIR "/data/spawns/world.npc-spawns.indexed.bin"
#define GROUND_PATH "/tmp/runec_dormant_ground_items.bin"

enum {
    GSPI_MAGIC = 0x49505347,
    GSPI_VERSION = 1,
    GSPI_RECORD_SIZE = 22,
    GSPI_MAPSQUARES = 65536,
};

static void write_u32(FILE *f, uint32_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_u8(FILE *f, uint8_t value) {
    assert(fwrite(&value, sizeof(value), 1, f) == 1);
}

static void write_ground_row(FILE *f, uint32_t source_order,
                             uint32_t item_id, uint32_t quantity,
                             int x, int y, uint8_t plane) {
    write_u32(f, source_order);
    write_u32(f, item_id);
    write_u32(f, quantity);
    write_u32(f, (uint32_t)x);
    write_u32(f, (uint32_t)y);
    write_u8(f, plane);
    write_u8(f, 0);
}

static void write_ground_fixture(void) {
    FILE *f = fopen(GROUND_PATH, "wb");
    assert(f != NULL);
    write_u32(f, GSPI_MAGIC);
    write_u32(f, GSPI_VERSION);
    write_u32(f, 2);
    write_u32(f, GSPI_RECORD_SIZE);
    write_u32(f, 2);
    write_u32(f, 2);
    write_u32(f, 0);
    write_u32(f, 0);
    write_u32(f, 0);

    uint32_t first_row = 0;
    for (uint32_t mapsquare = 0; mapsquare < GSPI_MAPSQUARES;
         mapsquare++) {
        uint32_t count = 0;
        if (mapsquare == ((50u << 8) | 53u)) count = 1;
        if (mapsquare == ((62u << 8) | 62u)) count = 1;
        write_u32(f, first_row);
        write_u32(f, count);
        first_row += count;
    }
    assert(first_row == 2);
    write_ground_row(f, 0, 995, 100, 3213, 3428, 0);
    write_ground_row(f, 1, 995, 10, 4000, 4000, 0);
    assert(fclose(f) == 0);
}

static RcNpc *first_persistent_npc(RcWorld *world) {
    for (int i = 0; i < world->npc_count; i++) {
        RcNpc *npc = &world->npcs[i];
        const RcNpcDef *def = rc_npc_def_for_npc(npc);
        if (npc->active && npc->spawn_key != 0 && def
                && def->hitpoints > 0) {
            return npc;
        }
    }
    return NULL;
}

static RcNpc *npc_with_spawn_key(RcWorld *world, uint64_t spawn_key) {
    for (int i = 0; i < world->npc_count; i++) {
        if (world->npcs[i].active
                && world->npcs[i].spawn_key == spawn_key) {
            return &world->npcs[i];
        }
    }
    return NULL;
}

static RcGroundItem *static_ground_item(RcWorld *world, int item_id) {
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem *item = &world->ground_items[i];
        if (item->static_spawn && item->item_id == item_id) return item;
    }
    return NULL;
}

static RcGroundItem *dynamic_ground_item(RcWorld *world, int uid) {
    for (int i = 0; i < world->ground_item_count; i++) {
        RcGroundItem *item = &world->ground_items[i];
        if (item->active && !item->static_spawn && item->uid == uid)
            return item;
    }
    return NULL;
}

int main(void) {
    write_ground_fixture();

    RcWorldConfig config = rc_preset_base_only();
    config.subsystems = RC_SUB_COMBAT | RC_SUB_LOOT;
    config.npc_defs_path = NPC_PATH;
    config.spawns_path = SPAWN_PATH;
    RcWorld *world = rc_world_create_config(&config);
    assert(world != NULL);

    RcActiveAreaRequest varrock = {
        .origin_x = 3072,
        .origin_y = 3264,
        .width = 320,
        .height = 320,
        .min_plane = 0,
        .max_plane = RC_MAX_PLANES - 1,
        .flags = RC_ACTIVE_AREA_LOAD_NPCS
               | RC_ACTIVE_AREA_CLEAR_NPCS
               | RC_ACTIVE_AREA_LOAD_STATIC_GROUND_ITEMS
               | RC_ACTIVE_AREA_CLEAR_STATIC_GROUND_ITEMS,
        .npc_spawns_path = SPAWN_PATH,
        .ground_item_spawns_path = GROUND_PATH,
    };
    RcActiveAreaStats stats;
    assert(rc_world_activate_area(world, &varrock, &stats) == 1);
    assert(stats.streaming.dormant_npc_states == 0);
    assert(stats.streaming.dormant_ground_items == 0);

    RcNpc *npc = first_persistent_npc(world);
    assert(npc != NULL);
    uint64_t npc_key = npc->spawn_key;
    npc->current_hp = 0;
    npc->is_dead = true;
    npc->death_timer = 3;
    npc->respawn_timer = 17;
    npc->x++;

    RcGroundItem *static_item = static_ground_item(world, 995);
    assert(static_item != NULL && static_item->active);
    uint64_t static_key = static_item->spawn_key;
    assert(static_key != 0 && static_item->spawn_quantity == 100);
    static_item->active = false;
    static_item->quantity = 0;
    static_item->version++;

    assert(rc_ground_item_spawn(world, 1048, 1, 3214, 3428, 0,
                                RC_GROUND_OWNER_NONE));
    RcGroundItem *drop = NULL;
    for (int i = 0; i < world->ground_item_count; i++) {
        if (world->ground_items[i].active
                && !world->ground_items[i].static_spawn
                && world->ground_items[i].item_id == 1048) {
            drop = &world->ground_items[i];
            break;
        }
    }
    assert(drop != NULL);
    int drop_uid = drop->uid;
    int drop_timer = drop->despawn_timer;

    RcActiveAreaRequest distant = varrock;
    distant.origin_x = 3968;
    distant.origin_y = 3968;
    distant.width = 64;
    distant.height = 64;
    distant.flags = RC_ACTIVE_AREA_CLEAR_NPCS
                  | RC_ACTIVE_AREA_LOAD_STATIC_GROUND_ITEMS
                  | RC_ACTIVE_AREA_CLEAR_STATIC_GROUND_ITEMS;
    assert(rc_world_activate_area(world, &distant, &stats) == 1);
    assert(world->npc_count == 0);
    assert(stats.streaming.saved_npc_states == 1);
    assert(stats.streaming.dormant_npc_states == 1);
    assert(stats.streaming.saved_ground_items == 2);
    assert(stats.streaming.dormant_ground_items == 2);
    assert(dynamic_ground_item(world, drop_uid) == NULL);

    for (int i = 0; i < 20; i++) rc_world_tick(world);

    assert(rc_world_activate_area(world, &varrock, &stats) == 1);
    assert(stats.streaming.restored_npc_states == 1);
    assert(stats.streaming.dormant_npc_states == 0);
    npc = npc_with_spawn_key(world, npc_key);
    assert(npc != NULL);
    assert(npc->is_dead && npc->current_hp == 0);
    assert(npc->death_timer == 3 && npc->respawn_timer == 17);
    assert(npc->x == npc->spawn_x + 1);

    static_item = static_ground_item(world, 995);
    assert(static_item != NULL);
    assert(static_item->spawn_key == static_key);
    assert(!static_item->active && static_item->quantity == 0);
    drop = dynamic_ground_item(world, drop_uid);
    assert(drop != NULL);
    assert(drop->despawn_timer == drop_timer);
    assert(stats.streaming.restored_ground_items == 2);
    assert(stats.streaming.dormant_ground_items == 0);

    npc->x = npc->spawn_x;
    npc->y = npc->spawn_y;
    npc->plane = npc->spawn_plane;
    npc->current_hp = npc->spawn_hp;
    npc->attack_timer = 0;
    npc->death_timer = 0;
    npc->respawn_timer = 0;
    npc->attack_count = 0;
    npc->poison_damage = 0;
    npc->poison_tick_counter = 0;
    npc->is_dead = false;
    npc->disable_wander = false;
    npc->force_player_max_hit = false;
    npc->player_untargetable = false;

    assert(rc_world_activate_area(world, &distant, &stats) == 1);
    assert(stats.streaming.saved_npc_states == 0);
    assert(stats.streaming.dormant_npc_states == 0);

    rc_world_destroy(world);
    assert(remove(GROUND_PATH) == 0);
    printf("test_dormant_world_state: dormant state survives area changes.\n");
    return 0;
}
