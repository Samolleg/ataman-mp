# EI Mapper Endurance 0.3.0-alpha.26 — Object UI review r16

## What changed

- Object Properties now shows a collection editor for a selected magic trap,
  activation area, or cast target while Behavior mode is active.
- Activation areas and cast targets are presented as two independent sibling
  arrays, matching the MOB structure.
- Both arrays support drag reordering, arrow-button reordering, Undo/Redo, and
  an aim button that selects the actual child in the viewport and outliner.
- The collection fills the remaining Object Properties height and highlights
  the currently selected viewport child in gold.
- `+ Activation area` and `+ Cast target` append new children. Starting either
  action cancels any unfinished point placement before beginning the new one.
- Cast targets are displayed as 2D coordinates. The MOB stores only X/Y for a
  target; it has no authored rotation, direction, or height field.

## Focused test

1. Enter Behavior mode and select a magic trap, one of its activation areas,
   and one of its cast targets in turn. Confirm the same two-section collection
   remains visible.
2. Drag activation areas within their section, then Undo/Redo. Repeat for cast
   targets. Confirm a point cannot be dragged into the other section.
3. Use the arrow controls and the aim button. Confirm the outliner order and
   actual viewport selection follow the action without selecting the trap owner.
4. Start adding one kind of trap point, then click the other Add button before
   placing it. Confirm the unfinished point is cancelled and only the requested
   new point remains active for placement. Press Esc and confirm it is removed.
5. Save and reopen a test MOB. Confirm activation-area order, radii, cast-target
   order, and XY coordinates round-trip.

## Known limitation

Legacy MapEd trap arrays whose declared length is four bytes short can be opened
and preserved, but direct edits remain save-blocked until the explicit repair
transaction is implemented. Endurance does not silently normalize them.
