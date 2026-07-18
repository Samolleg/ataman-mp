# JMV direct single-player map launcher

This x86 `.plg` is a temporary, profile-specific provider for the fingerprinted
JMV runtime stack. It is not loaded into EI Mapper itself. During a test run
the editor copies it into JMV's configured `DEV/Plugin=plugin.dll` slot, where
Starter calls `EntryBeforePatch` and `EntryAfterPatch`. The slot must be absent;
Endurance never replaces an installed mod plugin. The provider and other
temporary files are removed after exit only when their SHA-256 values still
match the run manifest.

The provider verifies the exact approved `game.exe` hash and patches the
already loaded modules' imports of `CreateFileA/W` and
`GetFileAttributesA/W`. It records only `.mpr`, `.mob`, and `.mq` operations,
including thread ID and a bounded module-relative call stack, in the run's
`bridge-result.log`. It installs a byte-guarded detour at the normal
single-player loader identified by the alpha.51/52 evidence.

The provider first observes the engine's own `MainMenu`, mode-2 transition and
captures its real transition-controller object. After the ordinary main-menu
loader returns, it queues a private message to that same game thread. The
message hook resolves the requested `map.txt` zone record and invokes the
engine's normal transition as `(captured controller, zone ID, mode 0)`. The
loader detour substitutes only that transition's verified MPR/MOB arguments
with the staged Endurance paths. The engine retains its normal session, zone,
and player initialization. Module bases and each relevant file open remain
recorded for runtime verification.

The provider remains deliberately narrow:

- it invokes the fingerprinted normal single-player zone transition, not a
  console or multiplayer/server command;
- it derives the transition-controller pointer from the engine's own guarded
  main-menu call rather than treating a zone record as an object instance;
- it uses generic mode `0`; mode `2` is reserved by this runtime for the main
  menu and is never used for the requested editor map;
- it redirects only the expected loader call made by that transition;
- it does not modify Starter, JMV configuration, maps, saves, or movie files;
- it does not infer readiness from a fixed delay; the normal main-menu loader
  return and game-thread message loop provide the lifecycle boundary.

The user launches with Ctrl+F5 and waits for the normal intro/menu lifecycle.
The provider should automatically enter the staged map without a manual New
Game click. The previously tested internal `server` handler remains absent
because runtime evidence showed it enters an incomplete multiplayer/server
transition instead.

Starter treats a non-zero callback return as an assertion condition, so both
exports always return zero. Initialization failures are reported in the run
log and otherwise leave the runtime untouched.

Build with `build-x86.cmd` from an x86 Visual Studio 2022 environment. Generated
binary/PDB files under `bin/` are local build artifacts.
