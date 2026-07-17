# Alpha.34 document-script and first-patrol test matrix

Use copied MOB files or new Save As names. Keep workflow logs for any mismatch
or crash.

## Independent MOB script editor

1. Open a MOB, then use **Edit > Open MOB Script** (`Ctrl+Shift+M`). Confirm
   MOB Parameters is not involved and no second script editor is embedded there.
2. Edit the script and press Apply or `Ctrl+S`. Confirm one **Edit MOB script**
   history entry appears. Undo and redo it, Save As, reopen, and verify the text.
3. Leave unapplied text in the script window and close it, unload the MOB, and
   exit the editor in separate runs. Verify Save/Discard/Cancel works and no
   child window survives the main editor.
4. Leave unapplied text in the internal editor, then import an external change.
   Confirm the internal text is retained and the title reports pending local
   text rather than silently discarding it.

## External script workspace

1. Load a MOB without opening MOB Parameters or the internal script window.
   Edit its managed UTF-8 file under `%LOCALAPPDATA%\EI Mapper Endurance\script-workspaces`.
2. Save repeatedly and through an editor that atomically replaces files. Confirm
   at most one import prompt appears at a time.
3. Accept the import. Confirm one **Import external MOB script** entry appears in
   the main History window and undo/redo changes the embedded document script.
4. Close the internal window but leave the MOB loaded and edit externally again.
   Confirm detection continues.
5. Try non-Windows-1251 characters. Confirm the import is rejected without
   changing or dirtying the MOB.

## First patrol point

1. In Logic mode, select a unit with no patrol points and press `P`.
2. Confirm the unit changes to Path behavior, receives a selectable first patrol
   point, and enters the normal placement operation.
3. Confirm placement, then undo once. The point must disappear and the previous
   behavior must return. Redo must restore both as one action.
4. Save As and test the copied MOB in game, including `drawall` and unload/reload.

## Shortcuts and duplicate

1. Press `Ctrl+L`; the History window must open.
2. Select one or several objects and press `Ctrl+D`. Confirm copies are selected
   in the normal pending movement mode. Confirm/cancel/undo/redo exactly as for
   ordinary paste and verify it is one top-level history action.
3. Open **Settings > Key Bindings**, change History, Duplicate, Open MOB Script,
   Open MOB/MPR, Save All, Undo, or Redo; Apply and confirm the new binding takes
   effect immediately and survives restart.

## Preservation

- Run an unchanged Save As and confirm byte-identical output.
- Save each tested intentional edit separately and compare it against its source.
- Reopen changed outputs in Endurance and MobExplorer, then retain workflow logs,
  hashes, and reproduction steps for any unrelated change.
