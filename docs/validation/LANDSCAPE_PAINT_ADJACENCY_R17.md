# Semantic-paint adjacency validation R17

## Scope

R17 makes consecutive strokes of the same semantic material merge into one
corner-consistent terrain region. It does not enable changed-MPR saving and it
does not claim arbitrary multi-family blending is complete.

## User evidence

- Diagnostic: `zone1-paint-diagnostic.json`
- SHA-256: `cb3731b030963796b80d1fffc34a8bdd6aaaef31f50925e66d0da07fa8dba103`
- Source MPR SHA-256:
  `a1ed4ac4f39e4da49bb29b3c80366db747be9f3a318726eaa2db9d1c1b150aae`
- The diagnostic source hash exactly matches the copied private Zone1 fixture
  used for the runtime regression.

The R16 diagnostic contains 217 append-only audit deltas over 18 consecutive
brush centers. It also contains stale pre-state entries after undo/history
activity, so it must not be treated as a final-state snapshot.

## Correction

- The selected material is excluded from the perimeter background vote. Its
  presence means the new stroke is continuing an existing painted region.
- Existing corners in the selected-to-background material route seed the local
  distance field. Nearby selected corners therefore join the new stroke rather
  than being overwritten as unrelated background.
- Existing signatures are decoded in terrain-grid corner space; atlas-image Y
  conversion remains confined to catalog selection.
- Diagnostic schema version 3 records a stable stroke ID and a current-state
  snapshot of every audited cell plus its one-cell halo.

## Automated evidence

- A synthetic adjacent-stroke regression verifies merge locality, shared
  corners, undo, and redo behavior.
- The copied private Zone1 regression replays all 18 captured brush centers and
  verifies both shared corners on every east and south edge of every changed
  cell.
- Full document suite: 48 passed, 0 failed, 1 unrelated optional fixture skip.
- Qt 5.12.12 / MinGW 7.3 release build passed.
- Offscreen widget and secondary-window lifecycle checks passed.

## Remaining manual check

Recreate the four nearby Ground/common strokes on a fresh copied Zone1 map.
They must form a continuous region with inward-facing external transitions and
no internal seam between strokes. If they do not, export the version-3 paint
diagnostic; it contains actual stroke grouping and current packed tile state.

Changed-MPR Save and Save As remain disabled.
