# Modern terrain atlas R120 all-zone tester changelist

This build refreshes every shipped terrain test collection from the fully
resolved R120 semantic catalog and fixes mixed transition artwork leaking into
the material-palette brush icons.

## Changed

- Synced the exact R120 catalog and replay authority through R117, recovered
  R119, consolidated R119, and R120. The preset SHA-256 is
  `CFC41CEC99F70A96FDA09B52DF576EF150B29E67A6689F65A34C6605CAD3E5C2`.
- Palette icons now accept a preferred catalog thumbnail only when that
  physical tile is genuinely uniform for the selected family at all four
  corners. Contextual one-on-three thumbnails fall back to an audited uniform
  artwork instead of appearing as a misleading mixed brush icon.
- Rebuilt all zone comparisons, the full-family stamp map, final material
  stripes, radius-3 circles, and both native-resolution uniform-brush
  inventories from the current catalog and mask resources.
- Exact authored signature/rotation reuse remains ahead of generated blends.
  A requested blend is never resolved by substituting a uniform tile.

## Automated fixture counts

- Reviewed land brushes: 82.
- Full-family stamp: 539 active slots, 383 generated exact tiles, zero
  unresolved cells.
- Final material stripes: 608 active slots, 383 generated exact tiles, 189
  global authored blends reused, zero unresolved cells.
- Every-material radius-3 circles: 748 active slots, 539 generated exact
  tiles, 168 global authored blends reused, zero unresolved cells.
- Zone comparisons: Zones 1, 2, 5_1, 11, 16, 17, and 18; every manifest has
  zero unresolved cells.
- Uniform inventory: 984/984 authorized source aliases, collapsed to 305 exact
  artworks and shown in all four packed rotations. The 256px map contains 261
  artworks/1,044 rotations; the 128px map contains 44 artworks/176 rotations.

## Focused manual checks

1. Inspect both `uniform-brush-inventory` collections. Every card should be a
   genuinely uniform artwork for its named brush; its four cells show packed
   rotations 0, 1, 2, and 3.
2. Inspect `final-material-stripes.collection` and
   `all-material-circles.collection` for wrong families, incorrect authored
   blends, generated-mask defects, or unexpected uniform placements at edges.
3. Check the palette icons while opening several zone collections. An icon
   must show a clean uniform material, never a mixed transition stamp.
4. Use **Mark bad tiles on map** for any visual problem so the report retains
   physical pixel hashes and source/generated provenance.

## Review boundary

Zero unresolved cells proves exact semantic corner resolution, not visual
approval. Rotation-equivalent physical deduplication remains deliberately
disabled; only byte-exact duplicates with matching semantic and runtime-type
identity are eligible for remapping. Installed stock Jamais Vu assets appear
only in these isolated test maps. EI-HD artwork remains research-only and is
not included.
