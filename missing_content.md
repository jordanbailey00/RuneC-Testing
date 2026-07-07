# Missing Content Audit

This document tracks why world NPCs and objects can be present in generated
data but missing from the viewer/runtime scene. It is driven by:

- `tools/audit_missing_world_content.py`
- `tools/reports/missing_world_content.txt`
- `tools/reports/missing_world_content_rows.tsv`
- `tools/reports/missing_world_content_regions.tsv`
- `tools/reports/missing_world_content_ids.tsv`

The row report is the authoritative list of exact missing instances. It includes
`content`, `cause`, `id`, `name`, `x`, `y`, `plane`, `region_x`, `region_y`,
and cause-specific detail.

## Result

Current audit after the fixes in this change:

- NPC spawn rows scanned: `24,110`
- NPC rows blocked or missing: `0`
- Object placement rows scanned: `4,768,076`
- Object rows with no current visual path: `0`
- Fixed Varrock animated object sidecar rows: `1,354`

Initial audit before these fixes:

- NPC spawn rows scanned: `24,110`
- NPC rows blocked or missing: `4,862`
- Object placement rows scanned: `4,768,076`
- Object rows with no current visual path: `10,108`
- Fixed Varrock animated object sidecar rows: `1,354`

## Exact Causes

### `npc.instance_filtered_static_spawn`

Count: `3,314`

Cause:
- `data/spawns/world.npc-spawns.bin` rows can carry `NSPN_FLAG_INSTANCE`.
- `rc-core/npc.c` treats that flag as "skip during static world-spawn loading".
- The flag was generated from a name-level wiki `locline.mapid` rule in
  `tools/export_spawns.py`: if every locline row for an NPC name has a positive
  map id, every NPCList row with that name is marked instance-only.
- That rule is too broad for scene rendering and manual validation. It hides
  valid loaded-map NPC rows such as Stronghold Security Minotaurs and Flesh
  Crawlers when the player is actually in that map window.

Fix applied:
- Keep the conservative default skip for generic static world loading.
- Add an explicit active-area/viewer mode that materializes instance-flagged
  rows when the loaded scene window includes their coordinates.
- The default audit now follows viewer/active-area semantics and this bucket is
  `0`. Use `--strict-static-instance-skip` to audit the old conservative static
  policy explicitly.

### `npc.missing_npc_render_model`

Count: `1,548`

Cause:
- The active NPC spawn row is loadable and has an NPC definition.
- The first audit saw no `data/models/npcs.models` entry keyed by that NPC cache
  id and classified it as a missing render model.
- A direct b237 cache check showed all `973` unique ids in this bucket have no
  cache model ids either. This is not an exporter loss; these are model-less
  cache definitions/control rows.

Fix applied:
- The audit now checks the b237 NPC config before reporting this bucket.
- It only reports `npc.missing_npc_render_model` when the generated NPC model
  pack is missing an id that the b237 cache says has model ids.
- Current count is `0`.

### `object.transform_object_no_default_model`

Count: `10,051`

Cause:
- The object placement exists in `data/defs/object_placements.bin`.
- The b237 cache loc definition has transforms and no directly renderable
  default model for the placed id/type.
- The visual exporter currently treats that as no current visual path instead
  of resolving a stable/default transform target for render-only scene output.

Fix applied:
- Add transform/default-state visual resolution in the object visual path.
- Keep the gameplay object id unchanged; only the visual model selection should
  use the resolved target.
- The cache object exporter and audit now resolve transform targets for visual
  selection. Current count is `0`.

### `object.no_visual_loc_definition`

Count: `57`

Cause:
- The object placement exists in gameplay data.
- The b237 cache loc-definition decode used by the visual exporter does not
  produce a visual loc definition with models for that object id.
- Current rows are mostly Fairy rings, plus two Spiritual Fairy Tree rows and
  one alternate Fairy ring id.

Fix applied:
- Resolve these through cache transform/default-state logic where possible.
- For any genuinely model-less ids, add a data-driven render alias to a cache
  loc id with the same visible object, not a per-location hardcode.
- The exporter now merges runtime `object_defs.bin` visual fallback definitions
  into cache loc definitions for render-only resolution. Current count is `0`.

## Verification

Run:

```sh
python3 tools/audit_missing_world_content.py
python3 tools/data_pipeline.py --check
```
