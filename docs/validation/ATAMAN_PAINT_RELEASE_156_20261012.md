# EI-PAINT-RELEASE-156 — 2026-10-12

## Cause and scope

The owner confirmed that strokes appeared while dragging and disappeared on mouse release in installed155. Its workflow log records Ground/Common 1999753121, applied stamps and changed cells followed by refused=1 on every stroke.

`paintWorldTerrainAt` deliberately delegates a single-map Mixed-edge stroke back to the local path. `resolveMixedEdgeTerrain` then incorrectly refused that stroke whenever any neighbor was selected, even when the brush never touched that neighbor. Production automatically selects neighbors; the previous155 isolated-view tests omitted that context.

156 removes this obsolete global selection guard. Multi-map routing remains in the existing world painting implementation. Actual resolution errors retain rollback and now include the detailed report in workflow.log. No HD/seam candidate changes are integrated.

## Evidence

Private copied zone1 MPR and HDPack_Terrain.res; source game resources read only. Hidden native Windows Qt/OpenGL context, production neighbor setter, native mouse press/move/release, GPU capture, exact sector Undo/Redo, in-memory clipboard and isolated backups.

- `build-tests-155-matrix/fixture-release156/neighbor-before`: baseline fails at mouse release with `terrain.mixed_edge.single_map`; preview is rolled back. The diagnostic fixture initially flattened its single XY array; this reproduced at the map corner. XY input validation was subsequently added.
- `neighbor-after`: actual XY points55,40 and80,65, shared brush and distant selected neighbor;34 PASS/0 FAIL, drag retained after release, GPU changed, other layer preserved, exact Undo/Redo.
- `all-modes-neighbors`: all material/surface/sculpt modes, held C, animation preservation and save/reopen. Initial diagnostic run used the local stamp counter for world strokes; that counter is not incremented by world painting. The regression now compares terrain snapshots at successive native mouse events instead.

## Additional defect caught before installation

The corrected per-event snapshot test on clean156 produced321 PASS/2 FAIL: held-C drags changed only the first stamp in both layers. The world preview uploads called `adoptTileImages`, which cleared the legacy picker selection. Subsequent native move events reported “Выберите материал или тайл.” The original local stamp counter could not detect this.

`previewWorldPaintState` now retains the selected physical slot and rotation through the upload and refreshes palettes through the existing gesture-safe deferred path. Scoped source commit `27eef6c6a5579a01e2f50575574fda92eb824c6c` includes this repair on top of `352289d6ffbd47bb633a53d1bdd536c8c2d46526`. Unrelated dirty seam changes in the same file were excluded by a scoped index patch.

Clean version156 was initially built with the release wrapper including runtime components. The two exact committed files of the follow-up (world preview and changelog) were applied to the same fresh156 snapshot and the changed object relinked. Final EXE SHA256 `B339FF9ED66BE9E18C8E76DFFEE561499AEE4C09DFD07BBE4D9C5314A59CB2C1`.

## Final native matrix

`build-tests-155-matrix/fixture-release156/all-modes-final156/stdout.log`: **323 PASS / 0 FAIL**, process exit0. Actual snapshots change at multiple drag centers in Surface delete/add, Lava, Swamp, Water, held-C Overlay, Ground, held-C Ground and Underwater. All12 sculpt tools, preferences roundtrip, exact Undo/Redo, other-layer preservation, original animation frames, ordinary Save, fresh sectors/semantic identities, frame-order/codec checks and a fresh post-reopen stroke passed. GPU renders passed with changed images. Clipboard nativeAttempts=0; external clipboard metadata changes were not caused by this test.

Snapshot compared with the final committed Git archive:1928 tracked files,0 content mismatches,2 line-ending-only differences from extracting the follow-up files. Receipt `build-tests-156-release/source-identity-final.txt`. The earlier raw git-show comparison reported line endings as mismatches; this comparison resolves that diagnostic.

Staged executable equals the tested SHA above and reports version156. UI-widget-check passed, exit0 (`fixture-release156/ui-final`), native clipboard isolation active. Package finalization and guarded installation passed as recorded below. No interactive editor or game acceptance is claimed.


## Verified handoff

Finalizer/internal distribution validator passed:136 prior /137 current,0 removed,0 stale files. Exactly one current changelist; no game data. Finalized manifest SHA256 `D42C260D8B55D65335E4C75DE56928AF140F288FBA17FC82D6A17B0AD68CFEEE`. Finalized staging was not run afterwards.

Guarded installation155→156 completed in `artifacts/EI-Mapper-Ataman-2-first-test`:137 managed files verified,6 replaced,1 added,0 removed. All23273 unlisted owner files retained exactly; before/after digest `2B17387BA018BB1294DE4DA812133C7FB2F4A6FB6C85FE2117656F5147AAA38F`. Host CIM checked mapper/runtime closure before backup and immediately before changes. Backups verified before replacement; retained rollback155 is `artifacts/rollback155-EI-PAINT-RELEASE-156-20261012`. Install receipt: `build-tests-156-release/install-receipt.txt`. Previous own rollback154 removed only after exact path, no-link, file-set and hash guards; foreign backups retained.

Source branch main, runtime source `27eef6c6a5579a01e2f50575574fda92eb824c6c`. Subsequent report commit changes documentation only. The painting fix and automated release handoff are complete; owner screen/game acceptance remains distinct. HD ALL256 and visual cross-map seams remain open under EI-RECOVERY-20261012.
