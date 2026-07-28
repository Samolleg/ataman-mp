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

The earlier 512/576/1,024 gate remains the historical lower-bound control. On
2026-07-29 campaign `expanded-atlas-zone18-stress-20260728-213659` reached the
packed maximum with installed Zone18 imagery and MOB content:

| Run | Pages / slots | Page resolutions | Frame buckets / dominant | Peak WS / private MiB | Peak committed VA / largest-free MiB | Result |
|---|---:|---:|---:|---:|---:|---|
| `20260728-213659-b0ff4af9` | 16 / 1,024 | 16 / 16 | 1,651 / 0.1032 | 619.0 / 886.3 | 1,682.3 / 2,044.8 | Pass |
| `20260728-213754-9862ebcc` | 32 / 2,048 | 32 / 32 | 1,745 / 0.1028 | 629.3 / 896.1 | 1,692.1 / 2,044.7 | Pass |
| `20260728-213849-09016bff` | 64 / 4,096 | 64 / 64 | 1,624 / 0.1053 | 645.7 / 913.2 | 1,708.8 / 2,044.7 | Pass |
| `20260728-213927-63c3418c` | 128 / 8,192 | 128 / 128 | 5,972 / 0.0513 | 2,081.3 / 2,457.3 | 3,212.4 / 303.5 | Pass |
| `20260728-214008-2273689a` | 256 / 16,384 | 256 / 256 | 5,972 / 0.0513 | 673.3 / 1,046.5 | 1,804.3 / 1,862.9 | Pass |

The `GameplayReady` private-byte regression is about 11,085 bytes (10.8 KiB)
per slot. Pre-pressure adjacent peak deltas are roughly 9–11.5 KiB per slot.
Peak residency is not linear: 8,192 slots approached the configured pressure
threshold, while 16,384 slots completed after cache eviction with much lower
residency. Per-slot estimates are therefore planning figures, not a promise that
every decoded page stays resident.

All five final runs proved staging, drawall, complete page resolution, rendered
terrain, dwell survival, source preservation, no new dump, and cleanup. This
validates the shipped JMV packed terrain-page field through 256 pages / 16,384
slots for this controlled Zone18 workload. It does not remove the need for
Endurance editor-renderer paging work or broader save/reopen/map coverage.
