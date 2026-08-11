# Modern terrain atlas R123 tester changelist

This build applies the reviewed R123 Lava mask routes, the current semantic
catalog, and finalization fixes needed to preserve and explain the exact edited
map that is written.

## Changed

- Synced semantic checkpoint `a910c24c771111e7b9ad150725558e7a17f8ba8e`
  with preset SHA-256
  `1B8493A9CC562603D57B4BDE97F069534B94AFE9A85D1F901C0AF7941B1E6D7A`.
- BaseGipat now exposes separate `Water / Still` and
  `Water / Still dark` overlay brushes. Zone18 `Sand / Dark`,
  `Stone / Orange`, and `Stone / Weathered` are land-only.
- Lava no longer falls through to obsolete geometric masks. The explicitly
  approved routes are liquid-flow for outer 1-of-4, and fracture-flow for
  edge 2-of-4, diagonal 2-of-4, and inside 3-of-4.
- Finalization now stages every current landscape edit, including sculpted
  heights and the latest paint stroke, before writing the isolated MPR.
- Finalization restores working-atlas provenance after compaction, so the
  preview and manifest distinguish retained original artwork, imported stock
  artwork, and generated blends. Source-slot aliases are retained for
  byte-identical authored candidates.
- The completion dialog now states that the finalized MPR has not yet been
  loaded and offers explicit **Open finalized MPR now** and
  **Keep original editing session open** choices.
- Palette thumbnails remain restricted to audited four-corner uniform artwork;
  current family and layer assignments are loaded from the R123 catalog.

## Automated fixture counts

- Reviewed land brushes: 82.
- Full-family stamp: 539 active slots, 383 generated exact tiles, zero
  unresolved cells.
- Final material stripes: 608 active slots, 383 generated exact tiles, zero
  unresolved cells.
- Every-material radius-3 circles: 748 active slots, 539 generated exact
  tiles, zero unresolved cells.
- Zone comparisons: Zones 1, 2, 5_1, 11, 16, 17, and 18; every manifest has
  zero unresolved cells.
- Uniform inventory: 984/984 authorized source aliases, collapsed to 305 exact
  artworks and displayed in all four packed rotations.

## Focused manual checks

1. On a BaseGipat map, confirm `Water / Still` and `Water / Still dark` are
   separate overlay cards and visibly retain their distinct hues.
2. Paint Lava boundaries covering outer, edge, diagonal, and inside
   topologies. They should use the reviewed organic flow/fracture masks and
   never the old mathematical geometry.
3. On Zone18, confirm `Sand / Dark`, `Stone / Orange`, and
   `Stone / Weathered` appear on Land and not Overlay.
4. Inspect both uniform inventories and the current palette icons. Every brush
   card should be a genuinely uniform artwork for its named family.
5. Make an unsaved sculpt and paint change, finalize, and verify the preview
   lists non-original tiles where applicable. Choose **Open finalized MPR
   now** and confirm the current edits remain present. Repeat with **Keep
   original editing session open** to confirm the choice is unambiguous.

## Review boundary

The automated collections exercise land brushes. Lava and the two BaseGipat
water hues are overlay checks and therefore require the focused manual test
above. Rotation-equivalent physical deduplication remains deliberately
disabled. Installed stock Jamais Vu assets appear only in these isolated test
maps; EI-HD research artwork is not included.
