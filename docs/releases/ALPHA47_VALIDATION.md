# Alpha.47 experimental JMV deferred-launch validation

Alpha.46 produced decisive runtime evidence. Starter loaded the hash-owned
`<modRoot>/plugin.dll`, called both exports with zero results, and the bridge
validated the runtime successfully. The selected map still did not load because
the `2utoexec.cfg` marker had already been processed before plugin readiness.

Alpha.47 removes that timing dependency. `EntryAfterPatch` registers a one-shot
Windows timer on the current game thread. After the message loop begins, its
callback installs exact MPR/MOB redirects and calls the fingerprinted internal
server handler with the real maps.txt identity. Current runs never read, write,
rename, or replace `2utoexec.cfg`; recovery support remains for interrupted runs
created by older alphas.

## Safety contract

- Source MPR/MOB files and JMV configuration are never replaced.
- Temporary maps and the packaged `.plg`/runtime `plugin.dll` provider are
  recorded by path and SHA-256 in a run manifest.
- The launcher verifies JMV's binary `config.reg` record for
  `DEV/Plugin=plugin.dll` and refuses to replace an existing file at that path.
- The provider verifies the approved JMV `game.exe` SHA-256 and exact internal
  server-handler prologue before scheduling the request.
- The deferred callback runs on the loader/game thread through the normal
  Windows message queue; no worker thread calls engine functions.
- At timer fire it patches only `CreateFileA/W` and `GetFileAttributesA/W`
  imports in loaded runtime modules. Only exact `<source>.mpr` and
  `<source>.mob` basenames redirect; all other files pass through unchanged.
- Temporary provider/maps are removed after exit only while their installed
  SHA-256 values still match. Interrupted cleanup is retried later.

## Automated and static evidence

- x86 provider build and undecorated one-argument `cdecl`
  `EntryBeforePatch`/`EntryAfterPatch` exports: pass.
- Both callbacks return zero as required by the installed Starter loader: pass.
- Alpha.46 runtime: provider discovery, callback ABI, runtime fingerprint, and
  cleanup all passed; missing autoexec-marker timing was isolated.
- Provider imports restricted to Windows system libraries: pass.
- Launcher/recovery, plugin-slot record, map catalog, spawn planner, profile,
  provider, and script-reference tests: pass before packaging.
- Production Qt5 build and clean-distribution validation: performed on the
  packaged tester build.

## Focused JMV runtime gate

1. Open an unchanged/disposable JMV MPR plus its primary MOB and press
   **Ctrl+F5**.
2. Confirm the bridge log records timer scheduling/firing, redirect installation,
   and the internal handler request.
3. Confirm the selected map loads instead of proceeding to the main menu.
4. Confirm the disposable player appears at the reported travel-zone point and
   is controllable. Exercise `drawall` and traversal.
5. Exit normally. Confirm `plugin.dll` and `ed*.mpr`/`ed*.mob` are gone, source
   hashes remain unchanged, and `2utoexec.cfg` was untouched.

Loaded-map identity reporting and multi-MOB launch remain later extensions; this
gate covers the requested single-MPR/single-MOB MVP.
