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

Pending: fresh Release159, same-angle paint measurement, changed-atlas world seam/UI checks, guarded permanent-install update. Owner mapper remains open; it must not be terminated by the agent. Completion of the functional158 task is not a blanket performance acceptance.
