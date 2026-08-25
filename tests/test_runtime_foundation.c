#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "api.h"
#include "assets.h"
#include "events.h"
#include "rng.h"

typedef struct {
    int first_calls;
    int second_calls;
    int tail_calls;
    int late_calls;
    int reentry_result;
} EventState;

typedef struct {
    RcWorldConfig config;
    _Atomic int failures;
} SharedDataThreadState;

static void tail_handler(RcWorld *world, int evt, const void *payload,
                         void *ctx) {
    (void)world;
    (void)evt;
    (void)payload;
    ((EventState *)ctx)->tail_calls++;
}

static void late_handler(RcWorld *world, int evt, const void *payload,
                         void *ctx) {
    (void)world;
    (void)evt;
    (void)payload;
    ((EventState *)ctx)->late_calls++;
}

static void second_handler(RcWorld *world, int evt, const void *payload,
                           void *ctx) {
    (void)world;
    (void)evt;
    (void)payload;
    ((EventState *)ctx)->second_calls++;
}

static void first_handler(RcWorld *world, int evt, const void *payload,
                          void *ctx) {
    EventState *state = ctx;
    state->first_calls++;
    if (state->first_calls == 1) {
        assert(rc_event_unsubscribe(world, evt, second_handler, ctx) == 0);
        assert(rc_event_subscribe(world, evt, late_handler, ctx) == 0);
    }
    state->reentry_result = rc_event_fire(world, evt, payload);
}

static void test_rng_contract(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    assert(cfg.seed == 0);
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->rng_state == RC_DEFAULT_SEED);
    uint32_t first = rc_rng_next(&world->rng_state);
    assert(first != 0 && first != RC_DEFAULT_SEED);
    rc_world_destroy(world);

    uint32_t state = 123456789u;
    for (int i = 0; i < 10000; i++) {
        assert(rc_rng_bounded(&state, 7) < 7);
        int value = rc_rng_range(&state, INT_MAX);
        assert(value >= 0 && value <= INT_MAX);
    }
    assert(rc_rng_bounded(NULL, 7) == 0);
    assert(rc_rng_bounded(&state, 0) == 0);
    assert(rc_rng_range(NULL, 10) == 0);
}

static void test_config_and_memory_contract(void) {
    char message[256];
    RcWorldConfig cfg = rc_preset_base_only();
    assert(rc_world_config_validate(&cfg, message, sizeof(message)));
    assert(cfg.npc_capacity == RC_WORLD_NPC_CAPACITY_BASE);
    assert(sizeof(RcWorld) < 3u * 1024u * 1024u);

    cfg.subsystems |= 1u << 31;
    assert(!rc_world_config_validate(&cfg, message, sizeof(message)));
    assert(strstr(message, "unknown subsystem bits") != NULL);
    assert(rc_world_create_config(&cfg) == NULL);

    cfg = rc_preset_base_only();
    cfg.npc_capacity = 0;
    assert(!rc_world_config_validate(&cfg, message, sizeof(message)));
    assert(strstr(message, "NPC capacity") != NULL);
    assert(rc_world_create_config(&cfg) == NULL);

    cfg = rc_preset_combat_only();
    cfg.prayers_path = NULL;
    assert(!rc_world_config_validate(&cfg, message, sizeof(message)));
    assert(strstr(message, "prayers_path") != NULL);
}

static void *shared_data_thread(void *opaque) {
    SharedDataThreadState *state = opaque;
    for (int i = 0; i < 200; i++) {
        RcGameDataLoadReport report;
        RcGameData *data = rc_game_data_load(&state->config, &report);
        if (!data || !report.ok) {
            atomic_fetch_add_explicit(&state->failures, 1,
                                      memory_order_relaxed);
            continue;
        }
        rc_game_data_release(data);
    }
    return NULL;
}

static void test_shared_data_concurrency(void) {
    enum { THREAD_COUNT = 4 };
    SharedDataThreadState state = {
        .config = rc_preset_base_only(),
    };
    atomic_init(&state.failures, 0);
    pthread_t threads[THREAD_COUNT];

    assert(setenv("RUNEC_VALIDATE_DATA_INSTALL", "0", 1) == 0);
    for (int i = 0; i < THREAD_COUNT; i++)
        assert(pthread_create(&threads[i], NULL, shared_data_thread, &state)
               == 0);
    for (int i = 0; i < THREAD_COUNT; i++)
        assert(pthread_join(threads[i], NULL) == 0);
    assert(unsetenv("RUNEC_VALIDATE_DATA_INSTALL") == 0);
    assert(atomic_load_explicit(&state.failures, memory_order_relaxed) == 0);
}

static void test_reset_and_npc_identity(void) {
    RcWorldConfig cfg = rc_preset_combat_only();
    cfg.seed = 77;
    cfg.npc_capacity = 1;
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    assert(world->npc_capacity == 1);

    RcGameDataLoadReport report;
    RcWorldConfig same = cfg;
    same.seed = 999;
    RcGameData *shared = rc_game_data_load(&same, &report);
    assert(shared == world->game_data);
    rc_game_data_release(shared);
    RcWorldConfig incompatible = cfg;
    incompatible.npc_defs_path = "/tmp/alternate-npc-defs.bin";
    assert(rc_game_data_load(&incompatible, &report) == NULL);
    assert(strstr(report.message, "incompatible RcGameData") != NULL);

    int def_count = 0;
    assert(rc_game_data_npc_defs(world->game_data, &def_count) != NULL);
    assert(def_count > 0);
    int def_idx = 0;
    int slot = rc_npc_spawn(world, def_idx, 3200, 3200, 0);
    assert(slot == 0);
    int old_uid = world->npcs[slot].uid;
    assert(rc_npc_resolve(world, old_uid) == &world->npcs[slot]);
    assert(rc_npc_resolve_const(world, (RcNpcId)old_uid)
           == &world->npcs[slot]);
    assert(rc_npc_spawn(world, def_idx, 3201, 3200, 0) == -1);

    RcNpc *npc_storage = world->npcs;
    const RcGameData *game_data = rc_world_get_game_data(world);
    world->tick = 99;
    world->player.x = 1;
    world->rng_state = 1;
    world->dormant_npcs = malloc(32);
    assert(world->dormant_npcs != NULL);
    world->dormant_npc_capacity = 1;
    assert(rc_world_reset(world));
    assert(world->npcs == npc_storage);
    assert(rc_world_get_game_data(world) == game_data);
    assert(world->npc_count == 0);
    assert(world->tick == 0);
    assert(world->rng_state == 77);
    assert(world->player.x == 3213);
    assert(world->dormant_npcs == NULL);
    assert(rc_npc_resolve(world, old_uid) == NULL);

    slot = rc_npc_spawn(world, def_idx, 3200, 3200, 0);
    assert(slot == 0);
    assert(world->npcs[slot].uid != old_uid);
    assert(rc_npc_resolve(world, old_uid) == NULL);
    assert(rc_npc_resolve(world, world->npcs[slot].uid) == &world->npcs[slot]);
    rc_world_destroy(world);
    assert(!rc_world_reset(NULL));
}

static void test_loaded_capability_contract(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/runec_empty_ndef_%ld.bin",
             (long)getpid());
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    uint32_t header[] = {0x4E444546u, 4u, 0u};
    assert(fwrite(header, sizeof(header[0]), 3, file) == 3);
    assert(fclose(file) == 0);

    RcWorldConfig config = rc_preset_base_only();
    config.subsystems = RC_SUB_COMBAT;
    config.npc_defs_path = path;
    RcGameDataLoadReport report;
    assert(rc_game_data_load(NULL, &report) == NULL);
    assert(strstr(report.message, "missing RcWorldConfig") != NULL);
    assert(rc_game_data_load(&config, &report) == NULL);
    assert(strstr(report.message, "missing subsystem capabilities") != NULL);
    assert(unlink(path) == 0);
}

static const char *defs_pack_name(char *out, size_t capacity) {
    DIR *dir = opendir("data/packs");
    assert(dir != NULL);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        size_t length = strlen(entry->d_name);
        if (length > 4 && strstr(entry->d_name, "defs")
                && strcmp(entry->d_name + length - 4, ".pak") == 0) {
            snprintf(out, capacity, "%s", entry->d_name);
            closedir(dir);
            return out;
        }
    }
    closedir(dir);
    return NULL;
}

static void test_asset_catalog_contract(void) {
    char dir[256];
    snprintf(dir, sizeof(dir), "/tmp/runec_asset_catalog_%ld", (long)getpid());
    assert(mkdir(dir, 0700) == 0);

    char pack_name[256];
    assert(defs_pack_name(pack_name, sizeof(pack_name)) != NULL);
    char source[512];
    char first[512];
    char second[512];
    snprintf(source, sizeof(source), "%s/data/packs/%s",
             RC_TEST_SOURCE_DIR, pack_name);
    snprintf(first, sizeof(first), "%s/a.pak", dir);
    snprintf(second, sizeof(second), "%s/b.pak", dir);
    assert(symlink(source, first) == 0);
    assert(symlink(source, second) == 0);

    assert(rc_asset_reset());
    assert(!rc_asset_set_pack_dir(NULL));
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_PACK));
    assert(rc_asset_set_pack_dir(dir));
    assert(!rc_asset_exists("defs/npc_defs.bin"));
    assert(!rc_asset_set_data_root("data"));
    assert(!rc_asset_set_backend(RC_ASSET_BACKEND_LOOSE));

    assert(rc_asset_reset());
    assert(unlink(second) == 0);
    FILE *bad = fopen(second, "wb");
    assert(bad != NULL);
    assert(fputs("not a pack", bad) >= 0);
    assert(fclose(bad) == 0);
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_PACK));
    assert(rc_asset_set_pack_dir(dir));
    assert(!rc_asset_exists("defs/npc_defs.bin"));

    assert(rc_asset_reset());
    assert(rc_asset_set_backend(RC_ASSET_BACKEND_LOOSE));
    assert(rc_asset_set_data_root("data"));
    assert(unlink(first) == 0);
    assert(unlink(second) == 0);
    assert(rmdir(dir) == 0);
}

static void test_event_contract(void) {
    RcWorldConfig cfg = rc_preset_base_only();
    RcWorld *world = rc_world_create_config(&cfg);
    assert(world != NULL);
    EventState state = {0};

    assert(rc_event_subscribe(NULL, RC_EVT_NPC_DIED,
                              first_handler, &state) == -1);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED, NULL, &state) == -1);
    assert(rc_event_fire(NULL, RC_EVT_NPC_DIED, NULL) == -1);
    assert(rc_event_fire(world, RC_EVT_MAX, NULL) == -1);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED,
                              first_handler, &state) == 0);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED,
                              first_handler, &state) == -1);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED,
                              second_handler, &state) == 0);
    assert(rc_event_subscribe(world, RC_EVT_NPC_DIED,
                              tail_handler, &state) == 0);

    assert(rc_event_fire(world, RC_EVT_NPC_DIED, NULL) == 0);
    assert(state.first_calls == 1);
    assert(state.second_calls == 1);
    assert(state.tail_calls == 1);
    assert(state.late_calls == 0);
    assert(state.reentry_result == -1);

    assert(rc_event_fire(world, RC_EVT_NPC_DIED, NULL) == 0);
    assert(state.first_calls == 2);
    assert(state.second_calls == 1);
    assert(state.tail_calls == 2);
    assert(state.late_calls == 1);
    assert(rc_event_unsubscribe(world, RC_EVT_NPC_DIED,
                                second_handler, &state) == -1);
    rc_events_init(NULL);
    rc_world_destroy(world);
}

int main(void) {
    test_rng_contract();
    test_config_and_memory_contract();
    test_reset_and_npc_identity();
    test_loaded_capability_contract();
    test_shared_data_concurrency();
    test_event_contract();
    test_asset_catalog_contract();
    printf("runtime memory: world=%zu npc=%zu base=%zu sim=%zu full=%zu bytes\n",
           sizeof(RcWorld), sizeof(RcNpc),
           sizeof(RcWorld) + RC_WORLD_NPC_CAPACITY_BASE * sizeof(RcNpc),
           sizeof(RcWorld) + RC_WORLD_NPC_CAPACITY_SIM * sizeof(RcNpc),
           sizeof(RcWorld) + RC_WORLD_NPC_CAPACITY_FULL * sizeof(RcNpc));
    printf("test_runtime_foundation: all runtime contracts passed.\n");
    return 0;
}
