# EI-INTERACTIVE-PERFORMANCE-159 — 2026-10-12

Request: remove stalls during painting, saving and camera movement, preserving HD, native water and cross-map editing. Base main `91092152cc4ff6e29866808f92c67a6ab1241d15`; installed program158 `14e763d7133804a98bbed016fe4ada468ddc016f`. Personal implementation; live maps/resources were not edited.

## Diagnosis and changes

Owner158 log: `build-tests-159-performance/owner-158-baseline.log`. Normal draws were often inexpensive, but long GUI gaps remained. Brush release on a private zone1 copy reproduced 5.97s and 5.57s blocks. Mixed-edge classification requested full pixel identities repeatedly across the atlas. The new bounded per-thread QImage-key digest/opacity caches retain no image buffers and invalidate naturally on QImage mutation. A mutation regression confirms detached artwork gets a different digest while the original remains unchanged.

The current panel exposes Modern/Manual and `classicAutoPaintingEnabled()` returns false. Deriving its invisible Classic bank and requesting visible grid rebuilds on every stroke/thumbnail delivery was redundant; this pass is skipped. The visible palette keeps its ordinary material and usage updates.

GPU preview remembers the implicitly shared images it actually uploaded, compares image keys and uploads changed slots only for a stable layout; resized layouts still use the full path. Rollback images and mip generation remain. The picker retains unchanged icons and delegates. No texture resolution, shadows, water rendering or quality option was disabled by the optimization.

Ordinary Save previously built/reopened the entire RES candidate in CView and again in the save coordinator. The coordinator now owns that work once; the multi-file cohort keeps its separate candidate path. Exact profile payload validation, untouched members, fresh staged reopen, backup, race checks, atomic replacement and post-commit validation remain. Immediate catalog adoption consumes its freshly verified binding once. Only a newly isolated namespace skips the unrelated full resource/neighbor visual reload; explicit shared-identity replacement still refreshes visuals.

## Bounded measurements and regression

- `build-tests-155-matrix/fixture-release156/perf159-before`: native baseline158 with timing instrumentation, 36 passed/0 failed. Two mouse releases 5966538us and 5569579us; move events roughly3–21ms.
- `.../perf159-after`: candidate, 36/0; releases253807us/197488us. A 40-frame middle-button camera probe gave median14893us, p95 15182us, max16670us including GPU completion. That probe changed the camera angle before the strokes: these numbers establish removal of the multisecond block but the final same-angle run is still pending below. Camera fixture contains terrain and selected neighbor context, not the owner's complete MOB scene.
- `build-tests-157-hd/perf159-save-before` and `perf159-save-after`: 26/0 each. MPR and RES restored from `build-tests-159-performance/save-fixture` before the second run. RES1396031312 bytes. Publication entered/succeeded timestamps: 23:39:54→23:41:17 (83s), 23:43:54→23:44:46 (52s). Variant-layout seeds differ and the first run overlapped compilation; these are indicative single-run times, not an isolated statistical throughput benchmark. Source/adoption mechanisms and removed duplicate work are directly verified in code/logs. The archive remains costly to save because it is rewritten and backed up as a whole.
- Fresh reopen retains HD terrain/layout, native128 animation pixels/ranges. Viewed `perf159-after/release-1999753121-55-40-after.png` and `perf159-save-after/reopened-2-viewport.png` through the actual native image outputs; no texture substitution/blank output observed.
- `build-tests-159-performance/unit-results.txt`:6 passed/0 failed/0 skipped (4 selected cases plus lifecycle): image-key mutation, imported/generated identities, world fallback, exact compressed animation page preservation. First redirected unit output was empty; explicit QtTest file output supplies the actual receipt.

## Final build and delivery

Final program source: main `8af2c3144dd145f2ba01ce4a437ef428bc085fa2` (implementation `96db02cdf521670d33c29da0f787af3bb9aec12b` plus rollback guard). Fresh snapshot `C:/Users/samol/AppData/Local/Temp/Ataman159-performance-final-b`, build `build-release-159-clean/release/EIMapperEndurance.exe`. EXE SHA256 `279BE1D25A9DA789AC8AC7FFB8AD4716726831D2424C1DF52FB1549DE0A31D6F`. The earlier clean snapshot preceded the rollback fix and is not the deliverable.

- Final same-angle zone1 run `build-tests-155-matrix/fixture-release156/perf159-final-same-angle`:36/0. Identical 41/54 changed-tile strokes to baseline. Release224309us/16397us against5966538us/5569579us; movement6196–16389us. First press193303us still includes the first atlas expansion; subsequent press10139us. No claim of zero first-use cost.
- Final `ui159-final`:exit0, widget interaction/lifecycle PASS.
- Final `build-tests-158-seam/stroke159-final`:57 PASS, no FAIL, exit0. Real two-map native stroke, exact Undo/Redo, shared Save, reopened border pixels and untouched neighboring MOB. Viewed `stroke-after-viewport.png`; the changed upload path retains terrain and the continuous seam.
- Final `build-tests-157-hd/perf159-save-rollback-final`:23/0, exit0. Test-owned Windows read handle denies replacing only the private MPR. RES publishes, MPR atomic replacement fails, verified candidate SHA permits exact RES rollback. Both original MPR bytes and original RES SHA are preserved. The inherited dialog conservatively reports MPR manual recovery although that file remained unchanged; deferred message issue, not an observed data-loss result. This test validates the rollback fix rather than repeating the happy-path save.
- Clipboard isolation: nativeAttempts0. No live map/resource writes; no interactive editor launched.

Staging `build-staging-159-performance-final`, version159 confirmed. Finalizer PASS:139 managed paths,0 removed, removed only stage-generated workflow.log. Manifest SHA256 `310A93789677EF941C7E1A47B267BE36B743559B1778F11FE36C04915FB5AF00`. The finalized staging directory was not launched again or edited.

Installed on2026-10-13 after the host process check found the owner editor closed: `artifacts/EI-Mapper-Ataman-2-first-test`,158→159.139 managed files,6 replaced/1 added/0 removed; installed managed hashes match staging.23824 unlisted owner files preserved, before/after digest `4AC4B6F31767759BB0C3A65B8C9CBA04770223D11B95F8805109966ACF777DE3`. Receipt `build-tests-159-release/install-receipt.txt`. Retained verified rollback158: `artifacts/rollback158-EI-PERFORMANCE-159-20261012`. Removed only the identified own rollback156 (7 files), preserving foreign older rollback folders; receipt `build-tests-159-release/rollback-cleanup-receipt.txt`.

Program implementation and delivery complete. Full owner MOB-scene interaction and gameplay are not claimed as covered by these bounded tests. Whole-archive Save remains noninstantaneous; this change removes redundant work while retaining backup and integrity checks.
