# Alpha.58 selected-zone and isolated-save JMV test

Alpha.57 proved that the fingerprinted complete New Game bootstrap can load a
staged editor MPR/MOB pair into a playable JMV session. It also exposed four
architectural issues: the injected script created a second player, New Game
retained zone1 session metadata, some MOBs could not be staged because script
injection was impossible, and the runtime could still enumerate live saves.

Alpha.58 removes MOB script injection. The staged MOB is the exact validated
editor candidate. During the normal New Game bootstrap, the bridge substitutes
the selected `map.txt` zone for zone1, allowing JMV itself to initialize one
player and profile-specific map metadata before the existing guarded map-loader
substitution supplies the staged pair. Map-table entrances retain source order:
the first clear hand-placed center wins for preflight, with a safe point inside
that same entrance selected only when its center is blocked. JMV remains
responsible for final deployment; the runtime test must verify it honors the
selected zone's entrance metadata.

All recognized save filesystem operations are redirected from the live JMV
`saves` tree into
`%LOCALAPPDATA%\EI Mapper Endurance\EI Mapper Endurance Edition\test-runs\<run>\runtime-state\isolated-saves`.
The live tree is not moved, copied, renamed, or used as the test namespace.
Requests for `1c.bik` and `nival.bik` are reported missing only inside the
launched test process, avoiding permanent movie renames.

## Focused runtime procedure

1. Load one matching JMV MPR and primary MOB, then press **Ctrl+F5**.
2. Confirm that preparation progress appears immediately. On the warning,
   optionally select **Don't show this warning again**.
3. Test one gameplay zone, one previously refused zone (zone17 or zone18), and
   one `bz` briefing zone.
4. Record whether there is exactly one controllable player, whether it starts at
   the map-table entrance, and whether terrain/objects/minimap/music/briefing UI
   match the selected zone.
5. Confirm the two intro movies do not play.
6. Open Save/Load. It must not list campaign saves. Create a disposable test
   save if possible, exit, and verify the file exists only below the run's
   `runtime-state\isolated-saves` folder.
7. Exit normally, then retain the newest run folder from **Tools -> Open JMV
   Test Runs Folder**.

The bridge log should contain `substituting New Game zone`,
`SAVE_REDIRECT`, `INTRO_SKIP`, `staged map loader returned result=1`, and
`staged_loader=observed`. Returning to the main menu or travelling away remains
allowed but may use original installed maps; it must still remain inside the
isolated save namespace.

## Validation gate

- Approved JMV game SHA-256 and byte signatures only.
- PE32 bridge built from source, with system imports only.
- Fresh main application build plus launcher and entrance-planner tests.
- Package contains no maps, saves, logs, dumps, settings, or local paths.
- Runtime validation must prove selected-zone metadata, one player, intro
  suppression, and live-save isolation before this path is called validated.
