# Alpha 26 terrain paint modes R151 beta UX test

This revision completes the first beta interaction pass over Modern, Classic
Auto, and Manual terrain painting. It retains the reviewed R148 batch-one
semantic catalog (`ee5d1139`, preset SHA-256
`1D980F2DD24ADE17DD2AD3A0F37E80C0885840FB550CDC937B45FF6B6223EA89`).

## Changes

- Classic Auto now lays out in multiple columns on its first visible frame;
  opening Modern first is no longer required.
- `Z`, `X`, and `C` switch directly to Modern, Classic Auto, and Manual while
  terrain painting is active. Ctrl shortcuts retain their normal behavior.
- Tile and Vertex targeting are explicit. Tile is the default and paints
  complete cell ownership; Vertex retains shared-corner detail painting.
- Classic Auto Safe is enabled by default. It applies every exact authored
  connection available in the current atlas and skips only unsupported edge
  cells instead of inserting an incorrect uniform tile or refusing the whole
  stroke. The prior bounded hard-edge behavior remains available by disabling
  Safe.
- Classic Auto hides corner-only contextual stamps while Tile targeting is
  selected. They remain available for Vertex work.
- Brush shape and target selection now have a high-contrast checked state.
- Holding right mouse and moving continuously samples the nearest material
  vertex in Modern/Classic Auto. Manual samples the exact physical slot and
  packed rotation. Existing tile information tooltips remain available.
- **Resolve missing blends...** replaces the redundant atlas tile picker. It
  scans Ground and Overlay, realizes missing Modern exact tuples, repairs
  inconsistent physical cells, and records the MPR repair as one undo step.
- Grayscale heightmap import now lives with Sculpt Terrain instead of beneath
  the painting controls.

## Focused manual test

1. Open Zone2, enter terrain paint, and select Classic Auto without opening
   Modern first. Confirm the brush grid has multiple columns and does not
   overlap brush controls.
2. Press `Z`, `X`, and `C`. Confirm they select Modern, Classic Auto, and
   Manual. Confirm Ctrl+Z/C/X continue to perform normal shortcuts.
3. In Classic Auto Safe, paint across a boundary for which some authored
   connections exist and one does not. Confirm valid cells paint, the missing
   edge remains unchanged, no uniform tile appears in that edge, and the atlas
   does not grow. Undo/redo must be exact.
4. Compare Tile and Vertex targeting. Tile size 1 should preview and paint one
   complete cell; Vertex size 1 should edit the nearest shared vertex and its
   incident cells. Confirm corner-only Classic stamps are hidden only in Tile.
5. Hold right mouse and move across several materials. Confirm the selected
   Modern/Classic brush follows the nearest vertex. In Manual, confirm both
   physical slot and quarter-turn follow the tile under the cursor.
6. Deliberately create or retain a wrong/missing blend, then run **Resolve
   missing blends...**. Confirm the progress dialog appears, the repair is one
   undo entry, undo restores the exact prior packed arrangement, and redo
   restores the repair.
7. Open Sculpt Terrain and confirm heightmap import and vertical flip are
   present there and absent from Terrain Paint.

## Known limitations

- Classic Auto Safe cannot invent artwork. Unsupported connections stay
  unchanged; use Modern or the map-wide resolver when the atlas may grow.
- Undoing **Resolve missing blends** restores all MPR cells exactly. Any newly
  realized working-atlas slots remain as unused cache entries, matching normal
  Modern stroke behavior, and are removed by final compaction when unused.
- Automatic semantic inference for unknown third-party atlas identities remains
  out of scope. Manual physical-slot painting remains available.
- Rotation-equivalent physical deduplication remains disabled by decision.
