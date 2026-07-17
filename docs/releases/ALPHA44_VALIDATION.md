# Alpha.44 experimental JMV bridge validation

Alpha.43 proved that temporary map staging and cleanup work, but its command
route does not: the validated JMV game reports `server` as an unknown command.
Alpha.44 replaces that rejected route with a temporary, profile-specific x86
bridge. Runtime success is not claimed until this build passes the focused JMV
test below.

## Safety contract

- Source MPR/MOB files are never replaced.
- Temporary map copies, the launch bridge, and the `2utoexec.cfg` marker are
  recorded by path and SHA-256 in a run manifest.
- Existing `2utoexec.cfg` bytes are backed up exactly and restored only while
  the active file still matches Endurance's recorded patch.
- The bridge verifies the approved JMV `game.exe` SHA-256 and the exact engine
  hook signature before modifying process memory.
- It accepts only the random run's private `endurance_launch ed????????` marker.
- The bridge runs only inside the launched game process and is removed after
  exit only while its installed SHA-256 still matches.
- Interrupted cleanup is retried on the next Endurance start.

## Automated evidence

- x86 bridge build: pass; PE architecture is `pei-i386`.
- Bridge imports are restricted to Windows system libraries.
- Exact JMV `game.exe` hash and hook prologue inspection: pass.
- Engine calling-convention inspection confirms command text is supplied in
  `ECX`, which is what the bridge hook reads.
- Launcher/recovery tests, including owned-bridge removal and externally changed
  bridge retention: pass.
- Production Windows x64 Qt5/MinGW application build: pass.
- Focused map catalog, spawn planner, compatibility profile, runtime provider,
  and launcher/recovery regression suites: pass.
- Clean-distribution validation: pass for the packaged candidate.

## Focused JMV runtime gate

1. Open a disposable JMV MPR plus its MOB and use **Tools -> Test Current Map in
   JMV** (`Ctrl+F5`).
2. Confirm the game bypasses the ordinary intro/main-menu route and loads the
   temporary map with its test player.
3. Exit normally and send `workflow.log`. The retained LocalAppData test-run
   folder also contains `runtime.log`, `bridge-result.log`, and `manifest.json`.
4. Confirm temporary `ed*.mpr`/`ed*.mob`, the temporary ASI, and the private
   marker are removed/restored.

If the game still reaches the main menu, the bridge result distinguishes an ASI
load failure, runtime fingerprint refusal, missing transition marker, and an
accepted internal launch request. Do not manually enter `server` in the console;
that command path is already disproven.
