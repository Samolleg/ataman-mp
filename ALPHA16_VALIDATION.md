# EI Mapper Endurance 0.1.0-alpha.16 validation

## Undo-to-clean and copy-only Save As

Alpha.16 corrects the sticky MOB changed flag reported during alpha.15 testing.
The editor now captures a deterministic projection baseline after loading a MOB
and compares the current projected state with it after edit, undo, redo, and at
the Save As boundary.

Expected behavior:

- A real UI edit shows the changed marker and changed MOB saves remain blocked.
- Undoing every projected change back to the load baseline clears the marker.
- Save As then writes the preserved original bytes as a byte-identical copy.
- Redo restores the marker and again blocks changed Save/Save As.
- Save As never silently discards visible edits by writing the original bytes.

The refusal message now explicitly says that all edits must be undone until the
changed marker disappears before byte-identical Save As is available.

## Focused manual test

Use a copied MOB:

1. Record its SHA-256 and open it.
2. Change one ordinary property and confirm the changed marker appears.
3. Undo once and confirm the marker disappears.
4. Save As and verify source and destination SHA-256 values match.
5. Redo the edit and confirm Save As is refused without creating or replacing a
   destination.
6. Repeat with multiple property edits and undo all of them.

Object deletion undo now restores the object's original projected ordering
instead of appending it. Report any command that remains marked changed after a
complete undo, since it may require the same command-specific ordering fix.

## Local validation

- Windows x64 Qt 5.12.12/MinGW release build: passed.
- Existing document, backup, resource-profile, resource-name, duplicate-ID,
  and runtime-provider suites: 44 passed, 0 failed.
- Focused interactive undo/Save As sequence: pending tester confirmation.
