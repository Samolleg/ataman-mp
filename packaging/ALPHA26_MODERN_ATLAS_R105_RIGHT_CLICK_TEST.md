# Modern terrain atlas R105 tester changelist

This build fixes the right-click crash reported in R104 while retaining its
exact paint-issue provenance and the R103 overlapping-preview correction.

## Changed

- Right-clicking an imported or generated working-atlas tile no longer passes
  its recyclable extension slot to the authored MPR atlas inspector.
- The inspector now rejects negative, extension, metadata-mismatched, and
  out-of-table tile indices before reading tile-category metadata.
- A valid authored slot still updates the legacy manual tile selection and
  rotation. Modern working-atlas tiles continue through semantic sampling:
  uniform tiles select their material, while transition tiles retain the
  current material and report that a uniform tile must be sampled.
- The headless widget regression exercises working slot 1393, the exact index
  recovered from the R104 crash.
- No material family, authored blend signature, approved mask, resolver rule,
  or compositor behavior changed. The preset SHA-256 remains
  `0411D38DE59C255D36448203F4522939B4D43191AD07E45EDA882291AA09519B`.

## R104 bad-material reports

The 13 R104 marks resolved to six exact imported authored blends, not generated
masks. All six are queued for all-four-corner semantic review; no relabel was
guessed in this build:

- `Zone7` slot 249
- `Zone6_2` slot 390
- `bz3g` slots 218, 204, 470, and 281

`bz3g` slot 281 corroborates an existing R102 review item; the other five are
new provenance-qualified review groups.

## Included visual collections

- Final material stripes: 80 materials, 623 active physical slots, 383
  generated exact blends, 203 imported stock blends, 7,680 packed cells, zero
  unresolved cells.
- Every-material radius-3 circles: 80 materials, 762 active physical slots,
  543 generated exact blends, 181 imported stock blends, 7,680 packed cells,
  zero unresolved cells.
- Zone comparisons: Zone 1, 2, 5_1, 11, 16, 17, and 18; every map has zero
  unresolved cells.

The collections are retained byte-for-byte from the catalog-identical R104
package. The crash fix does not affect their generated terrain payloads.

## Focused manual checks

1. Paint several materials until the working atlas has imported/generated
   extension tiles, then right-click uniform areas and transition boundaries
   repeatedly. The editor must not crash.
2. Right-click a uniform working tile and confirm its semantic material becomes
   the selected brush. Right-click a mixed transition and confirm the current
   brush is retained with the transition guidance message.
3. Hold and release right-click over several tiles. The persistent tile
   information tooltip must remain stable, and sampling must still occur.
4. If an incorrect material appears inside a blend, mark only the affected
   cells and send the resulting v3 paint-issue JSON.

## Review boundary

R105 fixes the proven UI ownership crash. It does not pre-apply any of the six
pending authored-blend semantic decisions.
