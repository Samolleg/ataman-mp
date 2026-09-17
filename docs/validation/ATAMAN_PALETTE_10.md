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

Release/build delivery pending. Do not treat synthetic offscreen checks as
manual acceptance on the user's real map. Manual check: open landscape tools
in Sculpt, enable Paint, inspect Ground and Overlay without toggling Classic,
then resize the dock and confirm selection remains. Parent brush/save acceptance
tasks remain open. No installed game resources are changed by this work.
