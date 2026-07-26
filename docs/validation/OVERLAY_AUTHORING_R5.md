# Overlay coverage authoring R5 validation

This increment separates overlay coverage from overlay texture painting and
removes two unsafe assumptions from new-surface initialization. It operates on
the canonical SEC edit model and retains the staged MPR save contract.

## Changes

- **Add / remove overlay surfaces** is a distinct Landscape Tool. **Paint
  existing overlays** now only changes atlas tiles on active cells.
- Add-surface picking prefers an existing overlay under the cursor. Its outline
  is blue and projected to that surface; where no overlay exists it uses the
  terrain and the terrain-colored outline. Overlay outline edges fall back to
  terrain projection outside thin/disconnected coverage instead of vanishing.
- **Atlas texture material** selects the visible atlas family. **Surface
  material (tint / animation)** selects the existing MPR material record and
  displays its kind, RGB, opacity, wave multiplier, and warp speed.
- Newly activated cells initialize from current land plus minimal clearance.
  They preserve genuinely active neighboring seams but never resurrect hidden
  vertex data from a removed/source overlay cell. Undo still restores the exact
  previous SEC bytes.
- Activation at raw height 65535 clamps only the new overlay vertex to maxZ and
  reports possible coplanar overlap. It does not fail the stroke or rescale the
  MPR height range.

## Automated evidence

- `endurance_document_tests`: 73 passed, 0 failed, 3 expected private-fixture
  skips.
- Synthetic regressions cover dormant overlay geometry, repeat material-record
  assignment, maxZ clamping, unchanged undo/redo, staged reopen, and untouched
  archive members.
- Qt 5.12.12 / MinGW 7.3 full release compile: pass.
- `--ui-widget-check`: pass, including the separate surface-tool state.
- `git diff --check`: pass.

## Manual checks

Use only copied maps or an isolated mod workspace.

1. Select **Add / remove overlay surfaces → Add surface**. Over empty land the
   outline must remain terrain-aligned; over an existing overlay it must move to
   that surface and turn blue.
2. Add radius-1 and radius-2 surfaces on visually flat land. The first click
   must create the complete footprint without spikes. Repeating the same click
   with the same atlas and surface materials must be a no-op.
3. Remove a patch and add it again. It must initialize from current land, not
   restore its former sculpted/hidden shape. Undo and redo must remain exact.
4. Change only **Surface material (tint / animation)** and add over an existing
   active cell. The stored material choice must update independently of the
   atlas texture; visible differences depend on the authored material records.
5. At high terrain that reaches MPR maxZ, adding must succeed with a clamp
   warning rather than refusing the stroke. Inspect for coplanar overlap, then
   lower the terrain or increase maxZ if clearance is required.
6. Confirm **Paint existing overlays** cannot add/remove coverage and still
   supports automatic and manual atlas-tile painting.

