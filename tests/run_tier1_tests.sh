#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${RC_BUILD_DIR:-$ROOT/build}"
JOBS="${RC_BUILD_JOBS:-2}"
SUITE="${1:-all}"

case "$SUITE" in
    all) LABEL="tier1" ;;
    runtime-foundation) LABEL="tier1_runtime_foundation" ;;
    tick-action-scheduling) LABEL="tier1_tick_action_scheduling" ;;
    coordinates-world-primitives) LABEL="tier1_coordinates_world_primitives" ;;
    active-area-persistence) LABEL="tier1_active_area_persistence" ;;
    movement-pathfinding-los-routing)
        LABEL="tier1_movement_pathfinding_los_routing"
        ;;
    npc-runtime) LABEL="tier1_npc_runtime" ;;
    objects-dynamic-locs) LABEL="tier1_objects_dynamic_locs" ;;
    items-inventory-equipment) LABEL="tier1_items_inventory_equipment" ;;
    interaction-engine) LABEL="tier1_interaction_engine" ;;
    *)
        printf 'Tier 1 tests failed: unknown suite "%s".\n' "$SUITE" >&2
        printf '%s\n' \
            'Valid suites: all, runtime-foundation, tick-action-scheduling,' \
            'coordinates-world-primitives, active-area-persistence,' \
            'movement-pathfinding-los-routing, npc-runtime,' \
            'objects-dynamic-locs, items-inventory-equipment,' \
            'interaction-engine.' >&2
        exit 2
        ;;
esac

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    printf 'Tier 1 tests failed: no configured build tree at %s.\n' \
        "$BUILD_DIR" >&2
    printf 'Run: cmake -S %s -B %s\n' "$ROOT" "$BUILD_DIR" >&2
    exit 2
fi

cmake --build "$BUILD_DIR" -j "$JOBS"
ctest --test-dir "$BUILD_DIR" --label-regex "^${LABEL}$" \
    --output-on-failure
