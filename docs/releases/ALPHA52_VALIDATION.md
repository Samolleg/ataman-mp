# Alpha.52 JMV map-loader entry probe

Alpha.51 captured a normal, graceful-exit JMV session containing the complete
main-menu, New Game (`zone1`), travel (`zone7`), and main-menu-return file
sequence. Immediate callers and recovered game frames reconcile to the normal
single-player loader at `game.exe+0x00195480` on the approved runtime.

Alpha.52 adds one narrow entry probe to confirm that loader's real arguments,
owner, caller, and thread during those same transitions. It remains an
observe-only feasibility build and is not expected to load the editor map.

## Safety contract

- The provider must match the approved JMV `game.exe` SHA-256.
- The entry address is fixed only for that fingerprint and its original six
  bytes must match before the probe is installed.
- The hook copies the complete displaced instruction into a trampoline and
  captures values without calling logging or runtime APIs on the game thread.
- A separate telemetry worker formats captured values after the engine has
  continued through its original code.
- It invokes no console or internal game command and redirects no file.
- It does not alter maps, saves, configuration, Lua, runtime binaries, or movie
  files. Hash-owned temporary files retain the established cleanup rules.

## Focused runtime procedure

1. Load one unchanged JMV MPR/MOB pair in Endurance and press **Ctrl+F5**.
2. Confirm that the dialog describes an observe-only map-loader entry probe.
3. Let JMV reach its main menu and start **New Game** normally.
4. Enter the first playable map and travel to one other zone if convenient.
5. Return to the menu if convenient, then exit the game normally.
6. Return the retained test-run folder.

The probe passes when JMV behaves normally and `bridge-result.log` contains
`MAP_LOAD_ENTRY` lines for the observed transitions. Those lines must expose
stable `this`, caller, and path arguments. Process exit code 0 is a normal exit,
not a crash signal.

## Build validation

- Build the x86 provider with the required one-argument `cdecl` exports and
  `/Oy-`.
- Verify PE32, exports, system-only imports, target-byte guard, displaced
  instruction, trampoline jump, and state-preserving naked capture stub.
- Run launcher/recovery, maps/catalog, spawn-planner, resource-profile,
  runtime-provider, script-reference, and existing focused regression tests.
- Build the production Qt5 application and validate a freshly extracted
  package without user settings, workflow logs, or crash dumps.
