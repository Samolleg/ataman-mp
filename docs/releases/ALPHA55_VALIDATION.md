# Alpha.55 direct JMV map test

Alpha.55 corrects the transition ABI established by the Alpha.54 runtime trace.
The selected `map.txt` zone (`gz11k` in that test) resolved correctly, but
Alpha.54 passed its zone record as the transition object and requested mode `2`.
The engine reserves mode `2` for `ZoneMainMenuNew`, so it reloaded the menu and
later crashed while using the invalid object pointer.

Alpha.55:

1. Hooks the fingerprinted transition entry before startup continues.
2. Captures the real controller from the engine's own `MainMenu`, mode-2 call.
3. Waits for that ordinary main-menu map load to return.
4. Resolves the selected `map.txt` zone as an independent preflight check.
5. Calls the original transition on the same game thread with the captured
   controller, selected zone ID, and generic mode `0`.
6. Substitutes only the map loader returning at `game.exe+0x0017A61B` with the
   hash-owned staged MPR/MOB pair.

## Safety contract

- The provider must match the approved JMV `game.exe` SHA-256.
- Transition, resolver, and map-loader entry bytes are checked before patching.
- A missing main-menu controller, missing zone, wrong call site, or failed hook
  disables direct launch rather than widening the redirect.
- The transition and loader trampolines preserve their original calling
  conventions (`thiscall`, detours returning with `ret 8` and `ret 0x0C`).
- Partial hook installation restores the transition entry before returning a
  failure.
- No console/server command, fixed delay, movie change, UI input, campaign
  save, or live-map replacement is used.
- Source maps, JMV configuration, Lua, and runtime binaries are not replaced.
- Cleanup removes only hash-owned temporary maps and bridge files.

## Focused runtime procedure

1. Load one matching JMV MPR and its primary MOB.
2. Press **Ctrl+F5** and accept the experimental confirmation.
3. Do not click **New Game**; let the menu lifecycle proceed.
4. Confirm that the selected map loads automatically.
5. Confirm that the disposable player appears at the logged clear travel-zone
   point, then use `drawall` and traverse the map.
6. Exit normally and retain the run folder.

The bridge log should contain, in order:

- `captured normal transition controller=...`
- `queued ... after normal main-menu load`
- `invoking ... zone=... controller=... requested_mode=0`
- `redirecting normal single-player transition loader ...`
- `single-player transition completed ... staged_loader=observed`

The run folder can be opened from **Tools -> Open JMV Test Runs Folder**.

## Build validation

- Build PE32 provider with only system imports and the required exports.
- Inspect both trampolines, controller capture, mode `0`, exact return-site
  comparison, fastcall detour cleanup, and partial-install restoration.
- Run launcher/recovery, map catalog, spawn planner, resource profile, runtime
  provider, script-reference, and MOB transaction regression suites.
- Build and freshly extract the Qt5 package; verify every checksum and confirm
  that it contains no settings, logs, dumps, game assets, or older validation.
