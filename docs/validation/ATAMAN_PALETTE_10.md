# EI-PALETTE-010 — first paint activation (2026-09-17)

Base: `737b74d8094ae4202612ce817772b1ae3db113af`, delivered version 9.
Branch: `codex/paint-palette-layout-v10`. Version 10.

## Reproduction and cause

The supplied 31.10-second recording shows Sculpt -> Paint leaving material
buttons stretched one per row. Switching Classic -> Modern restores the grid.
Qt gives the hidden palette viewport placeholder geometry; the old code only
recalculated on outer-panel resize or palette-mode changes. Showing the paint
controls/layer does not necessarily resize the outer panel.

`paintPaletteUsesVisibleWidthOnFirstActivation` starts in Sculpt, loads a
synthetic catalog, shows the tools, then activates Paint without changing tabs.
All four rows (Modern/Classic x Ground/Overlay) fail on the old implementation:
`build-tests-10-widgets/before.txt` — 2 PASS, 4 FAIL (init/cleanup included).

## Fix and regression

Watch Show/Resize on all four palette viewports and coalesce recalculation onto
the event queue. Ignore hidden viewport geometry. Retain existing selection and
filter through rebuilding; clear old column stretches when the grid narrows.
No changes to terrain painting, resource selection, save or game data.

`build-tests-10-widgets/after.txt`: 21 PASS, 0 FAIL, Qt 5.12.12 offscreen.
Includes the four original cases, resize narrower/wider, tab round trip,
selection/filter retention, batch catalog updates, deferred thumbnails,
manual-slot identity and mode/layer controls. GPU tests were not selected for
this UI-layout-only change. User settings isolated in QTemporaryDir.

## Delivery and remaining acceptance

Source: `0ec4420f128b3a7fb511610d4f3e5f4b4e4e9e3a`.
Fresh shadow `build-release-10-palette`, Qt 5.12.12/MinGW 7.3 x64 editor and x86
runtime components: PASS (`build-palette10.log`). Endurance toolchain read-only.
Ready-executable version, database creation, resource navigation and scenario
checks passed (`build-release-10-palette/checks.json`); scenarios 47/47 PASS.
General `--ui-widget-check` again timed out after 120 seconds; the runner stopped
only its own process. This inherited limitation is not PASS. The original
palette reproduction and focused UI regressions above pass independently.

Fresh owned `build-staging-10-palette`: windeployqt, isolated-PATH version smoke
and finalizer PASS. Exactly one current changelist; no historical alpha notes.
All 100 manifest entries/hashes and exact staged file coverage verified;
executable/runtime match fresh build. Permanent closed installation updated:
`artifacts/EI-Mapper-Ataman-2-first-test`. Six program files replaced, none
deleted; 1051 unlisted user files preserved byte-for-byte by hash.
Installed manifest verified. Executable SHA-256 (build/stage/installation):
`F2B0375C651EB1E1F30AD48FA9F36D31C4C634BA1B7394682BD60812E28EEA5D`.
Version-9 replaced files kept in `build-rollback-palette10`; older version-8
`build-rollback-save9` positively identified, hash-verified and removed only
after successful installation verification.

Do not treat synthetic offscreen checks as manual acceptance on the user's real
map. Manual check: open landscape tools
in Sculpt, enable Paint, inspect Ground and Overlay without toggling Classic,
then resize the dock and confirm selection remains. Parent brush/save acceptance
tasks remain open. No installed game resources are changed by this work.
