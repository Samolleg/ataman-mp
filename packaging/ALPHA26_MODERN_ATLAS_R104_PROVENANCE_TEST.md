# Modern terrain atlas R104 tester changelist

This build makes every terrain paint issue mark identify the exact live artwork
that was visible when it was reported. It retains the R103 overlapping-preview
fix and the R102 semantic catalog and approved masks.

## Changed

- Land-tile issue reports now record a live physical pixel SHA-256 instead of
  treating a recyclable working-atlas slot number as provenance.
- Each marked tile records whether its artwork is from the current map, an
  imported exact authored blend, or a canonical generated blend.
- Imported artwork records its source namespace and source slot. Generated
  artwork records its canonical signature, mask provenance, runtime terrain
  type, and every exact authored uniform source used to compose it.
- Each generated input records its material, source namespace/slot, atlas
  identity, and physical pixel SHA-256.
- The diagnostic issue-review builder now follows the same current-map-first
  source ordering as the live painter, so its source examples cannot silently
  come from a different atlas binding.
- No semantic family assignment, approved mask, resolver preference, or blend
  compositor behavior changed in R104. The preset SHA-256 remains
  `0411D38DE59C255D36448203F4522939B4D43191AD07E45EDA882291AA09519B`.

## R102 report reconciliation

- The 13 reported non-uniform signatures had no exact authored transition
  candidate and were generated blends or junctions.
- Their 16 authored uniform inputs were all genuine 256 x 256 approved-HD
  assets with no resampling or missing-page fallback.
- Fifteen inputs were confirmed against the current semantic catalog. The sole
  pre-existing review item is `bz13h` slot 11 (`Sand / Yellow`); the R102 report
  produced no new provenance-qualified wrong-family tile.
- The reported three- and four-family failures use nine individually approved
  mask topologies. They remain multi-family compositor/mask evidence, not proof
  that an authored uniform tile replaced a required blend.

## Included visual collections

- Final material stripes: 80 materials, 623 active physical slots, 383
  generated exact blends, 203 imported stock blends, 7,680 packed cells, zero
  unresolved cells.
- Every-material radius-3 circles: 80 materials, 762 active physical slots,
  543 generated exact blends, 181 imported stock blends, 7,680 packed cells,
  zero unresolved cells.
- Zone comparisons: Zone 1, 2, 5_1, 11, 16, 17, and 18; every map has zero
  unresolved cells.

The collections are retained from the catalog-identical R102 generation. R104
changes diagnostic provenance only and does not invalidate their packed grids.

## Focused manual checks

1. Reproduce any visibly wrong uniform placement or generated blend, select
   **Mark paint issue**, and mark only the affected terrain cells.
2. Send the resulting
   `diagnostics/terrain-paint/<map>-paint-issues.json`. Do not infer source
   identity from the displayed physical slot number.
3. For a generated blend, verify the marked tile contains
   `physicalIdentity.origin = canonical-generated`, a non-empty pixel hash,
   mask provenance, and `inputUniformSources`. For authored artwork, verify it
   contains a source namespace and slot.
4. Repeat one mark after painting more terrain. Earlier report entries must
   keep their captured hashes and provenance even if working slots are later
   recycled.

## Review boundary

R104 makes future reports conclusive about artwork ownership. It does not claim
that the current multi-family junction compositor is visually accepted, and it
does not resolve the pending `bz13h` slot 11 semantic review.
