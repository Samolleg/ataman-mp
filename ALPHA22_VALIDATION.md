# Alpha.22 quick UI validation

## Selection and outliner

1. Single-click an outliner object: it becomes selected without moving the camera.
2. Double-click it: the camera centers on the selected object or group.
3. Select an object in the viewport, then click empty landscape.
4. Undo and redo those selection changes.

Expected: the outliner current row clears when nothing is selected and reveals
the exact selected row after Undo/Redo. Selection remains serialization-neutral.

Press Escape in Select mode. Expected: selection clears as one undoable action.
Press Escape during move, rotate, scale, and unconfirmed paste placement.
Expected: the operation cancels without creating an edit; paste previews vanish.

## Recent files and shortcuts

Open several MPR and MOB files. Expected: File -> Open Recent lists up to 12
paths newest-first, moves reopened files to the top, removes missing entries when
selected, and offers Clear Recent Files. The list persists after restart.

- Ctrl+O: Open MPR or MOB.
- Alt+Shift+O: Open MOB(s).
- Ctrl+Shift+O: Open MPR.
- Alt+Shift+S: Save All MOBs.

## Model and texture filtering

In both Object Details and Create New Object, click Model name or Texture and
type several characters. Expected: typed text remains visible, the popup filters
case-insensitively, selecting a result applies it, and entering a complete custom
name not present in the list is accepted when editing finishes.
