# Alpha.20 transform cancellation and document-history validation

## Paste placement Undo

1. Copy and paste an object, but do not confirm placement.
2. Press Undo once.

Expected: the pasted object is removed, its temporary movement is reverted, the
original object is selected, and the viewport returns to Select mode. A later
left click must not create a movement edit or dirty the MOB.

The workflow log should contain `Cancelled pending viewport operation
method:move` with its tracked-object count.

## Ordinary movement Undo

1. Select an existing object and begin moving it.
2. Before confirming the movement, press Undo once.

Expected: the object returns to its pre-movement position, Select mode becomes
active, and no prior history entry is undone. The MOB dirty state remains what
it was before the unfinished movement.

## Document lifecycle boundary

1. Make several undoable edits and inspect the History window.
2. Open another MPR or MOB.

Expected: opening the document does not add a Load/Open entry to History.
Opening an additional MOB preserves safe edit history for already loaded MOBs;
replacing the current MPR clears history because terrain-edit commands belong to
the replaced landscape.

3. Close an active MOB or use File -> Close All.

Expected: the history is cleared before object storage is destroyed. Undo and
Redo remain disabled until a new edit is made. Reopening the last session and
repeatedly pressing Undo/Redo must not unload documents or crash.

## Regression

Repeat paste, confirmed movement, Undo movement, Undo paste, Redo paste, and
Redo movement. Exact object selection and dirty-state restoration must remain
correct. Selection-only changes remain serialization-neutral.
