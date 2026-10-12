# HD and cross-map native review — 2026-10-12

Status: IN PROGRESS. Source base main `df281e66f7575bf99b4b1e0121ebcab7c9723bc4`, installed156. Candidate changes are not installed or accepted yet. Work is performed personally under EI-HD-SEAMS-VISUAL-157-158; no delegated worker or live game-data write.

## Build identity and recovery

The first incremental diagnostic executable mixed stale objects because copied source retained old timestamps. It is excluded from acceptance. Its hidden-widget captures were also clipped; they are not visual evidence. A fresh shadow build `Ataman156-paint-release-clean/build-release-158-candidate` includes all current candidate sources and a dedicated scene FBO. The capture follows the ordinary scene renderer, including neighboring maps; it does not use the minimap renderer.

The owner deleted the shared Qt/MinGW while that fresh build was compiling, then explicitly authorized recovery or downloading. No matching toolchain was found in the Windows recycle metadata. aqtinstall3.3.0 was downloaded from its GitHub release; SHA256 `4F74D4C95C464D238D7E17EC2D9B7F22A7C333F0F5270A62584E2B47FC765150` matches release metadata. Qt5.12.12 win64_mingw73 and MinGW7.3 were restored to the previous Endurance `.tooling/Qt` path from the Qt mirror at mirrors.dotsrc.org, with published SHA-1 archive validation. Other deleted Endurance files were not restored. qmake reports5.12.12/win32-g++, g++ reports7.3.0. Fresh candidate build completed exit0; log `build-tests-158-fresh-candidate-restored.log`. The diagnostic still identifies as156; it is not a release package.

## Cross-map correction and evidence

The world boundary compositor retains the resolved mixed body, uses both surface coordinates and the SEC renderer's triangle interpolation, and fades a narrow common-source edge into that body. A further renderer mismatch was found: CTile displays UV[1/16,15/16], so an outer12px band on256px tiles was outside the visible image. The constraint now includes the visible edge's bilinear footprint and fades inward. Geometry is unchanged. CPU tests174 PASS/0 FAIL/0 SKIP (`build-tests-158-seam/cpu.log`).

Native input copies: `build-tests-158-seam/fixture`, zone4 and zone6, provenance in its `input-hashes.csv`. Native view is1024x1024, world center(960,206),16x16 world units, top-down, normal scene path, both maps loaded. Ground is enabled; water, grid and brush preview are disabled. This tests a mixed material stamp crossing the actual world boundary, with four mixed semantic cells on each map.

`build-tests-158-seam/stroke-native/stdout.log`: exit0,57 PASS/0 FAIL; real brush transaction, exact Undo/Redo, joint MPR/RES save, embedded semantic-field binding and reopened border equality passed. Clipboard isolation records nativeAttempts0. The changed clipboard sequence is external metadata activity and is not claimed unchanged.

The before/after native viewport PNGs were visually opened. The stamp crosses the middle map boundary without a visible straight cut in this view and feathers into surrounding grass. Undo PNG SHA256 equals the before PNG (`5EF680635C7C08366AF73C2BB823310E7D5E95BC9908F25CDE7329002EDDFA9E`); Redo equals after (`F43F7D3CAFBDD308C5025A08E09F156B23D1370D51E25FA2CAF9EAE8B1136D46`).

`build-tests-158-seam/reopen-native-fresh`: separate process, exit0, loaded the saved copies and captured the same camera before any edit. Its native PNG was visually opened: the material transition survives reopening. This does not claim other map placements or game acceptance; the repair command and final release regression remain to be completed.

## HD publication

Native publication on zone19 copies ran in `build-tests-157-hd/unchecked-native`; baseline hashes and times are recorded in `build-tests-157-hd/native-baseline.json`. Four real slots34/44/215/666 have normal-render before/after captures, all visually opened. The published image contains finer cracks/paving detail while retaining material regions. Slots34/44 are at the original map edge; black lower areas are outside the map and are not a clipped framebuffer. The cold index has37440 work slots, followed by960 target slots and861 transition candidates. Publication and baseline adoption succeeded, with the layout option initially unchecked and no layout edit added.

This first run is not a PASS: a combined animation-image/range assertion failed; later drawing after in-process reload crashed in nvoglv64.dll, exit0xc0000005. The new HD harness omitted makeCurrent between unload/load, unlike the established paint harness. That omission was corrected. Animation checks now log each native frame's size, original QImage format and RGBA SHA256, and separately compare ranges. A small Qt5.12 reproduction proves identical RGBA8888 and ARGB32 QImages compare unequal with raw operator==, but equal after canonical RGBA conversion. The original palette usesRGBA8888; the MMP decoder returnsARGB32. Real-frame hashes must still confirm this diagnosis. `unchecked-diagnosed` is the repeat with restored original private inputs and explicit context acquisition. The first run's crash and raw assertion are retained as failed diagnostic evidence, not relabeled PASS.

The source-index cache was saved (47,852,619 bytes). Its full SHA-bound header and stream structure validated. An initial fixture reset incorrectly reparsed a PowerShell JSON DateTime through localized text, changing the test RES timestamp and correctly invalidating the cache. Subsequent fixture restoration uses numeric UTC ticks; this was a test setup error, not a relaxation of source-identity guards.

Remaining: native unchecked/checked publication, review actual HD detail and transitions, animation equality, layout Undo/Redo/Save/reopen; native repair command; scoped157/158 source commits, clean release regression and guarded permanent installation.

## Ordinary Save correction and bounded follow-up

`unchecked-diagnosed`: all32 animation frames are128px with exact canonical RGBA hashes; after/reopened PNGs match in all four cameras. Raw QImage-format mismatch explains the old29/1 result; it is not relabeled a clean test exit. `checked-native` passed publication, native frames, layout Undo/Redo and seed, then exposed a real ordinary-Save defect:32 native128 frames were enlarged to256 despite unchanged ranges. Independent RES/MPR decoding confirms it.

Finalization now reserves native animation pages at their original size and copies the original compressed payload/mips. Unassigned transparent page padding is explicitly physicalPadding, excluded from material assignment and working source remaps; incomplete logical identities still fail validation. Owner clarified that water must remain128 because water rendering is procedural. Rendering of water itself is unchanged.

Follow-up uses the already validated HD publication backup (RES8c172a8806945236f3ee9c978255863a928a8d8fed500ae5c06a6f2429dfe00a), shuffles variants, then exercises normal Save and fresh load. It does not repeat indexing/republication. First implementation exposed the pre-existing all-slots-must-be-logical assumption on transparent slot32; test failures are retained in save-native-fixed and save-native-padding-fixed. Unit: mixed-size static finalization and preview-tail preservation passed; explicit native animation/padding scenario passed after correcting its fixture catalog to leave padding unassigned.

Repair command native before/after images were visually inspected. Real brush and repair retain a gradual mixed patch across the map boundary in this camera. Initial repair run passed transaction, exact Undo/Redo, repeated idempotence and save; reopened semantic checks lacked restoreVirtualMaterialFieldState, unlike the brush harness. This test-only omission is corrected using the bound embedded semantic payload.

`save-native-final`: exit0,26 PASS/0 FAIL; variant layout Undo/Redo and exact seed, ordinary Save, fresh reopen, all32 canonical RGBA frames and animation ranges remain exact128px. Four native after/reopened views captured; reopened junction view visually opened with retained HD detail and material regions. This closes the real Save defect. Native source-page payload/mips are copied verbatim, padding is unassigned, and the completeness check counts logical slots separately. Pure UI follow-up hides padding from the newly generated material preview.
