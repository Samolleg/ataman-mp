# Alpha.56 JMV follow-on loader test

Alpha.55 proved that the fingerprinted single-player transition can leave the
main menu and enter a gameplay-like shell. The result had a working pause menu
but no terrain, player, cursor, or controls. A later `zone1.mob`/`zone1.mpr`
load in the trace was initiated manually by the tester; it was not scheduled by
the transition. The staged MPR/MOB pair was not opened.

The same trace exposed a bridge lifecycle error: Alpha.55 disarmed its staged
map request immediately when the transition returned without a synchronous map
loader call. It therefore could not observe or redirect a normal follow-on New
Game or disposable-save load initiated from that state.

Alpha.56 is a narrow follow-on loader test:

1. It keeps the staged request armed after the transition function returns.
2. It records up to 32 follow-on calls to the already fingerprinted map-loader
   entry, including caller address/RVA, MPR and MOB arguments, controller,
   context, and thread.
3. It retains Alpha.55's exact `game.exe+0x0017A61B` redirect guard. A verified
   normal single-player call receives the staged pair; every other caller is
   observed but left unchanged.
4. It continues to clean up only the hash-owned temporary bridge and map files.

## Focused runtime procedure

1. Load one matching JMV MPR and its primary MOB.
2. Press **Ctrl+F5** and accept the experimental confirmation.
3. Let the automatic transition enter the blue/empty gameplay shell.
4. Prefer **Esc -> return to main menu -> New Game** if that route is available.
   If it is not, use Load Game only with a disposable test save and do not save
   that session over any campaign slot.
5. Report whether the staged editor terrain/objects and a controllable player
   appear. Exit normally without saving.
6. Open **Tools -> Open JMV Test Runs Folder** and retain the newest run folder.

The bridge log should contain:

- `single-player transition returned ... request remains armed`
- one or more `pending loader observed ...` entries
- on an exact verified call, `redirecting normal single-player transition loader`

Those entries determine whether the existing normal-loader path can complete
the staged test session after the shell transition, and preserve the evidence
needed if a separate session/bootstrap call is still missing.

## Safety and build validation

- Require the approved JMV `game.exe` SHA-256 and verified entry bytes.
- Preserve x86 calling conventions and the original loader/transition flow.
- Do not replace live maps, saves, Lua, configuration, or runtime binaries.
- Build the PE32 provider with system imports only.
- Run launcher/recovery, map catalog, spawn planner, resource profile, runtime
  provider, script-reference, and MOB transaction regression suites.
- Freshly extract and checksum the Qt5 package; exclude settings, logs, dumps,
  game assets, and older validation documents.
