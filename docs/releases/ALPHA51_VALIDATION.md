# Alpha.51 JMV single-player caller probe

Alpha.50 safely recorded the normal JMV file sequence during New Game, travel,
and return to the main menu. Its optional stack walker returned no frames, so
the exact engine function initiating each transition remained unidentified.

Alpha.51 keeps the same observe-only behavior and records the immediate caller
address at each intercepted MPR/MOB/MQ operation. That address is captured
before the original API call and reported as both a module-relative offset and
an absolute runtime address. The provider is also built with frame-pointer
omission disabled so deeper stack telemetry can work where the runtime permits.

## Safety contract

- The provider must match the approved JMV `game.exe` SHA-256.
- It invokes no console or internal game command.
- It redirects no file and does not alter maps, saves, configuration, Lua,
  runtime binaries, or movie files.
- It observes only `.mpr`, `.mob`, and `.mq` calls through already loaded
  modules' `CreateFileA/W` and `GetFileAttributesA/W` imports.
- Each observation includes UTC time, thread ID, operation, path, result,
  access/disposition values, a mandatory immediate caller, and an optional
  bounded call stack.
- Temporary probe and correlation-map files remain hash-owned and are removed
  after exit. Externally changed files are retained rather than overwritten or
  deleted.

## Focused runtime procedure

1. Load one unchanged JMV MPR/MOB pair in Endurance and press **Ctrl+F5**.
2. Confirm that the dialog describes an observe-only transition probe.
3. Let JMV reach its main menu and start **New Game** normally.
4. Enter the first playable map and, if convenient, travel to one other zone.
5. Exit the game normally.
6. Return the retained test-run folder or its `bridge-result.log` and
   `runtime.log`.

The probe passes when JMV behaves normally and relevant `TRANSITION_PROBE`
lines contain a non-empty `caller=module+0x...@0x...` field. It is not expected
to load the editor's staged map yet.

## Build validation

- Build the x86 provider with the required one-argument `cdecl` exports and
  `/Oy-`.
- Verify that the provider is PE32 and imports system libraries only.
- Run the maps/catalog, spawn-planner, script-reference, launcher/recovery, and
  existing regression tests.
- Build the production Qt5 application and validate a freshly extracted
  package without user settings, workflow logs, or crash dumps.
