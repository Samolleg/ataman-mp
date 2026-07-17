# Alpha.33 external-script and property-clipboard test matrix

Use copied MOB files or new Save As names.

## External script editor

1. Open MOB Parameters, launch the external script editor, modify the script,
   and save repeatedly in quick succession.
2. Confirm Endurance shows at most one import question at a time. Choose Yes and
   verify the MOB Parameters text changes. Apply, undo, redo, Save As, and reopen.
3. Repeat and choose No. Confirm the same unchanged external content does not
   immediately ask again.
4. Save through an editor that replaces the file atomically. Confirm one import
   question still appears and Endurance remains responsive.
5. Try unsupported non-CP1251 characters. Confirm one encoding warning appears,
   no import occurs, and there is no prompt storm or crash.

## Property clipboard

1. Right-click a text value and its corresponding label. Both menus must offer
   enabled Copy value and compatible Paste value commands.
2. Select a green property label and use Ctrl+C/Ctrl+V on the same property of
   another object. Model and texture pastes apply immediately.
3. Shift/Ctrl-select several labels, copy them, select the same ordered property
   types on another object, and paste. Confirm all values change as one undo step.
4. Select a different number or type/order of destination fields. Paste must be
   rejected without changing any field.
5. Repeat with non-text values: a dropdown, vector, color, multiline list, body
   parts, and unit stats. Structured values must require a matching typed
   Endurance clipboard value.
6. Paste copied plain text into a text editor. Confirm it is readable.
7. Confirm Shift-click in the viewport no longer copies or pastes objects and
   continues to behave as the normal viewport interaction.

## Menu state and preservation

- MOB Parameters is disabled with no active MOB and enabled after a MOB loads.
- Run an unchanged Save As and confirm byte-identical output.
- Reopen changed outputs, compare against their sources, and retain workflow
  logs and hashes for any failure.
