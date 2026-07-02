# RuneC Data Cleanup Remaining Plan

This is the single active tracker for unfinished data-cleanup work. Completed
implementation history lives in `data_cleanup_history.md`.

Do not add completed work back to this file. Remove an item only after the
matching report, test, runtime check, clean-clone check, or release gate proves
it is closed.

## Done Means

Remaining data cleanup is done when all of these are true:

- `runtime-data.lock` points to official runtime-data artifacts with URLs and
  checksums, and a clean clone can install them.
- Early v1 viewer validation limitations are documented as follow-up parity
  work instead of blocking the runtime-data release.
- `python3 tools/data_pipeline.py --check` and the CTest suite pass from a clean
  checkout after documented data setup.

## Ordered Work

### 1. Final Closure Verification

Required work:
1. Correct stale planning/docs so they match the final repo state.
2. Re-run source/provenance validators and ensure blocked private/wrong-game
   provenance is absent from active required runtime data.
3. Confirm any accepted RuneLite/RSMod/wiki evidence is documented as evidence,
   not as a local checkout dependency.
4. Split or organize the staged work so source imports, runtime code, tooling,
   generated data policy, and docs are reviewable.
5. From a clean checkout after documented data setup, run:
   - `python3 tools/data_pipeline.py --check`
   - `ctest --test-dir build --output-on-failure`
6. Confirm generated artifacts, source-controlled inputs, release artifacts, and
   ignored local installs are classified correctly.

Close when:
- The clean-checkout validation passes.
- The release gate is publishable.
- Early v1 parity limitations are documented as follow-up work.

### 2. Parity Work After Release

Current state:
- Full Varrock startup remains the baseline.
- Automated loose/pack smoke checks now verify scene assets for the current
  first-release validation set:
  - dev boss destinations: Graardor, KBD, Vorkath, Jad;
  - object/traversal smoke destinations: Edgeville dungeon, Varrock Rat Pits,
    Varrock sewer, Wilderness lever/coffin, Observatory ladder, Yanille
    railing.
- Validation-bank wielding now passes automated coverage for the current
  validation item set.
- Explicit player/NPC/object/all animation packs and projectile model packs are
  rebuilt and included in runtime packs.
- The external reference icon overlay is missing 26 newest b237 item icons; the
  current build keeps the b237-rendered icons for those items.
- `python3 tools/data_pipeline.py --check`, loose/pack viewer smoke, and CTest
  currently pass.
- Some manual validation paths still have early-v1 parity issues. These are
  accepted as post-release follow-up unless a gap blocks clean install, pack
  loading, agreed validation startup, or source/provenance correctness.

Required work:
1. Item icon correctness.
2. Exact item bonuses for cache fallback rows.
3. Animation parity.
4. Missing/partial scene visuals.
5. Deeper boss/mechanic polish.

Close when:
- The early-v1 follow-up parity list is explicitly resolved or superseded by a
  newer validation scope.

## Deferred Technical Debt

These are important but should not block the first data-cleanup release unless
we explicitly change the release bar:

- Removing every remaining `RcGameData` compatibility global.
- Moving all legacy immutable tables into `RcGameData`.
- Perfecting every boss mechanic beyond the post-release parity scope.
- Full-world visual coverage beyond the post-release parity scope.
- Dialogue behavior validation beyond ensuring dialogue data is not loaded by
  viewer startup unless explicitly requested.
