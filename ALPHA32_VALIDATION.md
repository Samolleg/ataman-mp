# Alpha.32 transaction regression test matrix

Use copied MOB files or new Save As names. Alpha.32 focuses on the issues found
while testing alpha.31.

## MOB Parameters and scripts

1. Change diplomacy in MOB Parameters and press Apply. Confirm one `Edit MOB
   parameters` entry appears in the main undo history.
2. Undo and redo that entry. Confirm the diplomacy value and dirty marker follow
   the history position.
3. Edit the embedded script directly, Apply, undo, and redo. Save As and reopen
   the result to confirm the script round-trips.
4. Open the script externally, save it normally and then with an editor that
   replaces the file atomically. Accept the import prompt, confirm the text in
   MOB Parameters changes, then Apply. Repeat with unsupported non-CP1251 text
   and confirm it is rejected.

## Structural undo and repeated saves

1. Copy/paste a unit with patrol points, add a patrol point, then undo everything.
   Confirm the dirty marker clears and Save As creates a byte-identical copy.
2. Redo through the same history by clicking entries in the undo-history view.
   Confirm no crash and save the changed result.
3. Save a structural edit, make another ordinary edit, then Save As again. The
   second save must not report a raw-provenance mismatch.
4. Repeat edit/undo/redo/save twice on a large MOB and retain the workflow log.

## UI checks

- Create Object stays unavailable when no MOB is active.
- Save As starts in the active MOB's directory with its current filename.
- Shift-right-click copies selected objects; Shift-left-click pastes them.
- Object-detail values copy as readable text. Pasting a value into an
  incompatible field is rejected. Model and texture paste remains uncommitted
  until Enter or an explicit dropdown choice; Escape or focus loss cancels it.

For every changed MOB, reopen it in Endurance, compare it against its source,
and run the normal JMV `drawall`, unload/reload, traversal, and graceful-exit
checks. Attach the clean `logs` folder and source/output hashes to any report.
