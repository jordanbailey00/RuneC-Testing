# Rendering Gaps Audit

This document tracks broad missing-rendering classes found after
`missing_content.md` was completed. The prior NPC/object visual-path audit is
still clean; this file covers terrain composition, generated scene coverage,
and static world ground-item spawns.

Reports:

- `tools/audit_rendering_gaps.py`
- `tools/reports/rendering_gaps.txt`
- `tools/reports/rendering_gaps_rows.tsv`
- `tools/reports/rendering_gaps_scene_cache.txt`
- `tools/reports/rendering_gaps_scene_cache_rows.tsv`

## Current Status

Implemented:

- Terrain exporter no longer drops visible edge/context tiles just because a
  tile has no direct underlay/overlay; it fills from nearby floor context when
  the cache implies adjacent visible floor.
- Void overlays now fall back to underlay/nearby floor context when a visible
  fallback exists.
- Full overlay tiles with `hide_underlay=false` now blend overlay/underlay
  color instead of painting only the overlay.
- Shaped overlays now emit sub-tile overlay geometry instead of painting every
  shaped overlay as a full square.
- Validation scene exports now generate planes `0,1,2,3` for every validation
  scene prefix.
- Validation scenes now include the four Stronghold of Security floor windows
  used for goblin/minotaur/flesh-crawler/ankou manual validation.
- Viewer scene-plane transitions now ensure the active scene plane is loaded
  before drawing after same-window plane changes.
- Static ground-item export/load plumbing exists:
  `content/world/static_ground_items.tsv` ->
  `data/spawns/world.ground-items.bin` -> active-area loader -> viewer draw path.

Latest audit summary:

- Generated scene prefixes scanned: `14`
- NPC rows inside generated scene prefixes: `2,386`
- NPC rows on planes missing generated scene assets: `0`
- Ankou rows inside generated scene prefixes: `32`
- NPC rows blocked/missing by source/model audit: `0`
- Object placement rows with no current visual path: `0`
- Static ground-item spawn export files present: `1`
- Static ground-item spawn rows exported: `0`

## Remaining Gaps

### Static world ground items

Status: source-blocked.

The runtime/export/load path now exists, but the approved source file has zero
rows. Loose map items such as the coins and gold bars under Varrock west bank
will remain absent until an approved static world ground-item corpus is added
to `content/world/static_ground_items.tsv`.

### Terrain fidelity

Status: improved, not final parity.

The current terrain renderer is still a vertex-colored approximation. It now
handles the gap classes that produced obvious grey/missing edge tiles, but it
does not yet implement full client-faithful floor texture UVs/materials.

The audit still reports:

- `empty_terrain_no_floor_context`: cache space with no nearby floor fallback.
- `void_overlay_no_floor_context`: void overlay space with no nearby floor
  fallback.
- One full-cache `underlay_definition_missing` anomaly at region `98,199`,
  outside the current validation areas.

Those first two buckets are tracked to distinguish intentional empty/void cache
space from exporter holes; they are not automatically gameplay blockers.

## Root Causes Fixed

Grand Exchange floor / grey edge tiles:

- Cause: simplified terrain export skipped no-underlay/no-overlay context
  tiles, skipped void overlays, ignored shaped overlay coverage, and ignored
  overlay/underlay blending.
- Fix: contextual fallback, void fallback, shaped overlay geometry, and blended
  full overlay color were added to `tools/cache_pipeline/export_terrain.py`.

Missing NPCs after transports, including Stronghold examples:

- Cause: this was a coverage/invariant issue, not missing Ankou source data.
  NPC defs, spawn rows, and render models existed, but generated scene windows
  were not consistently plane-complete.
- Fix: validation scenes now export all planes, Stronghold validation windows
  are included, and scene plane changes trigger active plane loading.

Static loose world items:

- Cause: dropped loot existed, but permanent static ground-item spawns had no
  committed export, binary, or active-area loader.
- Fix: export/load plumbing was added. The actual source rows remain blocked.

## Verification Commands

```sh
python3 tools/audit_missing_world_content.py --cache data/source/b237-openrs2-2528/cache
python3 tools/audit_rendering_gaps.py --cache data/source/b237-openrs2-2528/cache --terrain-scope all-cache
python3 tools/audit_rendering_gaps.py --cache data/source/b237-openrs2-2528/cache --terrain-scope scene-cache --report tools/reports/rendering_gaps_scene_cache.txt --rows-report tools/reports/rendering_gaps_scene_cache_rows.tsv
```
