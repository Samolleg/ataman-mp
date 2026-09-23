# EI-OVERLAY-REMOVE-PERF-53 — validation

Date: 2026-09-23
Base: Ataman 52, `74adbecabcfae5911057c0497d71e69c71468557`
Branch: `codex/overlay-remove-performance-v53`
Source commit: `75008847d9c7366f5a3692463c22076c7b72e1de`

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
- Raw and staged `--version`: PASS, `EI Mapper Endurance Edition 53`.
- `--ui-widget-check` on the staged application exited 1. The report showed
  failures in `compatiblePaste*`, `fieldPaste*`, and
  `numericComponentPaste` checks; other listed checks passed. A repeat against
  the raw executable produced no output before it was stopped as our owned
  test process. These failures are not overlay-removal checks, but the broader
  UI suite is not a pass.
- Package: `C:/GOG Games/maped/ei-mapper-ataman/build-staging-53-overlay-remove-performance`;
  finalizer and distribution validator PASS. The 101 managed paths have valid
  SHA-256 entries, and the staged executable hash matches the fresh Release.
- Permanent install: `artifacts/EI-Mapper-Ataman-2-first-test`, version 53;
  all 101 managed files match the package manifest. The executable hash is
  `1dfc3d6bc45a8b3b0d5477f41bbb3d855a756e16f7b4e0e40c644a7812fbd42a`.
  A pre-update rollback was hash-verified at
  `artifacts/rollback-EI-Mapper-Ataman-v52-to-53-20260923`.
- 19,407 unlisted user files remained in the installation; their paths and
  lengths were unchanged. No map, RES archive, or game data was modified.
- User confirmation is still needed for performance improvement and rendered
  boundary continuity by repeating the removal stroke on the installed build.
