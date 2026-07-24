# Landscape Preview R2 — Changelist

Code baseline: branch `feature/landscape-editing`, commit `90cc10b`.

## Landscape workflow

- Unified landscape editing in **View > Landscape Tools (Preview)**.
- Added explicit modes:
  - **Sculpt terrain**;
  - **Paint land tiles**;
  - **Paint existing overlays**.
- Transformed `B` from the inherited hidden tile-brush mode into the visible **Paint land tiles** workflow.
- Added **Choose atlas tile...** inside Landscape Tools.
- Preserved overlay safety: painting may change an existing overlay tile, but cannot silently create a liquid surface.

## Sculpting

- Raise, Lower, Flatten, and Smooth remain canonical SEC-backed preview operations.
- Holding the left mouse button still now repeats the sculpt stamp every 100 ms.
- Moving and stationary stamps remain grouped as one undoable stroke until mouse release.
- Losing editor focus safely ends the current stroke.

## Visualization

- Added **View > Show Terrain Tile Grid**.
- The cyan overlay outlines atlas-tile boundaries (2 x 2 terrain units), not internal vertex triangles.
- Grid geometry follows live sculpted terrain.

## Menu safety

- Save Active MOB, Save Active MOB As, and Save All MOBs are disabled when no MOB is active.
- Atlas categories remain read-only.
- Direct liquid-material editing remains disabled in this preview increment.

## MPR save contract

- Changed-MPR Save and Save As remain disabled.
- Unchanged same-path Save is now a true no-op: no backup, replacement, byte write, or timestamp change.
- An unchanged loaded MPR that changed externally on disk is refused rather than overwritten.
- Unchanged Save As retains fresh reopen validation, destination backup where applicable, and atomic byte-identical copying.
- The three supplied JMV `zone1` fixtures were byte-identical; the observed difference was timestamp-only.

## Test resources

- Added five deterministic, synthetic 257 x 257 grayscale PNGs under `heightmaps/`:
  orientation corners, radial hill, diagonal ramp, terraced basin, and seam cross.
- Each fixture has a documented SHA-256 hash and contains no game asset data.

## Known limitations

- Changed MPR serialization is intentionally unavailable.
- Overlay creation and liquid-material editing are not enabled yet.
- Ramp sculpting exists in the tested canonical core but is not exposed in this preview UI.
- Navigation data is not regenerated; terrain changes continue to emit navigation-risk diagnostics.

## Automated evidence

- `endurance_document_tests`: 43 passed, 0 failed.
- Full Qt/MinGW release build: passed.
- Supplied `zone1.mpr`, `zone1test.mpr`, and backup `zone1.mpr`: 1,523,282 bytes each and SHA-256
  `A1ED4AC4F39E4DA49BB29B3C80366DB747BE9F3A318726EAA2DB9D1C1B150AAE`.
- No live game file is included in the package.
