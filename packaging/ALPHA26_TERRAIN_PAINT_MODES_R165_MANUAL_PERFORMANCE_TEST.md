# Alpha 26 terrain paint modes R165 manual performance test

This revision removes the map-wide semantic work accidentally introduced into
each Manual tile click and its undo command in R164.

## Changes

- **Manual clicks are local again.** Painting one Manual cell records its
  physical tile plus at most the four shared-corner values touched by that
  cell. It no longer reconstructs the complete map in a temporary model.
- **Undo/redo stays proportional to the stroke.** Adjacent Manual stamps merge
  their shared vertices into one sparse command. They do not carry four full
  map-sized semantic snapshots through the undo stack.
- **Plain maps remain physical-first.** If a map has no semantic field yet,
  Manual painting writes only the selected physical artwork. A later Modern or
  Shift+R operation initializes once from the resulting packed cells.
- **Existing semantic maps remain exact.** Assigned Manual artwork updates the
  touched current and saved-baseline corners together. Unassigned live artwork
  invalidates only its four touched corners and still routes to Material Atlas;
  unrelated terrain semantics remain intact.

No resolver ownership rule, authored mask, material assignment, or atlas
capacity policy was changed in this performance revision.

## Focused manual checks

1. Open Zone2, select Manual, and paint single cells as well as a short drag.
   Confirm each click appears immediately without a multi-second freeze.
2. Undo and redo those Manual strokes. Confirm both operations are responsive
   and restore the exact physical tiles and rotations.
3. After Manual painting, switch to Modern and paint across the edited area.
   Confirm Modern continues from the Manual result without resurrecting the
   previous terrain.
4. Repeat the R164 single-tile case: place one assigned Manual uniform inside a
   different 3x3 material area, then press Shift+R. Confirm the Manual material
   remains the center and the blend resolves outward.

## Automated validation

- Focused sparse Manual command/undo regressions: 4 passed, 0 failed.
- Document suite: 120 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape Tools widget suite: 5 passed, 0 failed.

## Manual validation boundary

The model regressions prove that one cell carries four sparse vertices and an
adjacent two-cell stroke carries six, with exact undo/redo. End-to-end viewport
latency still requires the focused interactive checks above because automated
tests do not launch the editor or create a live OpenGL scene.
