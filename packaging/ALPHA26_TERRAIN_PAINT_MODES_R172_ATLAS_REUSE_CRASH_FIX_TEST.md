# Alpha 26 terrain paint modes R172 atlas-reuse crash fix test

R172 keeps the verified decoded-atlas reuse introduced in R171 and fixes the
deterministic first-load crash in that new path.

## Root cause and change

- A freshly constructed atlas inspector did not yet contain its eight quick
  access table items. The decoded-atlas adoption path updated those items
  directly, causing a null read before MPR loading could complete.
- Quick-access items are now a construction invariant and are also ensured at
  every resize/update boundary. The normal decoded-atlas and legacy fallback
  paths therefore share the same safe widget state.
- A headless regression now adopts a complete verified 64-tile atlas into a
  fresh inspector, verifies all eight quick items, selects a physical tile, and
  rejects an out-of-range working slot.

No terrain resolver, paint output, mask, semantic assignment, runtime terrain
type, atlas capacity, save, or publication rule changed.

## Timing comparison

The comparison package's Zone2 worklogs completed in 4,589 ms and 5,078 ms.
R170 completed in 5,479 ms. R171 crashed before emitting its MPR or workspace
completion timing, so it did not provide a valid performance measurement.

## Focused manual check

1. Open Zone2 through the same resource profile used for the comparison run.
2. Confirm the first load completes without a crash.
3. Send the newest `Terrain material catalog phases`, `MPR editor load phases`,
   and `Map workspace load phases` lines from `logs/workflow.log`.
4. The optimized path should report `prevalidatedTilesAdopted:true`,
   `atlasHashMs:0`, and a near-zero `tileTableMs`.
5. Open Manual mode and Material Atlas once and confirm thumbnails and physical
   tile selection are intact.

## Automated validation

- Document suite: 124 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape-tools widget suite: 7 passed, 0 failed.
- Fresh decoded-atlas adoption regression: access violation before the fix;
  explicit PASS afterward.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The crash and adoption invariants are covered headlessly. Actual Zone2 wall
time remains dependent on the configured RES profile, storage cache, and
renderer, so the new worklog timings are the authoritative performance result.
The editor and game were not launched by the build process.
