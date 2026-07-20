# 0.2.1-alpha.1 JMV hot-reload probe

This probe tests repeated Ctrl+F5 against the game process launched by
Endurance. It does not attach to an independently launched game and does not
modify source maps, installed JMV maps, runtime binaries, or campaign saves.

## Test order

1. Load zone1 and press Ctrl+F5. Wait until the editor reports that JMV is ready
   for hot reload and confirm normal play.
2. Make one visible disposable MOB change in the editor. Do not close the game.
   Press Ctrl+F5 again. The action should read **Hot Reload Map** and the game
   process/PID should remain the same.
3. Confirm the map transitions once, the change is visible, there is one player,
   controls remain usable, and the editor reports completion rather than timing
   out.
4. Repeat with a second change. Then load a different registered map pair in the
   editor, select an entrance, and press Ctrl+F5. Confirm normal entrance-based
   deployment on that map.
5. Use Stop Test and confirm the owned game process exits and the temporary live
   MPR/MOB/plugin files are removed.

## Expected limitations

- Exact same-map player position/facing preservation is not enabled. Reload uses
  the selected map-table entrance or explicit Play from Here anchor.
- Hot reload is JMV-only and requires the exact validated runtime fingerprint.
- Collection-backed multi-MOB launch is not included yet.

## Required report

Use the Test Runs quick-access link and send the complete newest run directory.
It contains `runtime.log`, `bridge-result.log`, `manifest.json`, the numbered
`reloads/` audit directories, and the request hashes. Also include the editor's
`logs/workflow.log` and state whether game exit was graceful.
