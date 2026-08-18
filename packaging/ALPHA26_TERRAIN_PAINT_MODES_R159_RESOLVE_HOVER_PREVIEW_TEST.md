# EI Mapper Endurance 0.3.0-alpha.26 terrain paint modes R159 resolve/hover/preview test

Focused follow-up for the R158 terrain paint modes build.

## Changes

- Resolve Missing Blends now judges each cell from the other cells incident to its four shared vertices. A wrong tile can no longer make its own required repair appear ambiguous.
- The repair pass can replace an incorrect decoded non-uniform blend with the unanimous neighbouring signature, while continuing to preserve contextual stamps when their neighbours imply a uniform tile.
- The Modern Tile-target live preview now paints only the outlined core tiles. The surrounding blend ring remains unchanged until release resolution, so the visible preview is no longer one tile larger than the selected footprint.
- Terrain hover picking refreshes on every mouse-move event. Slowly crossing a tile boundary no longer leaves the outline on the old tile while a click targets the new one.

## Validation

- `terrain_virtual_atlas_tests`: 103 passed, 0 failed.
- `endurance_document_tests`: 107 passed, 0 failed, 6 expected fixture/export skips.
- `--ui-widget-check`: explicit PASS with the Qt minimal headless platform.
- Read-only `zone2resolve4.mpr` audit: 60 repairable cells found with either available Zone 2 sidecar profile (58 wrong blends and 2 missing blends); 2 contextual stamp-to-uniform cases remain protected.
- `git diff --check`: pass.

## Focused manual checks

- Open `zone2resolve4.mpr` with its matching Zone 2 atlas resource and run **Resolve missing blends**. It should create one undoable repair action instead of reporting that no cell needs repair. Existing contextual stamps should remain.
- In Modern + Tile target, compare brush sizes 1/2/3. During the drag preview, only the yellow outlined core tiles should receive the uniform preview; exact boundary blends should appear on release.
- Move the pointer very slowly across a tile boundary, then click without another large motion. The painted centre must match the currently displayed outline.
- Classic Auto vertex preview remains intentionally unchanged in this revision pending the UX decision described in the handoff.
