# 0.3.0-alpha.26 Object UI review r18

## Changes

- Dragging a patrol, look point, trap activation area, or trap cast target now
  previews the complete resulting order directly in its collection.
- Dropping on the top half of a row consistently inserts before it; dropping
  on the bottom half consistently inserts after it. The preview renumbers all
  affected rows so the pending result is explicit.
- Leaving the collection or pressing Esc restores the original preview order
  without creating an undo entry. A completed drop remains one undoable move.
- Reorder completion now updates only the affected Behavior hierarchy in the
  outliner instead of rebuilding every object row. The workflow log records
  projection, outliner, property, and total reorder timings for follow-up
  performance diagnosis.
- Includes the r17 responsive X/Y/Z vector-property layout and full-value-on-
  focus behavior.

## Focused manual checks

1. Drag patrol 2 over the upper half of patrol 0. Confirm the collection
   immediately previews `2, 0, 1`, then release and verify that order remains.
2. Undo and repeat over the lower half of patrol 0. Confirm the preview and
   committed result are `0, 2, 1` every time.
3. Drag a look point repeatedly across several rows without releasing. Confirm
   the complete look order and numbering follow the cursor smoothly, while a
   different patrol remains an invalid destination.
4. During a changed preview press Esc, then repeat by moving the cursor outside
   the collection. Confirm both restore the original order and add no history
   entry.
5. Repeat with activation areas and cast targets. Confirm each can reorder only
   inside its own trap section.
6. Check `logs/workflow.log` after completed drops. `Behavior collection
   reorder` and `Trap collection reorder` lines report per-stage milliseconds.

## Known limitations

- Drag preview is deliberately collection-only; the outliner changes after the
  drop commits rather than mirroring an unfinished drag.
