# rc-viewer

`rc-viewer` is the playable client for RuneC. It renders world state
produced by `rc-core` + `rc-content` and handles presentation concerns
such as camera, input, HUD, animation playback, and other viewer-only
systems.

This directory is not the simulation engine. Headless RL sims and
other stripped-down builds should be able to omit `rc-viewer`
entirely.

Use this file for:
- what the viewer owns
- what the viewer depends on
- what code lives in this directory
- what the viewer is and is not responsible for

Planning and task tracking are kept in local Markdown files. This README is
the tracked technical overview of the viewer boundary, runtime assets, and
presentation responsibilities.

## Role

- Interactive local play and visual debugging.
- Rendering and presentation of engine state.
- Viewer-only systems such as camera control, HUD, and audio/music.

## Boundary

- Depends on `rc-core` and `rc-content`.
- May choose which content modules to register at startup.
- Must not own gameplay rules, combat formulas, encounter mechanics,
  quest state machines, or other simulation logic.
- Must treat `data/` as ignored local generated/runtime assets, not as source
  files in the main RuneC repo.

## Key files

- `viewer.c`
  - main window lifecycle
  - input handling
  - camera control
  - visual scene selection/generation followed by backend active-area
    activation through `rc_world_activate_area`
  - world ticking for the current viewer path
- `streaming.c` / `streaming.h`
  - viewer streaming defaults and load telemetry counters
  - monotonic scene/chunk timing independent of the render implementation
  - deterministic center-first mapsquare planning and asset path contracts
- `ui.c` / `ui.h`
  - cache-sprite-backed OSRS viewer UI shell
  - clickable chatbox, minimap/orbs, tabs, inventory/equipment panels,
    prayer/spellbook surfaces, and context menus
  - viewer-owned minimap texture state for the circular moving map
    surface and hookable map dots
  - hook-ready UI state and intents for future gameplay systems
- `ui_assets.c` / `ui_assets.h`
  - small Raylib texture/font loader for viewer UI sprites
  - loads `data/sprites/ui/*.png` generated from the current OpenRS2
    oldschool live cache through
    `tools/cache_pipeline/export_sprites_modern.py` and the local
    RuneLite OSRS font when present
- `terrain.h`
  - terrain-binary loader and terrain mesh helpers
- `objects.h`
  - static object mesh loader
- `models.h`
  - model-set loading for player and NPC meshes
- `anims.h`
  - animation-cache loading and mesh deformation helpers
- `equipment_render.h`
  - viewer-only generated item render map loader
  - maps item IDs to cache-composed ground/equipped render model IDs
    and default body-part hide masks
- `item_render_defs.c` / `item_render_defs.h`
  - viewer-only fallback item-definition render metadata loader
  - extracts ground/equipped model IDs from `items.bin` without exposing
    those fields through `rc-core`
- `npc_render_defs.c` / `npc_render_defs.h`
  - viewer-only NPC animation/model metadata loader
  - extracts NPC stand/walk/attack/death animation IDs and model links from
    `npc_defs.bin` without exposing them through `rc-core`
- `combat_visuals.c` / `combat_visuals.h`
  - viewer-only combat presentation profile loader
  - owns projectile, spotanim, animation, attachment, and visual timing
    metadata; `rc-core` only consumes backend hit-delay profiles
- `object_action_visuals.c` / `object_action_visuals.h`
  - viewer-only object action presentation metadata loader
  - extracts action animation IDs from `object_behaviors.bin` without
    exposing them through `rc-core`
- `dev_validation.c` / `dev_validation.h`
  - viewer-only combat testing helpers
  - owns the temporary validation bank seed, Clan-tab boss transports,
    Varrock-bank combat dummy spawn, and leave-one validation withdraw policy
  - can be disabled with `RUNEC_DEV_VALIDATION=0`

## Runtime expectations

- `rc-viewer` expects to be launched with the project root as the
  working directory because it loads data by relative path.
- It depends on runtime assets such as:
  - `data/regions/{rx}_{ry}.p{plane}.terrain`
  - `data/regions/{rx}_{ry}.p{plane}.objects`
  - `data/regions/{rx}_{ry}.p{plane}.object_anim.models`
  - `data/regions/mapsquare.materials.atlas`
  - `data/regions/mapsquare.materials.tanim`
  - `data/regions/mapsquare.catalog`
  - `data/regions/world.collision-tiles.indexed.bin`
  - `data/defs/npc_defs.bin`
  - `data/spawns/world.npc-spawns.indexed.bin`
  - `data/spawns/world.ground-items.indexed.bin`
  - `data/models/*.models`
  - `data/models/item_render.map`
  - `data/anims/*.anims`
  - `data/sprites/ui/*.png`
- The normal viewer starts from prebuilt mapsquare data. Explicit development
  and compatibility datasets can be selected with environment overrides:
  - `RUNEC_TERRAIN`, `RUNEC_OBJECTS`, `RUNEC_CMAP`
  - `RUNEC_OBJECT_PLACEMENTS` (defaults to the indexed world placement file)
  - `RUNEC_COLLISION_TILES` (defaults to the indexed world collision file)
  - `RUNEC_NPC_DEFS`, `RUNEC_NPC_SPAWNS`, `RUNEC_NPC_MODELS`
  - `RUNEC_NPC_ANIMS`, `RUNEC_PLAYER_MODELS`,
    `RUNEC_PLAYER_ANIMS`, `RUNEC_FALLBACK_ANIMS`
  - `RUNEC_ITEM_MODELS`, `RUNEC_ITEM_RENDER_MAP`
  - `RUNEC_COMBAT_VISUALS`, `RUNEC_COMBAT_PROFILES`
  - `RUNEC_WORLD_ORIGIN_X`, `RUNEC_WORLD_ORIGIN_Y`,
    `RUNEC_WORLD_W`, `RUNEC_WORLD_H`
  - `RUNEC_PLAYER_START_X`, `RUNEC_PLAYER_START_Y`,
    `RUNEC_PLAYER_START_PLANE`
  - `RUNEC_WORLD_ACTIVE_RADIUS_REGIONS`,
    `RUNEC_WORLD_PRELOAD_RADIUS_REGIONS`,
    `RUNEC_WORLD_MAX_CACHED_REGIONS`
  - `RUNEC_VIEWER_SCENE_RADIUS_REGIONS`, `RUNEC_VIEWER_PRELOAD_RADIUS`,
    `RUNEC_VIEWER_MAX_CPU_CHUNKS`, `RUNEC_VIEWER_MAX_GPU_CHUNKS`,
    `RUNEC_VIEWER_UPLOAD_BUDGET_MB_PER_FRAME`
  - `RUNEC_MAPSQUARE_STREAMING`, `RUNEC_MAPSQUARE_DIR`,
    `RUNEC_ACTOR_DRAW_RADIUS`
  - `RUNEC_SCENE_AUTO_EXPORT=1` enables development-only mapsquare generation
    from a local b237 cache; it is off by default
  - `RUNEC_VIEWER_TELEMETRY_OVERLAY=1` for the opt-in streaming overlay
  - `RUNEC_DEV_VALIDATION`, `RUNEC_DEV_BANK_DUMMY`,
    `RUNEC_DEV_TRANSPORT_DEST`, `RUNEC_DEV_BOSS_ATTACKS`
- Those runtime assets are expected to be local in the RuneC working tree
  under `data/`, populated by `scripts/setup-data.sh` or by maintainer data
  rebuild tooling.

- The viewer must not read runtime assets from a sibling reference checkout.
  Reference repos are audit material; generated viewer assets are loaded from
  the local `data/` tree.
- It links both `rc-core` and `rc-content`, but it should still behave
  as a presentation shell rather than a second gameplay engine.

The PR 1 viewer defaults preserve current behavior: scene radius 1, startup
preload radius 0, CPU/GPU chunk caps 128, and a declared 16 MB upload budget.
The upload budget is telemetry/configuration only until the asynchronous upload
path is implemented. Existing `RUNEC_SCENE_RADIUS_REGIONS` and
`RUNEC_STARTUP_SCENE_RADIUS_REGIONS` overrides remain accepted.

PR 5 makes per-mapsquare visuals mandatory during normal gameplay. The current
synchronous cache loads complete
mapsquares center-first, retains overlap, evicts entries outside the new plan,
and obeys the lower CPU/GPU chunk cap. Empty object-animation sidecars are
complete without a model sidecar. Newly decoded chunks are staged and committed
only after the full visible window and backend area succeed, so a failed edge
load leaves the prior scene intact. Static object CPU geometry is released
after GPU upload. The shared material page has separate ownership from chunk
geometry so it is loaded once and can be replaced by paged material ownership
later without changing chunk files.

Split terrain uses neighboring mapsquares when calculating underlay smoothing,
lighting normals, and outer corner heights. New chunks carry a `65x65` corner
grid for their `64x64` tiles; runtime sampling clamps legacy `64x64` edges so a
player or actor cannot receive an out-of-range sentinel height at a boundary.
The shared mapsquare catalog is loaded once into an 8 KiB bitset. Cache-index
voids are removed from sparse destination plans, but every catalog-listed chunk
remains mandatory before a scene or transport commits.

Fixed and generated aggregate scenes remain an explicit development
compatibility path and are not included in runtime-data packs. Normal startup
and transitions never launch Python or read the source cache. Missing installed
assets report the first incomplete path and leave the player in the prior
loaded scene. `RUNEC_SCENE_AUTO_EXPORT=1` retains bounded missing-mapsquare
generation for maintainers with a local b237 cache. Scene or plane transitions
still load the visual and backend destination before committing coordinates.

Streaming telemetry logs startup and scene/active-area changes. It reports
decode/upload time, resident scene chunks and vertices, deduplicated texture
memory, estimated model memory and draw calls, active actors/items, and backend
page timings. CPU and GPU chunk counts are currently equal because current
loaders decode and upload synchronously; later chunk-cache work will separate
them.

## Why it exists

RuneC is meant to support both:

- a playable OSRS game with a real client window
- high-performance headless RL sims that run without rendering

`rc-viewer` keeps those concerns separate so the simulation backend can
stay modular and fast.

## Current implementation shape

Today `rc-viewer` is primarily:
- a render/debug shell for the current world slice
- a movement/camera frontend
- a way to inspect terrain, collision, NPC placement, and animation
- an OSRS-style clickable UI shell with core-synced inventory/equipment
  state and stable intent surfaces for prayer/spell/chat integration
- cache-backed OSRS gameframe assets for the chatbox, minimap/orbs,
  side panel, side tabs, worn-slot icons, skill icons, prayer icons, and
  spell icons
- cache-backed item model overlays for worn equipment and dropped ground
  items, with fallback render metadata loaded in `rc-viewer`
- a generated equipment render map that composes the player from
  default identity-kit body parts plus recolored/retextured equipped
  item models, with body-part hiding driven by viewer-only render
  metadata rather than core gameplay rules
- a terrain/collision-backed circular minimap surface with player/NPC/
  destination dots and click-to-route behavior
- current-cache OpenRS2 b237 OSRS widget art for the native compass,
  minimap cover/masks, side icons, orb frames/fillers/icons, and skill
  icons; RuneLite gameval sprite IDs are the naming authority
- a renderer for core-owned dynamic object state, linked-below scenes,
  bounded mapsquare scenes, compatibility region slices, viewer-owned combat
  projectiles, spot animations, combat attack animations, and equipment visuals
- isolated combat-validation helpers for local manual testing, kept out of
  `rc-core` so they can be disabled or removed without changing simulation
  rules

That means it is useful for presentation validation and the first
inventory/equipment runtime checks, but it is not yet the authority for
end-to-end gameplay validation across combat, shops, quests, banking,
or other UI-driven systems.
