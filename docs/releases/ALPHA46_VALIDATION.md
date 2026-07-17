# Alpha.46 experimental JMV plugin-redirect validation

Alpha.44 established that temporary map staging and cleanup are reliable, the
private autoexec marker reaches the game thread, and the internal server handler
accepts the request. Alpha.45 moved the provider away from the warning-producing
ASI path and added exact map-file redirection. Three runtime attempts proved the
loose `.plg` was not discovered: there was no bridge result file and no Starter
`Loading plugin` log entry.

The installed JMV `config.reg` declares `DEV/Plugin=plugin.dll`. Disassembly of
Starter's loader confirmed that it loads that exact configured file; `*.plg`
enumeration occurs only when the configured value resolves to a directory.
Alpha.46 therefore verifies this config entry and temporarily fills the otherwise
absent `<modRoot>/plugin.dll` slot. It refuses to replace any existing file and
removes only its own hash-matching provider.

Static disassembly of the installed Starter `addon.dll` proved the plugin ABI:
`EntryBeforePatch` and `EntryAfterPatch` are looked up by undecorated name, each
is called as a one-argument `cdecl` function, and a nonzero return enters the
loader's error/assert path. Alpha.46 preserves the correct exports and calling
convention while returning zero from both callbacks. Initialization failures are
written to the run-specific bridge log and leave the runtime unpatched.

The provider calls the internal handler with the real maps.txt identity (for
example, `zone13`) and temporarily redirects only that map's `.mpr`/`.mob` file
opens to hash-owned `ed????????` copies. The source/live map is never renamed or
replaced, and its embedded MP/SEC namespace remains unchanged.

## Safety contract

- Source MPR/MOB files are never replaced.
- Temporary maps, the packaged `.plg`/runtime `plugin.dll` provider, and the `2utoexec.cfg` marker are recorded
  by path and SHA-256 in a run manifest.
- The provider verifies the approved JMV `game.exe` SHA-256 and exact console
  hook signature before installing anything.
- It accepts only the random run's private marker and a strict ASCII source map
  identity inherited from the validated editor request.
- At marker time it patches only `CreateFileA/W` and `GetFileAttributesA/W`
  imports in loaded runtime modules. Only exact `<source>.mpr` and
  `<source>.mob` basenames redirect; all other files pass through unchanged.
- The provider runs only inside the launched game and is removed after exit only
  while its installed SHA-256 still matches.
- Existing `2utoexec.cfg` bytes are restored only while the active file still
  matches Endurance's recorded patch. Interrupted cleanup is retried later.

## Automated evidence

- x86 provider build and required undecorated `EntryBeforePatch`/
  `EntryAfterPatch` exports: pass.
- Installed Starter loader ABI and zero-success return semantics: verified by
  direct disassembly of its callback lookup/call/error branches.
- Provider imports restricted to Windows system libraries: pass.
- Exact game hash, console calling convention, and hook prologue inspection:
  pass.
- Launcher/recovery, map catalog, spawn planner, profile, provider, and script
  reference test executables: pass before packaging.
- Production Qt5 application build and clean-distribution validation: performed
  on the packaged tester build.

## Focused JMV runtime gate

1. Open an unchanged/disposable JMV MPR plus its primary MOB and press
   **Ctrl+F5**.
2. Confirm there is no obsolete-ASI warning and the game loads the selected map
   instead of proceeding to the normal intro/main menu.
3. Confirm the disposable player appears at the reported travel-zone point and
   is controllable. Exercise `drawall` and traversal.
4. Exit normally. Confirm the `ed*.mpr`/`ed*.mob`, temporary `plugin.dll`, and marker
   are gone, while the source map hashes remain unchanged.
5. Provide `workflow.log` and the latest LocalAppData test-run directory. Its
   bridge log records provider loading, redirect-hook count, each redirected
   MPR open, marker handling, and cleanup.

Loaded-map identity reporting and multi-MOB launch remain later extensions; this
gate covers the requested single-MPR/single-MOB MVP.
