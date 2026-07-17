# Alpha.27 atomic paste and provenance validation

Use copied MOB files only. Structural changed saves remain disabled in this
build. The purpose of this pass is to validate UI history behavior and the new
raw-object provenance bridge before it is allowed to emit structural output.

## Atomic paste Undo/Redo

1. Open a representative copied JMV MOB.
2. Select one object, copy it, paste it, move the automatically selected copy,
   and confirm placement once.
3. Press Undo once.

Expected: the pasted object disappears, the original object is selected, movement
mode is inactive, and the dirty marker clears if this paste was the only change.
There must not be a second empty or movement-only history item.

4. Press Redo once.

Expected: the exact pasted object and its confirmed placement return together as
one action. Repeating Undo/Redo must neither create additional objects nor crash.

Also test pressing Undo before confirming placement. The preview must be cancelled,
the pasted object must disappear, and no redoable history entry may be created.

## Raw provenance alignment

Open representative JMV MOBs, including one with duplicate NIDs if available.
The workflow log should contain:

`Bound raw MOB provenance to <count> projected objects sourceSha256:<hash>`

The count must equal the loaded object count. Report any occurrence of:

`Raw MOB provenance binding unavailable because projection order did not align`

Same-MOB copy/paste should retain this provenance through Undo/Redo. Save As of
the structural edit must still be refused in alpha.27; this confirms that adding
provenance did not accidentally weaken the fail-closed save boundary.

## Changed-save regression

Repeat one isolated Player assignment edit on a disposable source copy. It must
still produce exactly one authorized `OBJ_PLAYER` leaf edit and pass staged
validation. A model, texture, position, copy, or delete edit must remain blocked.

Return the clean-session `logs/workflow.log` and identify the MOBs used. Keep the
log private because it contains installation paths.
