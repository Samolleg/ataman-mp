# Alpha.45 experimental JMV plugin-redirect validation

Alpha.44 established several facts independently: temporary map staging and
cleanup are reliable, JMV loads the Endurance bridge, the private autoexec
marker reaches it on the game thread, and the internal server handler accepts
the request. It did not load the map because the random outer name was not a
valid runtime map identity. Starter also warned that the temporary `.asi` looked
like an obsolete addon. The game's `0xC0000005` exit status in that run followed
a normal user-requested exit and is not classified as a launch crash.

Alpha.45 uses Starter's supported mod-scoped `*.plg` provider interface instead.
It calls the internal handler with the real maps.txt identity (for example,
`zone13`) and temporarily redirects only that map's `.mpr`/`.mob` file opens to
the hash-owned `ed????????` copies. The source/live map is never renamed or
replaced, and its embedded MP/SEC namespace remains unchanged.

## Safety contract

- Source MPR/MOB files are never replaced.
- Temporary maps, the `.plg` provider, and the `2utoexec.cfg` marker are recorded
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
- Provider imports restricted to Windows system libraries: pass.
- Exact game hash, console calling convention, and hook prologue inspection:
  pass.
- Alpha.44 runtime evidence: provider loaded, marker accepted, handler queued,
  normal cleanup removed all temporary state.
- Launcher/recovery, map catalog, spawn planner, profile, and provider tests:
  pass before packaging.
- Production Qt5 application build: pass.
- Launcher/recovery, map catalog, spawn planner, profile, provider, and script
  reference test executables: pass.
- Clean-distribution validation: performed on the packaged tester build.

## Focused JMV runtime gate

1. Open an unchanged/disposable JMV MPR plus its primary MOB and press
   **Ctrl+F5**.
2. Confirm there is no obsolete-ASI warning and the game loads the selected map
   instead of proceeding to the normal intro/main menu.
3. Confirm the disposable player appears at the reported travel-zone point and
   is controllable. Exercise `drawall` and traversal.
4. Exit normally. Confirm the `ed*.mpr`/`ed*.mob`, temporary `.plg`, and marker
   are gone, while the source map hashes remain unchanged.
5. Provide `workflow.log` and the latest LocalAppData test-run directory. Its
   bridge log records provider loading, redirect-hook count, each redirected
   MPR open, marker handling, and cleanup.

Loaded-map identity reporting and multi-MOB launch remain later extensions; this
gate covers the requested single-MPR/single-MOB MVP.
