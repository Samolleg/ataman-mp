# Expanded terrain-atlas shipped-runtime validation

This gate tests whether the shipped Jamais Vu runtime can load a disposable
map whose terrain atlas is supplied by a run-owned RES. It does not install or
replace a shared atlas. Installed JMV imagery may be used to author the probe;
separate EI-HD imagery is not copied into the editor or probe package.

## Required order

1. Run the 8-page, 512-slot custom-RES control.
2. Proceed to 9 pages / 576 slots only after the control has rendered terrain.
3. Proceed to 16 pages / 1024 slots only after the 9-page probe passes.

`scripts/run_jmv_expanded_atlas_runtime.ps1` enforces the 512 control first.
Use `-ControlOnly` while diagnosing the control. A process exit, main-menu
load, `GameplayReady`, or one successful RES lookup is not a pass by itself.

## Staging contract

Each run creates a random `ed........` map identity. The launcher rebases all
of the following to that same identity before writing any live temporary file:

- the MPR's MP member;
- every SEC member;
- every terrain MMP member in the run-owned RES (`ed........000.mmp`, etc.);
- the private run-local map-table entry.

The shipped terrain loader derives its page namespace from the staged MPR
filename. Renaming only the outer MPR while retaining the finalized atlas
namespace loads terrain geometry without textures. The launcher reopens the
staged MPR and refuses the run unless its inner namespace, page count, slot
count, and RES page count agree.

The RES is registered only inside the owned game process. Installed archives,
source maps, Starter configuration, and shared JMV map namespaces remain
byte-identical. A journal records every temporary live path and hash before it
is created.

## Pass evidence

Every probe must establish all of the following:

- exact staged MPR/MOB hashes reached the loader;
- the run-owned RES registered;
- every expected page was requested by the terrain renderer and resolved under
  the random runtime namespace;
- the synthetic map reached `GameplayReady` and remained responsive for the
  configured dwell period;
- no new native dump appeared;
- `runtime-frame.png` was captured from the owned game window;
- the frame is not dominated by one quantized clear color and has sufficient
  color diversity to represent rendered terrain;
- owned-process working set, private bytes, and pagefile usage were sampled
  across loading, `GameplayReady`, dwell, and capture;
- the authoritative result validates page/slot counts and cleanup;
- no owned process or temporary live file remains.

The frame check explicitly rejects the flat cyan world produced by an MPR/MMP
namespace mismatch as well as a black/empty capture. The numeric threshold is
also covered by synthetic regression images; the first healthy shipped-runtime
frame must be retained for threshold calibration and human review.

## Invocation

Build `tests/modern_atlas_real_map_builder.pro` and generate the disposable
fixtures from the installed JMV root before invoking the gate:

```powershell
modern_atlas_real_map_builder.exe `
  artifacts\modern-atlas-runtime-probes-v6 `
  "D:\Evil Islands\Starter\MODS\JamaisVu135"
```

The generator writes the reviewable full-family/zone maps as well as the
8-, 9-, and 16-page runtime probes. Generated MPR/RES payloads contain installed
JMV imagery and remain local artifacts; source control carries the deterministic
builder and manifest schema, not copied game assets.

Build `tests/jmv_map_test_launcher_tests.pro` and place the current runtime host
and bridge under the test executable's `runtime` directory. Ensure Starter has
persisted the same JMV `AddonPath` as `-ModRoot`, close unrelated game/test
processes, then run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_jmv_expanded_atlas_runtime.ps1 `
  -TestExecutable build-test-expanded-atlas-runtime\release\jmv_map_test_launcher_tests.exe `
  -GameRoot "D:\Evil Islands" `
  -ModRoot "D:\Evil Islands\Starter\MODS\JamaisVu135" `
  -FixtureRoot artifacts\modern-atlas-runtime-probes-v6\runtime-atlas-probes `
  -QtBin <Qt-bin>
```

Qt reports use a unique ASCII `%TEMP%` path because Qt 5.12 cannot reliably
create command-line log files beneath a non-ASCII worktree. The wrapper deletes
that exact path first and refuses missing, stale, failed, timed-out, or malformed
reports. On timeout it invokes owned-run recovery before returning failure.
Each passing probe also preserves `expanded-atlas-PP-memory-latest.json` beside
the test executable. The complete matrix writes
`expanded-atlas-memory-comparison-latest.json`, which is the stable comparison
artifact for later page-count stress tests. The installed proxy log currently
records memory-pressure configuration but not numeric process-memory samples;
the comparison artifact therefore identifies its source explicitly as the
owned Windows `GetProcessMemoryInfo` samples.

## Current proof state

The original 512 run that reached `GameplayReady` is invalidated: its retained
human screenshot showed an untextured cyan world. Byte comparison against stock
JMV terrain pages found that the generated DXT1 pages stopped at 4 by 4 (10
mips) while shipped 2048-square pages carry a complete 12-level chain through
1 by 1 and set the compressed descriptor dword to 4. The incomplete OpenGL
texture was the first incorrect value after resource resolution. The encoder
now reproduces that shipped page contract and has a focused regression.

On 2026-07-29 the ordered shipped-runtime gate passed with installed JMV imagery:

| Run | Pages / slots | Page requests | Frame evidence | Result |
|---|---:|---:|---:|---|
| `20260728-203110-47a5ff5a` | 8 / 512 | 8 / 8 | 1,339 buckets; 0.0596 dominant ratio | Pass |
| `20260728-203133-81f28652` | 9 / 576 | 9 / 9 | 1,351 buckets; 0.0623 dominant ratio | Pass |
| `20260728-203156-a507a470` | 16 / 1,024 | 16 / 16 | 1,440 buckets; 0.0545 dominant ratio | Pass |

The same ordered run produced this initial process-memory baseline (MiB):

| Pages / slots | Samples | GameplayReady WS / private | Peak WS / private | Final WS / private |
|---|---:|---:|---:|---:|
| 8 / 512 | 54 | 171.5 / 353.5 | 305.3 / 527.8 | 304.2 / 525.9 |
| 9 / 576 | 54 | 171.6 / 353.7 | 304.8 / 525.3 | 304.5 / 524.9 |
| 16 / 1,024 | 55 | 170.5 / 352.8 | 321.9 / 550.2 | 320.8 / 550.2 |

All three maps reached `GameplayReady`, remained responsive for the dwell,
captured visibly textured terrain, produced no new dump, preserved their input
hashes, and completed cleanup. No owned process or temporary RES/MPR/MOB was
left behind. This validates the shipped runtime through 16 pages / 1,024 slots;
it does not by itself validate every theoretical MPR page value through 256.
