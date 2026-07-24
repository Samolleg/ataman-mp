# Semantic-paint multi-family validation R18

## Scope

R18 preserves an existing third terrain family where a new semantic stroke
meets an authored transition. It extends the pairwise adjacency work without
enabling changed-MPR saving.

## User evidence

- Diagnostic: `zone1test-paint-diagnostic.json`
- SHA-256: `8eebb67e4e867c43c3fef960d18b109bc80a28d92f393296bd3771188a21ecf9`
- Source MPR SHA-256:
  `a1ed4ac4f39e4da49bb29b3c80366db747be9f3a318726eaa2db9d1c1b150aae`
- The source hash exactly matches the copied private Zone1 fixture.

The version-3 current-state snapshot exposed three concrete shared-edge
disagreements south of cells `(99,40)`, `(100,40)`, and `(101,40)`. The new
Stone/lime route ended in Grass/common while the unchanged neighboring cells
still exposed Grass/dark.

## Atlas evidence

The registered Zone1 namespace contains authored three-family land tiles,
including:

- Ground/common + Grass/dark + Grass/common;
- Grass/dark + Grass/common + Stone/lime.

This is therefore a solver-boundary defect, not a missing-resource case.

## Correction

- The one-cell untouched halo supplies hard corner constraints at the edge of
  the local edit footprint.
- Those constraints may introduce a third or later family into requested tile
  signatures, selecting the atlas-authored junction instead of flattening the
  boundary to the selected/background pair.
- The complete constrained patch is preflighted before any command is emitted.
  If an exact multi-family layout is absent from the atlas, no partial
  multi-family patch is emitted; the established pairwise route remains the
  deterministic fallback.
- Painted core corners remain owned by the selected material.
- Variant selection and natural distribution remain deterministic.

## Automated evidence

- Public atlas tests prove both representative three-family signatures remain
  selectable in terrain-grid orientation.
- The copied private Zone1 regression replays the captured Ground/common and
  Grass/dark junction, requires a real 2+3+4 tile, and verifies every shared
  edge of every changed cell.
- It then replays the diagnostic's later Grass/dark and Stone/lime samples and
  verifies the full changed region remains corner-consistent.
- Full document suite: 48 passed, 0 failed, 1 unrelated optional fixture skip.
- Qt 5.12.12 / MinGW 7.3 release build passed.
- Offscreen widget and secondary-window lifecycle checks passed.

## Remaining manual check

On a fresh copied Zone1 map, create two different material regions whose halos
meet while both remain adjacent to Grass/common. The resulting junction must
use authored three-family tiles or an authored Grass/common route, with no
broken internal edge. Undo and redo must preserve the same layout.

Changed-MPR Save and Save As remain disabled.
