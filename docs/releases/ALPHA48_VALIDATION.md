# Alpha.48 experimental JMV game-window dispatch validation

Alpha.47 isolated the next failure boundary. Starter loaded the provider and
`EntryAfterPatch` scheduled a Windows timer, but the run-specific bridge log
never recorded its callback. The Starter plugin callback therefore executes on
a thread that does not pump timer messages; the map handler was never called.

Alpha.48 replaces that timer with a bounded worker that waits for the largest
visible top-level window owned by `game.exe`. It temporarily subclasses that
same-process window and posts one private message. The window procedure is
restored before the callback installs exact MPR/MOB redirects and invokes the
fingerprinted internal server handler on the actual window thread.

## Safety contract

- Source MPR/MOB files, JMV configuration, and `2utoexec.cfg` are untouched.
- Temporary maps and `plugin.dll` are path- and SHA-256-owned by the run
  manifest and removed only while their hashes still match.
- The provider verifies the approved JMV `game.exe` SHA-256 and exact internal
  handler prologue.
- The discovery worker never calls game code. It waits at most 30 seconds and
  only posts a private message to a visible window owned by the current process.
- The original game window procedure is captured, checked for concurrent
  replacement, and restored before the engine call.
- Only exact `<source>.mpr` and `<source>.mob` basenames redirect. Every
  redirected attribute check or file open is recorded in the bridge log.

## Automated and static evidence

- x86 provider build and required one-argument `cdecl` exports: pass.
- Both callbacks return zero as required by Starter: pass.
- Provider imports restricted to Windows system libraries: pass.
- Launcher/recovery, map catalog, spawn planner, profile, provider, and script
  reference tests: required before packaging.
- Production Qt5 build and extracted-package hash validation: required before
  packaging.

## Focused runtime gate

1. Load an unchanged/disposable JMV MPR plus its primary MOB and press
   **Ctrl+F5**.
2. Confirm the bridge log records window discovery, the source and target
   thread IDs, receipt of the private message, redirect installation, and the
   internal handler request.
3. Confirm the selected map loads instead of the main menu.
4. Confirm the disposable player appears at the reported clear travel-zone
   point and is controllable; exercise `drawall` and traversal.
5. Exit normally and confirm temporary files are removed without source changes.

Loaded-map identity reporting and multi-MOB launch remain later extensions.
