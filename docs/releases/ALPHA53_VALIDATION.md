# Alpha.53 direct JMV map test

Alpha.53 is the first build that should load the staged editor map. It uses the
normal single-player path identified by the alpha.50-52 runtime evidence:

1. Stage the validated in-memory MPR/MOB pair under a unique temporary name.
2. Wait for JMV's ordinary main-menu map loader to return.
3. Queue a private message to that exact game thread—no fixed delay.
4. Invoke the fingerprinted normal New Game initializer.
5. Substitute only that initializer's verified MPR/MOB loader arguments.
6. Let the engine complete its normal session/player setup while the staged
   MOB bootstrap creates the disposable test player at an unobstructed travel
   zone location.

## Safety contract

- The provider must match the approved JMV `game.exe` SHA-256.
- The map-loader entry address is fixed only for that fingerprint and its
  original six bytes must match before the detour is installed.
- No console, multiplayer/server command, arbitrary command injection, fixed
  launch delay, or movie-file change is used.
- The New Game entry and its loader return site are fixed to the validated
  runtime. Only that exact loader call can receive the staged paths.
- Source maps, campaign saves, JMV configuration, Lua, and runtime binaries are
  not replaced. The configured plugin slot must already be empty.
- MPR/MOB files are staged under a unique hash-owned name. Cleanup removes only
  files whose SHA-256 still matches the run manifest.
- The test player is created by the staged MOB script in disposable state at a
  travel-zone point whose clearance was checked against loaded MOB objects.

## Focused runtime procedure

1. Load one unchanged JMV MPR and its matching base MOB in Endurance.
2. Press **Ctrl+F5** and accept the experimental direct-test confirmation.
3. Let the normal intro/menu lifecycle proceed. Do **not** click New Game; the
   provider should enter the staged editor map automatically once ready.
4. Confirm that the loaded terrain/objects match the selected editor pair and
   that a controllable default player appears at the reported travel zone.
5. Run `drawall`, move around, and confirm scripts and rendering remain active.
6. Exit the game normally and return the retained test-run folder.

The launch passes when `bridge-result.log` contains, in order:

- `DIRECT_LAUNCH queued ... after normal main-menu load`
- `DIRECT_LAUNCH invoking the normal New Game initializer ...`
- `DIRECT_LAUNCH redirecting normal New Game loader ...`
- staged temporary MPR/MOB file-open observations
- `DIRECT_LAUNCH ... completed ... staged_loader=observed`

The visible map and player must match the staged run. Exit code 0 is a normal
exit. A failure must leave source/live files unchanged and retain diagnostics.

## Build validation

- Build the x86 provider with the required one-argument `cdecl` exports and
  `/Oy-`.
- Verify PE32, system-only imports, byte guard, trampoline, fastcall detour with
  `ret 0x0C`, thread-message hook, and exact New Game return-site comparison.
- Run launcher/recovery, maps/catalog, spawn-planner, resource-profile,
  runtime-provider, script-reference, and MOB transaction regression tests.
- Build and freshly extract the Qt5 package; verify every checksum and confirm
  that it contains no settings, workflow logs, dumps, game assets, or old alpha
  validation document.
