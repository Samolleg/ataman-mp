# Alpha 26 terrain paint modes R170 MPR binding-load test

This revision targets the synchronous MPR pause that remained unchanged in
R169. The R169 timing log proved that MOB activation was already fast and that
the remaining pause was inside terrain-atlas activation.

## Root cause and change

- **A verified embedded terrain profile is no longer decoded twice.** The
  archive selected before landscape activation is reused by the material
  catalog only when archive path, namespace, decoded atlas pixel hash, and slot
  count all exactly match the atlas loaded by the renderer.
- **Every mismatch still fails closed.** A different archive, namespace,
  artwork hash, or slot count takes the existing full verification path. The
  reuse approval is one-shot so a later profile-only edit cannot leave stale
  semantic metadata cached.
- **Manual palette icons are outside the blocking MPR transaction.** Hundreds
  of physical-tile hashes and icons are scheduled after MPR activation rather
  than contributing to the `MPR editor load phases` total. The palette remains
  current and is still rebuilt after atlas changes.
- **Load logs now expose the result.** `Terrain material catalog phases` reports
  `atlasHashMs`, `embeddedProfileLookupMs`, and
  `prevalidatedBindingReused` in addition to the existing phase timings.

No terrain resolver, paint result, authored mask, semantic assignment, runtime
terrain type, atlas capacity, or save/publication rule changed.

## Focused manual check

1. Open Zone2 through the same profile used for R169 and compare the MPR pause.
2. Inspect `logs/workflow.log`. The first catalog rebuild during load should say
   `prevalidatedBindingReused:true`, should have a small
   `embeddedProfileLookupMs`, and should report `manualPaletteMs:0`.
3. If a visible pause remains, send the newest `Terrain material catalog
   phases` and `MPR editor load phases` lines. They now separate the remaining
   renderer, atlas hash, profile lookup, catalog, and palette costs.
4. Open Manual mode, confirm the atlas grid appears, select one tile, paint it,
   and undo. This checks that moving the grid build out of MPR activation did
   not stale or disable it.

## Automated validation

- Document suite: 124 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape-tools widget suite: 7 passed, 0 failed.
- Exact prevalidated-binding match/mismatch regression: PASS.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The exact-binding contract and the non-blocking palette path are covered by
headless regressions. Interactive Zone2 timing still depends on the user's
configured RES profile, so the workflow timing lines remain the authoritative
measurement for any remaining pause. The editor and game were not launched by
the build process.
