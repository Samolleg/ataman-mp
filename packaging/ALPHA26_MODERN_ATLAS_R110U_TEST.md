# Modern terrain atlas R110U tester changelist

This follow-up adds exhaustive uniform-brush artwork review to the unchanged
R110 painter, semantic catalog, masks, and generated material collections.

## Changed

- Two new uniform artwork collections cover the reviewed physical resolutions:
  256px artwork in a Zone2 environment and 128px artwork in a Zone17
  environment.
- Every distinct exact artwork receives a 2x2 card showing packed rotations
  0, 1, 2, and 3. A one-cell gutter separates cards.
- Safe deduplication uses semantic material identity, runtime terrain type, and
  exact decoded RGBA SHA-256. Byte-identical namespace/slot aliases remain in
  the JSON and CSV provenance indexes.
- The coverage validator independently reconstructs all reviewed uniform
  sources from the semantic catalog and fails if a source is omitted,
  displayed twice across resolutions, assigned the wrong physical slot, or
  missing a rotation.
- Lower-resolution artwork is never resampled into the 256px map. Each source
  appears only in the map matching its authored physical dimensions.

## Uniform inventory counts

- Catalog-authorized source aliases: 1,011 / 1,011 displayed.
- Distinct exact artworks: 317 total (266 at 256px; 51 at 128px).
- Packed review instances: 1,268 (four rotations per artwork).
- 256px map: 80x96 cells, 778 active atlas slots, 13 texture pages.
- 128px map: 192x192 cells, 307 active atlas slots, 5 texture pages.

The 256px and 128px manifests each list the other resolution as explicitly
incompatible. Their combined coverage gate requires every catalog source to be
displayed exactly once at its native resolution.

## Existing R110 collections retained

- Final material stripes: 81 materials, 617 active slots, zero unresolved.
- Every-material radius-3 circles: 81 materials, 754 active slots, zero
  unresolved.
- Zone comparisons: Zone 1, 2, 5_1, 11, 16, 17, and 18; zero unresolved.

Preset SHA-256:
`EF9A8A92F1B9A6DA6B0E1C3B7CCA1DE90C1039F99056DDF88336C11ACDF80960`.

## Focused manual checks

1. Open `collections/uniform-brush-inventory-256px.collection`. Inspect every
   2x2 card for an incorrect family, decorative/false-uniform artwork, or
   objectionable rotation.
2. Open `collections/uniform-brush-inventory-128px.collection` and perform the
   same review for the lower-resolution authored atlases.
3. Use the adjacent `uniform-artwork-index.csv` or mark bad cells in the editor
   so reports retain the canonical namespace/slot aliases and pixel hash.
4. Continue using stripes and circles for transition-mask, edge, and junction
   review. The inventory maps intentionally show exact uniform artwork only.

## Review boundary

This follow-up does not change painter selection, blend masks, semantic family
assignments, or runtime terrain types. It exposes the complete current uniform
authority for human review. Installed stock Jamais Vu assets appear only in
these isolated test maps; EI-HD research assets are not included.
