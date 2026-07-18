# Alpha.57 complete JMV New Game bootstrap test

Alpha.56 proved there is no missing asynchronous loader after the partial
single-player transition. The transition entered a responsive blue gameplay
shell, but it did not create the normal session or player and never requested
the staged map. The only later loader in the retained trace was the tester's
manual return to `ZoneMainMenuNew`.

Alpha.57 replaces that retired shell path with the complete, fingerprinted New
Game bootstrap at `game.exe+0x0017BD10`. The bridge invokes it on the captured
game controller and game thread. The normal New Game code prepares the current
session and player, then its already verified loader call at return site
`game.exe+0x0017A61B` receives the hash-owned staged MPR/MOB pair.

Because the real New Game bootstrap prepares JMV's transient `saves/current`
directory, Endurance now snapshots that directory under the unique test-run
folder before launch. Every copied file is SHA-256 verified. On game exit—or
interrupted-run recovery during the next editor start—the disposable current
session is removed and the previous byte-exact tree is restored. A damaged
backup is rejected before the live scratch directory is cleared.

## Focused runtime procedure

1. Load one matching JMV MPR and its primary MOB.
2. Press **Ctrl+F5** and accept the experimental confirmation.
3. Do not click New Game or load a save manually; the bridge performs the full
   bootstrap automatically after the normal main-menu load completes.
4. Report whether the staged editor terrain and objects appear, whether the
   disposable player is visible and controllable, and whether **drawall** works.
5. Exit normally without saving.
6. Open **Tools -> Open JMV Test Runs Folder** and retain the newest run folder.

The bridge log should contain:

- `invoking complete New Game bootstrap`
- `redirecting normal single-player transition loader`
- `staged map loader returned`
- `complete New Game bootstrap returned staged_loader=observed`

The runtime log should record the current-session snapshot before launch and
its restoration during cleanup.

## Safety and build validation

- Require the approved JMV `game.exe` SHA-256 and verified transition, New Game,
  and map-loader entry bytes.
- Preserve x86 calling conventions and original transition/loader flow.
- Do not replace source maps, campaign save slots, Lua, configuration, or
  runtime binaries.
- Place map copies and the bridge only at hash-owned temporary paths.
- Hash-verify the JMV `saves/current` snapshot before deleting disposable state.
- Build the PE32 provider with system imports only.
- Run launcher/recovery, map catalog, spawn planner, resource profile, runtime
  provider, script-reference, document, backup, duplicate-ID, resource-name,
  and frustum regression suites.
- Freshly extract and checksum the Qt5 package; exclude settings, logs, dumps,
  game assets, saves, and older validation documents.
