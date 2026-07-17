# Alpha.49 experimental JMV stable-window dispatch validation

Alpha.48 found a visible game-owned window, replaced its window procedure, and
successfully queued a private launch message. The message was never dispatched.
The window had been selected less than one second after process start, before
the launcher itself considered the game window ready for focus.

Alpha.49 requires the same largest visible top-level game window to remain
stable for twenty 100 ms polls. It then uses a five-second-bounded
`SendMessageTimeout` rather than an asynchronous post. Windows executes the
temporary procedure on the target window thread; the worker never calls game
code. The original procedure is restored before map redirects are installed and
the internal server handler is invoked.

## Safety contract

- Source maps, JMV configuration, and `2utoexec.cfg` remain untouched.
- Temporary maps and `plugin.dll` remain manifest- and SHA-256-owned.
- The provider verifies the approved game hash and handler prologue.
- Window discovery is restricted to visible, unowned top-level windows in the
  current process with at least a 640x480 client area.
- The window must remain stable for two seconds; dispatch times out after five.
- The temporary window procedure passes every unrelated message through to the
  exact captured original procedure.
- The original procedure is restored before the map handler runs.
- File redirects remain restricted to exact source MPR/MOB basenames and are
  logged individually.

## Required evidence before packaging

- x86 provider build and required one-argument `cdecl` exports.
- Zero callback return values and system-library-only imports.
- Launcher/recovery, map catalog, spawn planner, profile, provider, and script
  reference tests.
- Production Qt5 build and extracted-package hash validation.

## Focused runtime gate

Load an unchanged JMV MPR/MOB pair and press **Ctrl+F5**. The bridge log must
record stable-window selection, receipt on the target thread, redirect
installation, the internal handler request, and redirected file access. Then
confirm the selected map and disposable player load, exercise `drawall`, exit,
and confirm hash-owned cleanup.
