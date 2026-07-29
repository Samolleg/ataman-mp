# Expanded terrain-atlas shipped-runtime validation

This gate tests whether the shipped Jamais Vu runtime can load a run-owned clone
of installed Zone18 whose terrain atlas is supplied by an isolated RES. It never
replaces a shared atlas. Installed JMV imagery is allowed; separate EI-HD imagery
is not copied into the editor or probe package.

## Ordered capacity gate

The custom-RES path was first proven at 512, 576, and 1,024 slots. The capacity
campaign now starts at 1,024 slots and advances only after each pass:

1. 16 pages / 1,024 slots;
2. 32 pages / 2,048 slots;
3. 64 pages / 4,096 slots;
4. 128 pages / 8,192 slots;
5. 256 pages / 16,384 slots, the packed MPR maximum.

`scripts/run_jmv_expanded_atlas_runtime.ps1` enforces this order. It preserves
and retries one unrelated startup/capture failure per size; a deterministic
failure still stops the campaign. A process exit, main-menu load,
`GameplayReady`, or one successful RES lookup is not a pass by itself.

## Staging and workload contract

Each run clones the exact installed Zone18 MPR+MOB under a random `ed........`
identity. The launcher rebases the MPR's MP/SEC namespaces and every terrain MMP
member to that identity, reopens them, and refuses inconsistent page/slot/member
counts. The RES is registered only inside the owned game process.

The bridge redirects the exact installed `2utoexec.cfg` path to a run-owned copy
containing `drawall 1` before the JMV module initializes. Redirect and successful
read markers must carry the expected owned-file hash. The installed command file,
source maps, Starter selection, installed archives, and saves remain unchanged.

A camera view normally requests only visible terrain pages. For a deterministic
capacity workload the bridge explicitly requests and verifies every run-owned
page after resource registration. This makes memory evidence include the complete
atlas preload instead of pretending one visible page proves all pages. Normal
terrain rendering still has to produce a credible frame after Zone18 loads.

## Required pass evidence

Every final probe must establish:

- exact staged MPR/MOB hashes reached the loader;
- the run-owned RES registered and every expected page resolved;
- the hash-verified run-owned `drawall 1` startup file was consumed;
- Zone18 reached `GameplayReady`, remained responsive for the dwell, and wrote no
  new native dump;
- a restored, owned game window produced a non-black/non-cyan terrain frame;
- working set, private/pagefile bytes, and committed/reserved/free/mapped/image/
  private 32-bit VA were sampled through load, ready, dwell, and capture;
- largest-free-region pressure and proxy-log deltas were retained;
- authoritative page/slot/source/cleanup contracts passed and no owned process or
  temporary live file remained.

Memory evidence is updated during loading, so a crash or external hard timeout
still leaves a useful last sample. A timestamped campaign folder contains every
attempt's Qt report, memory JSON, and proxy-log delta. `campaign-summary.json`
contains all attempts, final probes, adjacent observed bytes-per-added-slot, and
regression estimates. Numeric samples use `GetProcessMemoryInfo` plus a complete
4-GiB `VirtualQueryEx` walk.

## Invocation

Build `tests/modern_atlas_real_map_builder.pro`, then create only the five Zone18
fixtures:

```powershell
modern_atlas_real_map_builder.exe `
  artifacts\modern-atlas-runtime-probes-zone18-v1 `
  "D:\Evil Islands\Starter\MODS\JamaisVu135" `
  --runtime-only
```

The fixture root also contains one portable `.collection` per expanded MPR.
Each collection references its generated MPR relatively and resolves
`zone18.mob` through the active Jamais Vu Maps root, so the stock environment
MOB is used without copying it into the package.

Build `tests/jmv_map_test_launcher_tests.pro`, the x86 runtime host, and the x86
bridge; put the latter two below the test executable's `runtime` directory. Close
unrelated game/test processes and run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_jmv_expanded_atlas_runtime.ps1 `
  -TestExecutable build-test-expanded-atlas-runtime-vtable\release\jmv_map_test_launcher_tests.exe `
  -GameRoot "D:\Evil Islands" `
  -ModRoot "D:\Evil Islands\Starter\MODS\JamaisVu135" `
  -FixtureRoot artifacts\modern-atlas-runtime-probes-zone18-v1\runtime-atlas-probes `
  -QtBin <Qt-bin>
```

Qt reports are first written to an owned ASCII `%TEMP%` path because Qt 5.12 is
unreliable below a non-ASCII worktree. They are then copied into the campaign.
On hard timeout the wrapper invokes owned-run recovery before classifying it.

## Current proof state

The 2026-07-28/29 capacity results are not valid runtime passes. The shared
launcher accepted a stale `GameplayReady` record after `game.exe` displayed a
Microsoft Visual C++ Runtime Error / abnormal-termination dialog. Its frame
predicate measured only color variation, so title artwork and the crash dialog
were misclassified as rendered terrain. Cleanup then requested Stop and masked
the failure status in the authoritative result.

The user-observed 2026-07-29 rerun established the actual result for the two
largest probes:

| Pages / slots | Corrected classification |
|---:|---|
| 128 / 8,192 | **Fail:** native runtime crash during load; no player-on-map confirmation |
| 256 / 16,384 | **Fail:** native runtime crash during load; no player-on-map confirmation |

The earlier 512, 576, 1,024, 2,048, and 4,096 observations used the same
defective pass predicate and must be rerun under the corrected harness before
they are cited as automated capacity proof. Their retained memory samples are
diagnostic data only; they cannot estimate bytes per valid atlas tile until the
corresponding run passes.

The corrected shared launcher monitors the owned game process for native fatal
dialogs, records the evidence in the run manifest/result, automatically stops
the owned run, and rejects the result even if historical bridge output contains
`GameplayReady`. This applies to both the standalone campaign and Editor
**Test Current Map**. A nonuniform frame remains supporting evidence only and
must never override a native failure or substitute for confirmed staged play.

The corrected 2026-07-29 HD campaign regenerated every fixture from the exact
installed Zone18 MPR/MOB and produced the following authoritative boundary:

| Pages / slots | Corrected result | Peak private bytes | Peak committed VA | Minimum largest free region |
|---:|---|---:|---:|---:|
| 16 / 1,024 | **Pass:** player on staged map, rendered terrain, dwell and cleanup | 931,086,336 | 1,815,441,408 | 2,144,096,256 |
| 32 / 2,048 | **Pass:** player on staged map, rendered terrain, dwell and cleanup | 1,223,593,984 | 2,026,336,256 | 1,945,108,480 |
| 64 / 4,096 | **Pass:** player on staged map, rendered terrain, dwell and cleanup | 1,238,818,816 | 2,041,536,512 | 1,920,008,192 |
| 128 / 8,192 | **Fail:** CRT abnormal termination during load; no authoritative player-on-map result | 2,559,827,968 | 3,399,507,968 | 335,355,904 |

The 8,192-slot probe's nominal combined RGBA surface is 553,648,128 bytes, but
the observed private-memory growth above the last passing run is approximately
1.32 GiB and committed 32-bit address space reaches approximately 3.17 GiB.
The corrected classifier therefore identifies page/load-copy amplification and
address-space pressure rather than accepting the stale readiness line. The
ordered campaign stops at this confirmed endpoint and does not attempt 16,384.
Evidence is preserved in `artifacts/modern-atlas-runtime-evidence-r23`.
