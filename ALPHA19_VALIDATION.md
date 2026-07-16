# Alpha.19 gesture undo and lifecycle validation

## Reproducible crash regression

1. Open an MPR and MOB containing a wolf.
2. Copy and paste the wolf.
3. Move the pasted copy without clicking to finish the move.
4. Choose **File -> Close All**, decline saving, then choose **Open Last Session**.
5. Click the wolf.

Expected: the unfinished transform is cancelled before document destruction, the session
reopens normally, and clicking the wolf does not crash. The workflow log records
"Cancelled pending viewport operation method:move" with the tracked-object count.

## Gesture-level undo

- Move, rotate, scale, edit a property, randomize, or delete several selected objects.
  One Undo must revert the entire user action and one Redo must restore it.
- Paste several objects. The creation of the complete pasted set is one history entry.
- Finish the paste movement, Undo movement, then Undo paste creation. The original
  source selection must be restored.
- Redo paste creation. Only the pasted objects must be selected.
- Repeat against a MOB containing duplicate NIDs and confirm the exact selected
  instances change.

## Overlapping selection

Paste an object without moving it away from its source. The selected copy must render
over the identical deselected source at equal depth.

Selection-only changes must remain serialization-neutral. No-op MOB round trips must
remain byte-identical.
