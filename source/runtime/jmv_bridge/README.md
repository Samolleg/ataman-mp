# JMV direct single-player map launcher

This x86 `.plg` is the run-local provider for the fingerprinted Base or Starter
`game.exe`. Its engine hooks depend only on the executable's guarded code
layout. Starter addon.dll, SpellAddonX.asi, and the frequently updated JMV
addon.dll are not Endurance bridge-delivery dependencies or compatibility
gates. They remain profile gameplay components and JMV features may naturally
depend on them after the game starts.

Endurance uses the same owned-process transport for Base and JMV. Its packaged
32-bit runtime host creates the selected `game.exe` suspended, loads a copy of
this bridge from the run directory, calls `EnduranceInitialize` explicitly,
and resumes the game only after the bridge accepts the fingerprint and guarded
hook sites. Endurance never places `plugin.dll` in the mod, uses the JMV
`DEV/Plugin` loader, injects into an independently started process, or requests
elevation. The run-local provider is removed after exit only when its SHA-256
still matches the recovery journal.

The provider verifies the exact approved `game.exe` hash and patches the
already loaded modules' imports of `CreateFileA/W` and
`GetFileAttributesA/W`. It records only `.mpr`, `.mob`, and `.mq` operations,
including thread ID and a bounded module-relative call stack, in the run's
`bridge-result.log`. It installs a byte-guarded detour at the normal
single-player loader identified by the alpha.51/52 evidence.

The provider first observes the engine's own `MainMenu`, mode-2 transition and
captures its real transition-controller object. After the ordinary main-menu
loader returns, it queues a private message to that same game thread and uses
the complete New Game bootstrap. The loader detour substitutes only the
bootstrap's verified MPR/MOB arguments with the staged Endurance paths. The
engine retains its normal session, zone, and player initialization. Relevant
file operations retain module-relative call stacks for verification;
initialization records only an aggregate count of already loaded modules rather
than dumping every Windows DLL path.

For an explicitly unmapped MPR/MOB pair, the bridge obtains the selected
bootstrap record through the fingerprinted game's byte-guarded map-table lookup
and temporarily gives that record a run-local identity, MPR dimensions, empty
map/minimap metadata, and one synthetic entrance. It never scans or locks the
process heaps and never writes the installed `texts.res`/`map.txt`. The original
record is restored before a mapped reload or process cleanup.

The provider remains deliberately narrow:

- it invokes the fingerprinted normal single-player zone transition, not a
  console or multiplayer/server command;
- it derives the transition-controller pointer from the engine's own guarded
  main-menu call rather than treating a zone record as an object instance;
- it uses generic mode `0`; mode `2` is reserved by this runtime for the main
  menu and is never used for the requested editor map;
- it redirects only the expected loader call made by that transition;
- it does not modify Starter, JMV configuration, campaign saves, or movie
  files; temporary map aliases are hash-owned and removed after exit;
- it does not infer readiness from a fixed delay; the normal main-menu loader
  return and game-thread message loop provide the lifecycle boundary.

Hot reload protocol v3 gives each initial launch/reload an immutable map and
save generation beneath the owned run directory. Save path switching occurs on
the game thread immediately before a committed transition. The bridge records
guarded runtime-state changes and defers reload while state is initializing or
in a recent transition. Every reload uses the engine's normal Main Menu
transition followed by the complete New Game bootstrap so map metadata and UI
state are rebuilt together. A responsive loader-less stale transition is the
observed world-map/map-screen case and follows that same rebootstrap path. No
generic inventory/store UI function is detoured.

The user launches with Ctrl+F5 and waits for the normal intro/menu lifecycle.
The provider should automatically enter the staged map without a manual New
Game click. The previously tested internal `server` handler remains absent
because runtime evidence showed it enters an incomplete multiplayer/server
transition instead.

The legacy Starter callback exports remain for binary compatibility. The owned
runtime host prepares the bridge before process resume, caches the main module
at that loader-safe point, then activates byte-guarded map detours only after
the JMV executable patch phase becomes quiescent. Post-patch activation must
not call module-loader APIs while the main thread is suspended: that thread may
still own the Windows loader lock even after executable bytes stop changing.
Each bridge-export phase and timeout is written to `runtime-host.log`; a failed
activation terminates only the Endurance-owned game process.

The complete user-facing dependency contract, including the conditional
`AutoRunSpellAddon123.mob` requirement for Play from Here, Godmode, and explicit
supplemental MOB loading, is documented in
[`docs/TEST_CURRENT_MAP_DEPENDENCIES.md`](../../../docs/TEST_CURRENT_MAP_DEPENDENCIES.md).

Build with `build-x86.cmd` from an x86 Visual Studio 2022 environment. Generated
binary/PDB files under `bin/` are local build artifacts.
