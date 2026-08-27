# Alpha 26 terrain paint modes R167 load-stall fix test

This revision removes the remaining Zone2 pauses at 33% and 66% that were
still present in R166.

## Root cause and change

- **Save-state checks no longer hash tile artwork per map cell.** A publication
  check introduced in R164 asked for a full SHA-256 identity for every terrain
  cell. Zone2 has 30,720 cells and two terrain layers, so title/save-state
  refreshes repeatedly hashed roughly 15 GiB of duplicated 256x256 image data.
- **Hot scans now use constant-time slot-origin metadata.** Authored, imported,
  generated, invalid, and unmapped slots keep the same fail-closed publication
  behavior without hashing their pixels.
- **Load diagnostics expose the exact UI phase.** `workflow.log` now records
  `saveStateRefreshMs` for MPR loading and `titleMs`, `outlinerMs`,
  `propertiesMs`, and `stateSignalMs` for MOB activation.

No terrain resolver, Manual paint, authored mask, semantic assignment, runtime
terrain type, atlas capacity, or RES member validation changed in this revision.

## Focused manual checks

1. Open Zone2 and confirm the former 33% and 66% multi-second stalls are gone.
2. Close the editor and inspect `logs/workflow.log`. The Zone2 MPR line should
   contain a small `saveStateRefreshMs`; the MOB line now identifies any
   remaining title, outliner, properties, or state-signal time independently.
3. As a regression check, paint and undo one Manual tile, make one Modern
   stroke, and invoke Shift+R once. Their R166 behavior should be unchanged.

## Automated validation

- Document suite: 122 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The R166 workflow timings and R164 commit boundary identify the per-cell artwork
hash as the first incorrect operation behind both remaining pauses. Automated
tests prove the hot scan uses the constant-time classifier and preserve origin
classification, but only the user's installed Zone2 profile reproduces the
actual progress-bar timing and disk/cache environment.
