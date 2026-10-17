# Ataman199 — EI-MIXED-PAINT-199 — 2026-10-17

## Problem and cause

Owner screenshot: ordinary vertex painting after HD conversion on DejaVu zone3obr fails with `terrain.authored_library_pixels` (zone9 slot65). Installed197 logs show HD conversion of12705 cells followed by refused strokes. WorkingAtlas normalizes its resolver to the maximum native tile size (256); painting/prewarm/material hydration incorrectly used the first native tile (128). The transition importer correctly rejected the incompatible pixels.

## Change

Expose the actual resolver synthesis size and use it for transition imports, material sources, prewarm and deferred hydration. Reload cached materials of the wrong size. Initial hydration uses maximum native size. Native stored128/256 pages stay mixed; HD rim/DXT preservation, water and existing transitions are not rewritten by this fix. Accepted198 performance changes remain included. No132–176 transition kernel restored.

## Evidence

Private work directory: `build-mixed-paint-199-20261017` (not distributed).
- Minimal negative reproduction: `build-paint-mixed-audit-20261017/repro.txt`,128px source rejected /256px accepted.
- Actual native197 negative: `baseline.out` and `baseline-run/logs/workflow.log`; QTest mouse click fails to change terrain, exact `terrain.authored_library_pixels` reproduced (zone9 slot486).
- Unit regression: `unit-results.txt`,7 passed /0 failed /0 skipped. Mixed native storage, resolver canvas, authored transition import, ordinary save padding/multiple pages.
- Fresh Release199 and both runtime components: `final-release.log`; based on accepted198 code9daeecbf8cbe51039cc9225b198216d52be12bec plus scoped199 changes.
- Native positive run PASS:20 checks, exit0; `final3.out`. Hidden real CView, actual mouse clicks, vertex target, automatic transitions, two Grass materials, Undo/Redo, Save/reopen on private copy only. Not a substitute for owner visual/game acceptance.

Earlier prototype Save comparison falsely depended on sector enumeration order. Diagnostic comparison established0 changed tile references/0 pixel differences. Test now compares by stable sector coordinates. An earlier run stopped after Save/UndoRedo without a terminal result; it is not counted as a completed pass. Test no longer retains unnecessary diagnostic copies of the full atlas across reopening. Run final2 exited with access violation during reload; the harness omitted makeCurrent after unloadLand (which calls doneCurrent). Final3 restores the GL context around reload, matching the existing HD integration harness.

## Private fixture and preservation

Recovery source pair in owner backup folder `f0d5ce006828673d2b134144039306a2beffe3f00aaf007d4b8d5a894e1c0abb`:
- `20261016-193855-307-autosave-42af8a798aea3928-zone3obr.mpr`: SHA256 `0a5dfea9d66171e1e42a859c7c9e5d6e33737ab28cae92e4604b5af107befe34`.
- `20261016-193855-307-autosave-42af8a798aea3928-zone3obr-atlas.res`: SHA256 `89885f7dd40d1bd6038518701a2cc858fbc02cc5733c33ef963ae31d8a95845e`.

169 native pages/10816 slots, mixed128/256. Test writes only `fixture/Maps/zone3obr.mpr` and `fixture/res/HDPack_Terrain.res`. No live map/resource writes. Distribution contains no private fixture.

## Handoff

Native final3 completed successfully, including exact reopened terrain, second material mouse stroke, its Undo/Redo and second Save. Scoped implementation committed and guarded installation completed over198. Permanent target: `artifacts/EI-Mapper-Ataman-2-first-test`. Preserve every unmanaged owner file and verify managed manifest; keep verified198 rollback. No GitHub publication.

## Verified installation

- main source `de55f5e4c4f984a8c22e1e6908bd04c275f30c96`; 652 compiler/resource inputs match the built snapshot (line-ending-only differences allowed).
- Raw/staged version199, finalizer PASS; 130 managed files verified. Owner digest unchanged: `17898:97BAF43E6EB2AAE46753828050B600E288C5193C2DC5F5162E4C4D85642C27BD`.
- EXE SHA256 `CED102421B026319B2DEAEBCB7B963DC53851FBFF69A6674B9ABB66205C4E0CB`; manifest `C78FEA83EB0CEB76F069E40D4D56137884C7F6BD09FDBA3CB2CC97D6E1F7A08B`.
- Verified reconstructable198 rollback: `artifacts/rollback198-to199-mixed-paint-20261017`; only previous verified197 rollback retired.
- Receipts: private `build-mixed-paint-199-20261017/{build-receipt,staging-receipt,install-receipt,rollback-retirement}.json`.
- Installed program updated; owner visual/game acceptance remains separate.
