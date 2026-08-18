# Terrain paint modes R157: draft RES, repair, safe stamps, and outlines

This revision fixes the issues reported against the R155/R156 terrain-paint
mode test packages.

## Changes

- **Save editable modern draft as...** now creates the required map-local atlas
  `.res` whenever the document has a working modern atlas, even if the current
  paint only reuses or repacks existing atlas pages.
- **Resolve Missing Blends** no longer skips decoded-but-wrong cells in recovered
  finalized maps. It may now replace a decoded uniform or stale tile when the
  shared-corner field requires an exact authored/generated blend.
- Classic Auto Safe contextual stamps now reject unrelated support material
  instead of stamping nearly anywhere, while still allowing compatible vertical
  stamp connections when the atlas contains the needed edge blend.
- Manual painting is always tile-based. The Tile/Vertex target switch is
  disabled in Manual mode.
- Modern and Classic Auto tile-target hover outlines now use the same dry-run
  affected-cell set as the paint operation, so the displayed tile footprint
  matches the cells that will actually change after blend resolution.

## Focused manual checks

1. Open the reported Zone2 draft scenario and use **Save editable modern draft
   as...**. Confirm a sidecar `.res` is created beside the draft MPR.
2. Reopen that draft and run **Resolve Missing Blends**. It should repair
   visible missing blend cells instead of reporting that no unambiguous cell
   needed repair. Undo should revert the repair as one action.
3. In Classic Auto Safe / Vertex, try a contextual stamp on unrelated support:
   it should refuse. Then connect compatible contextual stamps vertically where
   the atlas has the matching edge blend.
4. Switch to Manual. Confirm Tile/Vertex target controls are disabled and the
   manual brush remains tile-based.
5. In Modern or Classic Auto with Tile target, hover with a multi-tile brush.
   The yellow tile outline should match the cells changed by the stroke,
   including any boundary cells added by blend resolution.
