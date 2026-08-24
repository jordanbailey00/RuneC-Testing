# Tier 1 suite membership. Missing tests are configuration errors so a rename
# or removal cannot silently weaken foundational coverage.
function(rc_add_tier1_suite suite)
    foreach(test_name ${ARGN})
        if(NOT TEST "${test_name}")
            message(FATAL_ERROR
                "Tier 1 suite ${suite} requires missing test ${test_name}")
        endif()
        set_property(TEST "${test_name}" APPEND PROPERTY LABELS
            tier1 "tier1_${suite}")
    endforeach()
endfunction()

# World creation, configuration, isolation, deterministic state, and data
# ownership used by every other system.
rc_add_tier1_suite(runtime_foundation
    validate_direct_global_access
    test_base_only
    test_determinism
    test_game_data_shared
    test_game_data_validation
    test_modular_loading
    test_normalization_runtime
    test_streaming_config
)

# Tick-owned action progression, replacement, delayed effects, cleanup, and
# deterministic ordering across integrated gameplay paths.
rc_add_tier1_suite(tick_action_scheduling
    test_determinism
    test_headless_action_runtime
    test_interaction_engine_phase1
    test_interaction_engine_phase2
    test_interaction_engine_phase4
    test_interaction_engine_phase6
    test_combat_phase2_attack_handoff
    test_combat_phase6_hit_pipeline
    test_combat_phase7_retaliation_ai
    test_combat_runtime_flow
    test_ground_items_phase4
    test_objects_runtime
    test_traversal_runtime
)

# Mapsquare, world-tile, local-tile, plane, collision, and placement coordinate
# contracts from generated data through runtime queries.
rc_add_tier1_suite(coordinates_world_primitives
    test_region_sets
    test_mapsquare_exports
    test_spawn_index
    test_object_placement_index
    test_collision_tile_index
    test_activity_spawns_runtime
    test_area_flags_runtime
    test_active_area_runtime
    test_collision_tiles_bin
    test_collision_tiles_runtime
    test_object_placements_bin
    test_pathfinding
    test_plane_contracts_runtime
    test_spawn_slices_runtime
    test_traversal_runtime
)

# Area activation, indexed loading, dormant state, streaming configuration,
# and state preservation across area changes.
rc_add_tier1_suite(active_area_persistence
    test_spawn_index
    test_object_placement_index
    test_collision_tile_index
    test_active_area_runtime
    test_collision_tiles_runtime
    test_dormant_world_state
    test_game_data_shared
    test_ground_items_phase1
    test_ground_items_phase4
    test_objects_runtime
    test_spawn_slices_runtime
    test_streaming_config
)

# Collision, routing, line of sight, plane isolation, movement-facing state,
# and route-driven interactions.
rc_add_tier1_suite(movement_pathfinding_los_routing
    test_collision_tiles_bin
    test_collision_tiles_runtime
    test_combat_phase3_movement_range_facing
    test_interaction_engine_phase4
    test_npc_facing_runtime
    test_objects_runtime
    test_pathfinding
    test_plane_contracts_runtime
    test_traversal_runtime
)

# NPC definitions, indexed spawning, identity across active areas, movement,
# interaction, combat lifecycle, and data-driven mechanics.
rc_add_tier1_suite(npc_runtime
    test_npc_defs_bin
    test_spawn_index
    test_spawn_slices_runtime
    test_spawn_sources_bin
    test_active_area_runtime
    test_dormant_world_state
    test_npc_facing_runtime
    test_npc_option_interactions
    test_regular_npc_mechanics_bin
    test_regular_npc_mechanics_combat
    test_combat_phase6_hit_pipeline
    test_combat_phase7_retaliation_ai
)

# Object definitions and placements, dynamic state, collision updates,
# resource changes, varbit-backed behavior, and interaction integration.
rc_add_tier1_suite(objects_dynamic_locs
    test_object_placement_index
    test_active_area_runtime
    test_collision_tiles_runtime
    test_game_data_shared
    test_interaction_engine_phase7
    test_modular_loading
    test_object_defs_bin
    test_object_placements_bin
    test_objects_runtime
    test_skills_runtime
    test_traversal_runtime
    test_varbits_runtime
)

# Item definitions, inventory mutation, equipment rules, ground-item handoff,
# combat resources, spells, and storage integration.
rc_add_tier1_suite(items_inventory_equipment
    test_items_bin
    test_inventory_equipment_runtime
    test_game_data_shared
    test_modular_loading
    test_headless_action_runtime
    test_interaction_engine_phase8
    test_combat_phase4_weapon_styles_cycle
    test_combat_phase5_formula_core
    test_combat_phase10_resources_specials
    test_combat_phase11_validation_gate
    test_ground_items_phase1
    test_ground_items_phase2
    test_ground_items_phase3
    test_ground_items_phase4
    test_ground_items_phase5
    test_prayer_spell_actions_runtime
    test_shops_storage_runtime
)

# Interaction admission, handler precedence, routing, stale-target handling,
# system handoff, and NPC/object/item integration.
rc_add_tier1_suite(interaction_engine
    test_headless_action_runtime
    test_interaction_engine_phase1
    test_interaction_engine_phase2
    test_interaction_engine_phase3
    test_interaction_engine_phase4
    test_interaction_engine_phase5
    test_interaction_engine_phase6
    test_interaction_engine_phase7
    test_interaction_engine_phase8
    test_npc_option_interactions
    test_objects_runtime
    test_ground_items_phase2
    test_ground_items_phase5
    test_plane_contracts_runtime
    test_combat_phase2_attack_handoff
)
