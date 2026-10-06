#define _POSIX_C_SOURCE 200809L
#include "api.h"
#include "config.h"
#include "content.h"
#include "items.h"
#include "npc.h"
#include "storage.h"
#include "../world_test_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>

int main(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    // Shops keeps the pre-fix configuration comparable; it formerly owned banker loading.
    cfg.subsystems = RC_SUB_STORAGE | RC_SUB_INVENTORY | RC_SUB_SHOPS;
    cfg.items_path = "data/defs/items.bin";
    cfg.shops_path = "data/defs/shops.bin";
    RcWorld *w = rc_world_create_config(&cfg);
    assert(w);
    rc_content_register_all(w);
    rc_test_open_mapsquare(w,w->player.x,w->player.y,w->player.plane);
    int count, index = -1, option = -1;
    const RcNpcDef *defs = rc_npc_defs_all(&count);
    for (int i = 0; i < count && index < 0; i++)
        for (int op = 0; op < RC_NPC_OPTION_COUNT; op++)
            if (!strcmp(defs[i].options[op],"Bank")) { index = i; option = op; break; }
    assert(index >= 0);
    int npc = rc_npc_spawn(w,index,w->player.x+1,w->player.y,w->player.plane);
    assert(npc >= 0);
    w->npcs[npc].disable_wander = true;
    rc_player_interact_npc(w,w->npcs[npc].uid,option);
    rc_world_tick(w);
    assert(w->player.storage_kind == RC_STORAGE_BANK);
    int slot = rc_bank_add_item(w,995,100);
    assert(slot >= 0);
    const int pairs = 200000;
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC,&start);
    for (int i = 0; i < pairs; i++) {
        assert(rc_bank_withdraw_slot(w,slot,1));
        rc_world_tick(w);
        assert(w->player.inventory[0].item_id == 995);
        assert(rc_bank_deposit_slot(w,0,1));
        rc_world_tick(w);
        assert(w->player.bank[slot].quantity == 100 && w->player.inventory[0].item_id == -1);
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    double seconds = end.tv_sec-start.tv_sec + (end.tv_nsec-start.tv_nsec)/1e9;
    printf("banking: %d queued transfers/ticks, %.0f SPS, %.3f us/transfer\n",
           pairs*2, pairs*2/seconds, seconds*1e6/(pairs*2));
    rc_world_destroy(w);
    return 0;
}
