# Alpha.43 experimental JMV test-launch validation

Alpha.43 adds the first experimental direct test path for one loaded MPR plus
one active MOB. The implementation is build- and unit-tested; actual JMV
runtime behavior remains deliberately marked experimental until the focused
in-game test below is completed.

## Safety contract

- Source MPR/MOB files are never replaced by test launch.
- The exact validated in-memory MOB candidate is staged under LocalAppData.
- A random short ASCII name is used for unique temporary MPR/MOB copies in the
  active JMV `Maps` folder.
- The player's spawn is selected from all travel-zone deploy rectangles by
  scoring candidates against physical MOB object bounds and rectangle edges.
  A blocked center is avoided; launch is refused if no safe candidate exists.
- `Engine/autoexec.cfg` is backed up byte-for-byte and temporarily receives one
  `server <temporary-name>` command. Restoration occurs only if the current
  file still has Endurance's recorded patched hash.
- Temporary map copies are removed only if their hashes still match the run
  manifest. Interrupted cleanup is retried at the next editor start.
- An already-running `game.exe` blocks launch. Stop Test controls only the
  process started by Endurance.

## Automated evidence

- Production Windows x64 Qt5/MinGW build: pass.
- Spawn planner: blocked-center avoidance, fully blocked refusal, and travel-
  zone fallback: pass.
- Embedded CP1251 script replacement preserves every other raw leaf: pass.
- Bootstrap insertion ignores strings/comments and rejects malformed existing
  `WorldScript`: pass.
- Resource profile capability and manifest tests: pass.
- `zone1.mob` projection-transaction corpus: pass.
- Headless widget interaction/lifecycle check: pass.

## Alpha.42 launch finding corrected in alpha.43

The first runtime attempt proved temporary-map creation and cleanup but reached
the normal main menu. Its manifest showed the command was written to addon-owned
`2utoexec.cfg`; the active JMV configuration does not enable that optional
addon autoexec. The immutable `game.exe` contains and executes
`exec autoexec.cfg` during startup. Alpha.43 therefore uses the engine's real
`autoexec.cfg`, restoring an existing file byte-for-byte or deleting the
Endurance-created file when none existed before the run.

## Focused JMV runtime gate

1. Open a disposable/unchanged JMV MPR and its primary MOB, then use
   **Tools -> Test Current Map in JMV** (`Ctrl+F5`).
2. Confirm the game loads the requested terrain and MOB exactly once and the
   temporary player is controllable at an unobstructed travel-zone position.
3. Exercise `drawall`, traversal, unload/reload where possible, and graceful
   exit. Confirm the source map hashes remain unchanged.
4. Confirm the temporary `ed*.mpr`/`ed*.mob` pair disappears and
   `autoexec.cfg` is restored byte-identically or removed if it did not exist.
5. Repeat once with a forced game crash. Restart Endurance and confirm recovery
   removes only the recorded temporary files and restores only its own patch.

Multi-MOB launch, loaded-map identity/readiness reporting, and Base EI launch
remain unavailable.
