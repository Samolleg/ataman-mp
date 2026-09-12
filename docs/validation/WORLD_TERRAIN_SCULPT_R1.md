# EI-WORLD-SCULPT-001 — world terrain sculpt r1

Implementation branch: `contrib/Samolleg/world-terrain-sculpt`.
Review: [draft PR #7](https://github.com/Ilufus/ei-mapper-endurance/pull/7).
Baseline: `ffdc28ea9f749896ddffdd583e2a6a5e44d5201a`.
The complete feature remains awaiting interactive acceptance; it is not closed
merely because the implementation and focused tests are ready.

## Behavior

- Landscape Tools exposes explicit neighboring-map selection and the switch
  «Изменять рельеф соседних карт». It starts off; each neighbor must be checked.
  Available map names and paths, retained modified files and history controls
  are visible. Save reviews the full set of modified MPRs.
- A stamp uses the saved world origins and actual packed vertex XY offsets.
  Raise/Lower/Flatten/Smooth share one world position/radius and world-height
  calculation. Existing raw step/flatten controls are converted using the
  active MPR scale for the whole stamp. Noise stays local and is explicitly
  refused while world mode is enabled.
- The owner approved independent editing for misaligned grids and overlaps,
  averaging unequal coincident boundary heights inside the brush, and bounded
  format rounding on save. This does not move maps or stitch outside a stroke.
- Smoothing reads the immutable input of the whole stamp, deduplicates shared
  boundary samples and includes both sides of coincident enabled boundaries.
  Every file rounds to its own uint16 scale; the difference between a pair is
  bounded by half the sum of their height quanta. Existing ranges are retained.
- All document changes are prevalidated on copies before any model is changed.
  One mouse drag is one world undo/redo entry, retained independently of the
  active map's ordinary history. Ctrl+Z/Redo use world history while the world
  Sculpt tool is selected; dedicated world buttons also remain available.
- Background render lifetime does not own editable documents. Map-browser and
  neighbor navigation retain dirty models and world history. Closing/reloading
  or leaving terrain sculpt for other editing asks Save all / Discard / Cancel.
  Start from saved local edits; pending parameter/atlas publication cannot be
  silently hidden by switching maps. Normal texture/object behavior is retained.
- Saving builds validated candidates, checks original identities, locks targets,
  writes and verifies backups, then writes a recovery marker beside each target
  before replacing any MPR. Failure attempts a complete rollback. An interrupted
  save is detected on reopening; explicit recovery checks all current and backup
  identities before restoring. A third-party change refuses recovery rather
  than overwriting it. This protocol is recoverable, not multi-file atomicity.

## Automated evidence

`tests/world_terrain_sculpt_tests.pro`: **9 passed, 0 failed** (Qt 5.12.12/MinGW).
Cases cover the original single-model seam failure, all four world modes, four-map
corners, swapped target enumeration, protected and half-offset neighbors,
cross-edge smoothing, rejected command provenance without partial mutation,
retained document identities, actual stroke save/reopen followed by undo/redo,
second-target injected write failure and rollback, invalid/missing input,
interrupted recovery discovery, and recovery refusal after an external change.
All fixtures are synthetic; installed game data was read only for investigation.

`tests/landscape_tools_widget_tests.pro`: **16 passed, 0 failed, 1 skipped**.
The skipped existing framebuffer test requires a real OpenGL context, unavailable
with the offscreen platform. New controls start protected, expose separate
signals, reflect undo/redo/save availability and render without clipping at the
checked 380-pixel width. A rendered widget image was inspected locally.

The portable Qt kit requires explicit `QT_QPA_PLATFORM_PLUGIN_PATH` for test
executables outside its bin directory. Omitting it caused startup stalls or
`0xC0000602` before tests ran. With it, focused widgets passed; this does not fix
the separate full-app UI check.

Release wrapper built the editor and both x86 runtime components. The two runtime
scripts now accept `EI_VS_DEV_CMD` and find the installed BuildTools fallback;
their old hardcoded Community path prevented the required release handoff.

**Full `--ui-widget-check` remains failing with `0xC0000005`**, confirmed using
an explicitly waited offscreen process and the correct plugin directory. This
was reported at baseline and is not claimed fixed. The focused tests above do
not waive that release gate.

## Manual acceptance still required

The permanent unpacked r1 tester folder passed `finalize_package.ps1` and
`validate_distribution.ps1`; all copied files matched the staged hashes. It
contains rebuilt runtime components, Qt/compiler libraries, notices, one current
changelist and checksums. No ZIP, user settings, logs or installed game files.

Use user-owned test copies/profile paths. This task did not copy installed maps
for editing and never launched the packaged editor interactively.

1. Open an adjoining pair in its saved layout, enable world sculpt and check the
   intended neighbor. Raise/Lower/Flatten/Smooth while dragging across the edge;
   inspect both terrains and the world brush outline.
2. Undo/redo once; change active map through the map browser/double-click and
   repeat history. Toggle background visibility and verify retained changes.
3. Uncheck a neighbor and verify it remains unchanged. Inspect the approved
   independent-grid behavior on fractional offsets and overlapping maps.
4. Save all; inspect the file list, reopen all modified MPRs and compare the edge.
   Close with Cancel and Discard on disposable maps. Confirm first-run packaging
   and responsiveness on the representative installed corpus.

## Uncertainty audit

No unresolved owner-policy choices remain. Native viewport interaction,
whole-window lifecycle and large-map performance have not been manually verified.
Fault tests inject deterministic failures; they do not simulate every possible
power-loss/filesystem condition. The full UI-check crash and unavailable offscreen
OpenGL are explicit evidence gaps. Named EI skills were unavailable; no private
engine source was inspected or included. The earlier geometry report is limited
to the located saved Gipat component, not every profile/layout.
