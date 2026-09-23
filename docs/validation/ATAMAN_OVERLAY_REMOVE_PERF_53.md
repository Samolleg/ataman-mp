# EI-OVERLAY-REMOVE-PERF-53 — validation

Date: 2026-09-23
Base: Ataman 52, `74adbecabcfae5911057c0497d71e69c71468557`
Branch: `codex/overlay-remove-performance-v53`

## Change

- `CSector::updateWaterTiles` now recomputes shared material emission and
  opacity only for grid vertices within changed tiles and their adjacent tile
  copies, instead of walking every vertex copy in the 16×16 sector.
- Water index data is rebuilt only when a changed overlay material crosses the
  empty/non-empty boundary. Changing the material on an already-present tile
  leaves its topology untouched.
- The land VBO is uploaded only when an affected underlay emission actually
  changes. The water VBO remains orphaned/uploaded after geometry or material
  changes to preserve the existing in-flight-frame ownership behavior.
- Overlay paint completion logs command, model, render and picking durations,
  picks, preview sector count, and stamp count.

## Checks

- Release compilation: PASS using Qt 5.12.12 / MinGW 7.3, fresh shadow directory
  `build-release-53-overlay-remove-performance`; executable produced at
  `build-release-53-overlay-remove-performance/release/EIMapperEndurance.exe`.
- Automated tests and interactive editor/game playback were not run. In
  particular, performance improvement and rendered boundary continuity still
  need confirmation with a repeat of the user's removal stroke.
- No installed application, map, RES archive, or game data was modified.
