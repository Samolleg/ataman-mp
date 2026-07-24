# Landscape Tools dock recovery manual test

## Changelist

- route the **L** shortcut, View menu, dock close button, and viewport requests
  through one Landscape Tools visibility controller;
- preserve the ordinary dock layout before opening Landscape Tools and restore
  it exactly when the tools close;
- reopen the dock at its remembered left/right area and docked height;
- retain Loaded Map while Landscape Tools is open;
- use Qt's normal floating-dock window flags so a floated Landscape Tools
  window can be docked again and remains recoverable after its close button;
- continue to start every editor process with Landscape Tools closed.

## Manual test cases

1. Arrange Loaded Map, Object Outliner, and Object Properties to visibly
   different heights. Press **L**, then **L** again. Confirm the three ordinary
   docks return to precisely their original sizes and positions.
2. Repeat test 1 five times. Landscape Tools must open at the same side and
   height every time rather than moving progressively toward the bottom.
3. Open Landscape Tools and drag it into a floating window. Drag or
   double-click its title bar to dock it again. Confirm docking remains
   available and Loaded Map remains visible.
4. Float Landscape Tools and close it with the window's cross button. Press
   **L**. The same tools must reopen and terrain editing must work without an
   editor restart.
5. Dock Landscape Tools, close it with its dock cross button, and press **L**.
   Confirm it reopens at the remembered dock side/height and closing it restores
   the ordinary dock layout.
6. Toggle Landscape Tools through **View > Landscape Tools** and then through
   **L**. The menu check state, panel visibility, and landscape-editing state
   must remain synchronized.
7. Close the editor while Landscape Tools is open. Restart and confirm the
   tools start closed while the last sculpt settings remain selected.
8. Enter and leave full-screen mode with Landscape Tools open. Confirm it is
   hidden/restored with the other docks and remains closable and reopenable
   afterwards.

## Automated evidence

- full Qt 5.12.12/MinGW release build: passed;
- `--ui-widget-check`: passed, including landscape-control and secondary-window
  lifecycle checks;
- no interactive editor window was launched during automated validation.
