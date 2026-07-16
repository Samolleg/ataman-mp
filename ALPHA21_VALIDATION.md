# Alpha.21 deferred paste validation

## Cancelled paste is not history

1. Copy and paste an object, but do not confirm placement.
2. Press Undo once.

Expected: the preview copy disappears, the original object is selected, Select
mode is active, and the MOB returns to its exact previous dirty state. Redo must
remain unavailable unless an older, unrelated redo entry existed before paste.
No empty redo step may appear and a later click must not dirty the MOB.

## Confirmed paste is history

1. Copy and paste an object, move the preview, and confirm placement.
2. Undo movement, then Undo paste.
3. Redo paste, then Redo movement.

Expected: the paste and its confirmed movement are separate history entries.
The exact copy is recreated, selected, and moved back to its confirmed position.
The source selection is restored when paste creation is undone.

## Lifecycle regression

Begin a paste preview and use Close All without confirming placement. Decline
saving if prompted, reopen the last session, and select the source object.
Expected: no crash, no preview object in the reopened MOB, and no stale Undo or
Redo entry.
