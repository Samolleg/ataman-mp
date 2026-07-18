# Alpha.54 direct JMV map test

Alpha.54 corrects the failed Alpha.53 trigger. Both Alpha.53 runtime runs proved
that `game.exe+0x00056140` only repeated startup/main-menu initialization and
never reached the staged loader. Alpha.54 instead invokes the same
map-table-driven single-player transition observed during a manual New Game:

1. Stage the validated in-memory MPR/MOB pair under a unique temporary name.
2. Wait for JMV's ordinary main-menu map loader to return.
3. Queue a private message to that exact game thread; no fixed delay is used.
4. Resolve the selected `map.txt` zone in the engine's loaded zone catalog.
5. Invoke the fingerprinted normal single-player transition routine.
6. Substitute only that transition's verified MPR/MOB loader arguments.
7. Let the engine complete its own transition bookkeeping while the staged MOB
   bootstrap creates the disposable test player at an unobstructed travel zone.

## Safety contract

- The provider must match the approved JMV `game.exe` SHA-256.
- The map loader, zone resolver, zone catalog, transition entry, and exact
  loader return site are specific to that fingerprint and byte-checked where
  code is entered or patched.
- No console, multiplayer/server command, arbitrary command injection, fixed
  launch delay, movie-file change, or synthetic UI input is used.
- Only the loader call made by the verified single-player transition may
  receive staged MPR/MOB paths.
- Source maps, campaign saves, JMV configuration, Lua, and runtime binaries are
  not replaced. The configured temporary plugin slot must already be empty.
- MPR/MOB files use a unique hash-owned name. Cleanup removes only files whose
  SHA-256 still matches the run manifest.
- The disposable player bootstrap uses a travel-zone point checked for
  clearance against loaded MOB objects.

## Focused runtime procedure

1. Load one unchanged JMV MPR and its matching primary MOB in Endurance.
2. Press **Ctrl+F5** and accept the experimental direct-test confirmation.
3. Let the normal intro/menu lifecycle proceed. Do **not** click New Game.
4. Confirm that the selected editor map loads automatically and that its terrain
   and objects match the staged pair.
5. Confirm that a controllable default player appears at the reported travel
   zone, then run `drawall` and move around.
6. Exit normally and return the newly retained test-run folder.

The bridge log should contain, in order:

- `DIRECT_LAUNCH queued ... after normal main-menu load`
- `DIRECT_LAUNCH invoking normal single-player transition zone=...`
- `DIRECT_LAUNCH redirecting normal single-player transition loader ...`
- staged temporary MPR/MOB file-open observations
- `DIRECT_LAUNCH single-player transition completed ... staged_loader=observed`

The visible map and player must match the staged run. A failed launch must leave
source/live files unchanged and retain diagnostics.

## Build validation

- Build the x86 provider with its required one-argument `cdecl` exports and
  `/Oy-`; verify PE32 and system-only imports.
- Inspect the byte guards, trampoline, fastcall detour with `ret 0x0C`, exact
  transition return-site comparison, zone lookup, and game-thread hook.
- Run launcher/recovery, map catalog, spawn planner, resource profile, runtime
  provider, script-reference, and MOB transaction regression tests.
- Build and freshly extract the Qt5 package; verify every checksum and confirm
  that it contains no settings, workflow logs, dumps, game assets, or older
  alpha validation document.
