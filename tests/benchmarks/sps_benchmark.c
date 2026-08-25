#define _POSIX_C_SOURCE 200809L

#include "../../rc-core/api.h"
#include "../../rc-core/combat.h"
#include "../../rc-core/npc.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum RcBenchMode {
    RC_BENCH_IDLE = 0,
    RC_BENCH_COMBAT = 1,
} RcBenchMode;

static double rc_now_seconds(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

static int rc_parse_positive(const char *arg, const char *value, int fallback) {
    char *end = NULL;
    long parsed = strtol(value, &end, 10);
    if (end == value || *end != '\0' || parsed <= 0 || parsed > 1000000000L) {
        fprintf(stderr, "invalid %s: %s\n", arg, value);
        exit(2);
    }
    return (int)parsed;
}

static const char *rc_mode_name(RcBenchMode mode) {
    return mode == RC_BENCH_COMBAT ? "combat" : "idle";
}

static int rc_find_bench_npc_def(void) {
    int count = 0;
    const RcNpcDef *defs = rc_npc_defs_all(&count);
    for (int i = 0; defs && i < count; i++) {
        if (defs[i].hitpoints > 0 && defs[i].max_hit == 0
                && strcmp(defs[i].options[1], "Attack") == 0) {
            return i;
        }
    }
    return -1;
}

static RcWorld *rc_make_world(int seed, RcBenchMode mode, int *npc_def_idx) {
    RcWorldConfig cfg = rc_preset_base_only();
    cfg.subsystems = mode == RC_BENCH_COMBAT ? RC_SUB_COMBAT : 0;
    cfg.seed = (uint64_t)seed;

    RcWorld *world = rc_world_create_config(&cfg);
    if (!world) {
        fprintf(stderr, "failed to create benchmark world\n");
        exit(1);
    }

    world->player.x = 3200;
    world->player.y = 3200;
    world->player.plane = 0;
    world->player.current_hp = 1000000000;
    world->player.max_hp = 1000000000;
    world->player.auto_retaliate = true;
    for (int i = 0; i < SKILL_COUNT; i++) {
        world->player.skills.base_level[i] = 99;
        world->player.skills.boosted_level[i] = 99;
    }
    rc_player_set_attack_style(world, 0);

    if (mode == RC_BENCH_COMBAT) {
        if (*npc_def_idx < 0) *npc_def_idx = rc_find_bench_npc_def();
        int npc_idx = rc_npc_spawn(world, *npc_def_idx, 3201, 3200, 0);
        if (npc_idx < 0) {
            fprintf(stderr, "failed to spawn benchmark npc\n");
            exit(1);
        }
        world->npcs[npc_idx].current_hp = 1000000000;
        world->npcs[npc_idx].spawn_hp = 1000000000;
        if (!rc_combat_start_player_vs_npc(world, 0, world->npcs[npc_idx].uid)) {
            fprintf(stderr, "failed to start benchmark combat\n");
            exit(1);
        }
    }

    return world;
}

static void rc_print_usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s [--mode idle|combat] [--envs N] [--steps N] [--warmup N]\n",
            argv0);
}

int main(int argc, char **argv) {
    RcBenchMode mode = RC_BENCH_COMBAT;
    int envs = 64;
    int steps = 20000;
    int warmup = 1000;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "combat") == 0) {
                mode = RC_BENCH_COMBAT;
            } else if (strcmp(argv[i], "idle") == 0) {
                mode = RC_BENCH_IDLE;
            } else {
                rc_print_usage(argv[0]);
                return 2;
            }
        } else if (strcmp(argv[i], "--envs") == 0 && i + 1 < argc) {
            envs = rc_parse_positive("--envs", argv[++i], envs);
        } else if (strcmp(argv[i], "--steps") == 0 && i + 1 < argc) {
            steps = rc_parse_positive("--steps", argv[++i], steps);
        } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
            warmup = rc_parse_positive("--warmup", argv[++i], warmup);
        } else if (strcmp(argv[i], "--help") == 0) {
            rc_print_usage(argv[0]);
            return 0;
        } else {
            rc_print_usage(argv[0]);
            return 2;
        }
    }

    int npc_def_idx = -1;
    RcWorld **worlds = calloc((size_t)envs, sizeof(*worlds));
    if (!worlds) {
        fprintf(stderr, "failed to allocate %d benchmark worlds\n", envs);
        return 1;
    }

    for (int i = 0; i < envs; i++) {
        worlds[i] = rc_make_world(9001 + i, mode, &npc_def_idx);
    }

    for (int step = 0; step < warmup; step++) {
        for (int env = 0; env < envs; env++) {
            rc_world_tick(worlds[env]);
        }
    }

    double start = rc_now_seconds();
    for (int step = 0; step < steps; step++) {
        for (int env = 0; env < envs; env++) {
            rc_world_tick(worlds[env]);
        }
    }
    double elapsed = rc_now_seconds() - start;

    uint64_t total_steps = (uint64_t)(uint32_t)envs * (uint64_t)(uint32_t)steps;
    double sps = (double)total_steps / elapsed;
    double ns_per_step = (elapsed * 1000000000.0) / (double)total_steps;

    printf("RuneC SPS benchmark\n");
    printf("mode: %s\n", rc_mode_name(mode));
    printf("envs: %d\n", envs);
    printf("warmup_steps_per_env: %d\n", warmup);
    printf("timed_steps_per_env: %d\n", steps);
    printf("total_env_steps: %llu\n", (unsigned long long)total_steps);
    printf("elapsed_seconds: %.6f\n", elapsed);
    printf("sps: %.0f\n", sps);
    printf("ns_per_env_step: %.2f\n", ns_per_step);

    for (int i = 0; i < envs; i++) {
        rc_world_destroy(worlds[i]);
    }
    free(worlds);
    return 0;
}
