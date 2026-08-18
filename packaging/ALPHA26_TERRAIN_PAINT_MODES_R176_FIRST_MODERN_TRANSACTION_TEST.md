# Alpha 26 terrain paint modes R176: First Modern transaction test

This focused build makes semantic-field recovery part of the first Modern
paint transaction instead of a hidden mutation performed while planning the
stroke.

## Changes

- On a fieldless MPR, deterministic semantic recovery, the first Modern paint,
  its exact release blends, and every later stamp in the same drag gesture are
  now owned by one undo command.
- Planning or refusing a first Modern stroke no longer changes the live
  semantic field. A missing transition, canceled preview, or no-op therefore
  cannot leave hidden state behind.
- Undoing that first Modern command returns the MPR to the exact fieldless
  state that preceded it. A preceding Manual command can then be undone
  normally; redo restores the exact recovered field and packed tiles.
- Stroke release derives its affected ring from the recorded painted vertices
  even when the first command stores a complete field transition rather than
  sparse vertex deltas.

## Focused test

1. Open a stock/fieldless Zone 2 MPR, place one Manual tile, then make the first
   Modern paint stroke elsewhere. Include a short drag so the gesture contains
   more than one stamp.
2. Confirm the released stroke already has correct blends and one undo entry.
3. Undo Modern once. The whole Modern result must disappear while the Manual
   tile remains exactly as placed.
4. Undo once more. The Manual tile must disappear. Redo both commands and
   confirm the exact Manual artwork and Modern blend field return.
5. If a deliberately unsupported Modern material is available, attempt its
   first stroke. A refusal must leave no visible or hidden edit and add no undo
   entry.

No semantic material assignments, authored atlas artwork, or blend masks
changed in this revision.
