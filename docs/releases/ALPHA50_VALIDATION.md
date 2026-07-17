# Alpha.50 JMV single-player transition probe

Alpha.49 proved that the fingerprinted internal `server` handler is not a
usable local single-player map transition. It was invoked on the game-window
thread after the main-menu MPR rendered, but never requested the selected
temporary MPR/MOB. The menu froze or the process later crashed.

Alpha.50 removes that handler and all map redirection. Ctrl+F5 now launches a
telemetry-only probe intended to locate the real single-player transition used
by New Game and normal zone travel.

## Safety contract

- The provider must match the approved JMV `game.exe` SHA-256.
- It invokes no console or internal game command.
- It redirects no file and does not alter maps, saves, configuration, Lua,
  runtime binaries, or movie files.
- It observes only `.mpr`, `.mob`, and `.mq` calls through already loaded
  modules' `CreateFileA/W` and `GetFileAttributesA/W` imports.
- Each observation includes UTC time, thread ID, operation, path, result,
  access/disposition values, and a bounded module-relative call stack.
- The run log records loaded module paths, bases, and sizes for offline address
  reconciliation.
- The temporary provider and correlation map copies remain hash-owned and are
  removed after normal exit. Externally changed files are retained rather than
  overwritten or deleted.

## Focused runtime procedure

1. Load one unchanged JMV MPR/MOB pair in Endurance and press **Ctrl+F5**.
2. Confirm the dialog says this is an observe-only transition probe.
3. Let JMV reach its main menu. Intro movies may be skipped manually, but do
   not rename files for this test.
4. Start **New Game** normally and enter the first playable map.
5. If convenient, travel normally to one additional zone.
6. Exit the game normally.
7. Return the run's `bridge-result.log` and `runtime.log`.

The test passes as a probe when the game behaves normally and the log contains
`TRANSITION_PROBE` MPR/MOB observations with `game.exe+0x...` call-stack frames.
It is not expected to load the editor's staged map.

## Build validation

- Build the x86 provider with the required one-argument `cdecl` exports.
- Verify that its imports are system libraries only.
- Run launcher/recovery and existing unit tests.
- Build the production Qt5 application and validate a freshly extracted
  package without user settings, workflow logs, or crash dumps.
