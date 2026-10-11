# EI-MOB-LOAD-148 validation

Date: 2026-10-12. Base: `main` `c2f4d6d43b6388363727844435f6df84eb6a7e1b`, installed VERSION 147. Scope: keep the opened MPR scene, MOB records, and available painting palette unchanged while removing paint-only catalog construction from the blocking map-open path; restore the full 146 package payload and reject unexplained package removals.

## Findings

The install logs place long paint-material work during MPR's deferred editor initialization, before MOB finishes: v147 gz1g reports `sourceHydrationMs:2545` and `materialCatalogMs` roughly 2.5 s; v145 gz7g reports `sourceHydrationMs:7222` and `materialCatalogMs:7866`. In the v147 gz1g slow sample, MOB's `End update objects` reports 4677 ms while its measured `otherMs` is 712 ms; another parallel-build sample reports MOB 14968 ms and workspace total 20846 ms. The byte-read/projection path is therefore not established as the source of those multi-second MOB figures: event-loop/UI work is included while the workspace load is still pumping events. The primary confirmed cause is paint-only catalog/source hydration competing with active map opening. Commits `72d75d0f` and `ff8b66bc` moved palette construction after the first landscape frame but kept the expensive catalog build synchronous on the GUI thread; `a779e305` added HD-variant catalog work. Ataman 147 commit `42d87a68` did not change this load path.

Version 148 holds deferred paint catalog hydration for the duration of a complete map workspace load, including the first landscape frame, and starts it only when a paint action explicitly needs it. The loaded terrain and palette data are still available on demand. MOB logging now separates file read, structural validation, backup, legacy projection, provenance, object update, clean projection, assembly snapshot, and activation.

## Measurements

The valid repeated 148 sequence used the normal Maps-browser `openMapPair` route, five stages per map, identical map copies, and the configured installed model profile. `Cold` is stage 1; `warm median` is the median of stages 2–5. Counts matched the source map each time.

| Map | Objects / units | v148 cold MOB ms | v148 warm MOB median ms | v148 workspace total, warm stage ms |
|---|---:|---:|---:|---:|
| gz1g | 1699 / 116 | 1068 | 477.5 | 1960 |
| gz9g | 2080 / 196 | 1385 | 656.5 | 2121.5 |
| gz7g | 3327 / 221 | 1278 | 856.5 | 1556 |
| bz1g | 495 / 51 | 482 | 195.5 | 997.5 |

The table uses the final five-stage sequence. Warm medians average the two middle values from stages 2–5. Exact cold/warm-median MOB values are gz1g 1068/477.5, gz9g 1385/656.5, gz7g 1278/856.5, bz1g 482/195.5 ms. Workspace totals are full map-open durations, not the separately forced catalog hydration total.

The v148 detailed gz1g warm MOB phases were: file read 0 ms, structural validation 42–46 ms, backup 5 ms, projection 102–106 ms, provenance 64–66 ms, `End update objects` 92–93 ms, post-object work through the read timer about 334–346 ms, and activation/outliner about 127–131 ms. The 148 warm MOB median is below the v126 installed gz1g point of 1162 ms by about 59%, with the same 1699 objects and 116 units.

Paint-only work remains intact after opening. An explicit post-open catalog request on gz1g logged `materialCatalogMs:1987`, `sourceHydrationMs:954`, and classic palette 803 ms; the request's separate deferred total was 5039 ms. Equivalent post-open hydration completed on gz9g (1628 ms catalog), gz7g (1415 ms), and bz1g (1536 ms). Those durations no longer delay workspace-open completion.

Historical install observations supplied with the task: gz1g v126 1162 ms; v137 5813 ms; v146 about 5000 ms, with a 14968 ms run during a parallel build; gz9g v126 about 4100–4300 ms, v128 6027 ms, v138 10289 ms; gz7g v145 5879 ms with 7866 ms catalog; v147 also produced gz1g 4953–5301 ms and gz7g 5879 ms. These are heterogeneous single install-log observations, not a five-run controlled matrix.
Available historical observations by version (single retained install-log points; `—` means no comparable value was supplied):

| Version | gz1g MOB ms | gz9g MOB ms | gz7g MOB ms | bz1g MOB ms |
|---:|---:|---:|---:|---:|
| 126 | 1162 | 4100–4300 | — | — |
| 128 | — | 6027 | — | — |
| 131 | — | — | — | — |
| 137 | 5813 | — | — | — |
| 138 | — | 10289 | — | — |
| 145 | — | — | 5879 | — |
| 146 | about 5000; 14968 during parallel build | — | — | — |
| 147 | 4953–5813 across retained logs | — | 5879 | 1858 |
| 148 | 1068 cold / 477.5 warm median | 1385 / 656.5 | 1278 / 856.5 | 482 / 195.5 |


**Historical matrix limitation:** controlled five-run replays for v126/128/131/137/146/147 were not completed. Their `QStandardPaths` routes the script workspaces to the Windows user profile even when `APPDATA` and `LOCALAPPDATA` are overridden; pre-v138 releases also lack the isolated-backup override. One v126 control was stopped after logs exposed that routing. The v126 number is excluded from the controlled matrix. Four matching script-workspace hashes and many timestamped backup directories were observed in user AppData; baseline ownership/content could not be established, so those paths were left untouched. No further historical EXE map loads were started. Historical attribution above is from retained install logs and source history, not a completed binary bisection.

## Scene identity and UI responsiveness

Input map copies remained hash-stable across the v148 sequence. Each stage loaded the same per-map object and unit counts shown above. This confirms byte-identical inputs and count identity; a full MOB save-and-byte-roundtrip equivalence check was not completed. Palette data was deferred rather than removed and is still explicitly loadable. Model-gallery responsiveness evidence: v147 receipt open 36 ms / select 338 ms; v148 receipt open 11 ms / select 3 ms (497 models, 7165 textures). These are separate runs, not a paired same-process measurement.

## Distribution regression

The v147 staging manifest loses exactly 30 paths relative to the 146 final manifest: seven tracked `heightmaps/*` examples and 23 `icons_color/*` files. The heightmaps are read by the New MPR import UI as example heightmaps. The 23 color icons were present in historical staging/releases (including v105/v116/v146 and rollback146), absent from Git before this fix, and have identical SHA-256 payloads across those packages. Current source references `icons_fluent`; the retained `icons_color` payload has no active source reference and is preserved as historical package composition, not asserted to be the active theme.

Version 148 tracks those 23 archived assets under `packaging/assets/icons_color`, copies them into fresh staging, requires all 23 and all seven heightmap files in the validator, and compares the current SHA manifest against a previous finalized staging manifest. Every removed path is listed; unexplained removals fail validation. Final release validation reported zero removed paths against v146.

## Checks run

- `--ui-widget-check`: PASS, 169 checks, clipboard `nativeAttempts=0`.
- `--scenario-layer-check`: PASS, 49 checks.
- `--dark-theme-check`: PASS, 70 checks.
- `--model-gallery-check`: PASS, 497 models / 7165 textures.
- Terrain random 136: PASS, 80 checks, 0 failures / 0 skips.
- Mixed-edge 139: PASS, 67 checks, 0 failures / 0 skips.
- HD variants: PASS; 326 manifest entries/generated variants, 96 groups, 68 inputs, 6 pages, 0 omissions; 1873 rejected candidates and 14 shortfalls are reported by the fixture.
- v147 vs v146 staging comparison detects the 30-path regression. Full v148 git-archive staging/manifest comparison and install readback are separate release handoff checks.
- `git diff --check`: PASS.

## Release and installation handoff

- Implementation source was archived and built from commit `32b1a511e56a5c6c1a9795f83b67826fbb3e8b25`; archive extract/build work is under `build-tests-148-mob-load/source-archive-32b1a511`. Staged `--version` reported `EI Mapper Endurance Edition 148` before finalization.
- Fresh package staging: `build-staging-148-from-archive`. Finalizer PASS: 127 v146 manifest paths, 129 current paths, zero removed paths; zero stale/runtime files removed except one generated `logs/workflow.log`. Staged and installed executable SHA-256: `47DDFCB42E8CDD586B8E288531E2D12A7426F038B786C6B10B61C9A3C13ABCD6`.
- Installed version 148 into `artifacts/EI-Mapper-Ataman-2-first-test`. Readback: all 129 manifest entries match; 6 managed files replaced, 32 paths added, 0 removed; 22,827 non-manifest owner files remained byte-identical. Restored seven heightmaps and 23 color icons. Rollback and receipt: `artifacts/rollback147-to148-EI-MOB-LOAD-148-20261012-retry`.
- The first guarded install attempt detected a metadata classification error in the install script and rolled back successfully; the corrected run passed complete readback. The initial and retry rollback folders were retained.
- No owner EXE was running at the pre-install checks. No game, mod, or live game resource was written. The required historical controlled matrix, exact MOB roundtrip, and manual/game UI acceptance remain open as listed above.
