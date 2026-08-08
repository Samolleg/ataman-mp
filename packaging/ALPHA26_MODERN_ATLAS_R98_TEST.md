# Modern terrain atlas R98 tester changelist

This build combines the fully reviewed R44-R77 authored-mask restoration with
the authoritative R98 semantic catalog and newly regenerated real-JMV visual
test maps.

## Changed

- The runtime embeds 88 hash-pinned reviewed mask assets across Grass, Soil,
  Stone, Gravel, Paving, Sand, Snow, Ice, and Water. The R75-R77 restoration
  pass replaces rejected geometric masks with approved authored ownership,
  opacity gradients, and non-rectilinear support-edge cleanup.
- Mask selection and rotation follow the exact requested NW/NE/SE/SW ownership.
  Diagonal artwork is not selected by an orientation score or material-ID
  ordering, and the packaged maps report zero missing masks and zero unapproved
  two-family non-junction cells.
- Genuine authored uniform variants and exact authored blend variants remain
  available to deterministic per-stroke selection. Undo/redo and save/reopen
  preserve the exact packed physical slot and rotation chosen for every cell.
- The R98 semantic catalog is embedded at SHA-256
  `7C74A820A82AA8599152B627F605C75154765AD99700453ECB0438FD7CE6ED6F`:
  38 presets, 58 bindings, 457 families, and 46 transition-support families.
  The R98 review closes the six remaining shared bz11k/Zone14 Sand / Irregular
  southwest corners at slots 194, 195, 196, 292, 394, and 396.
- Right-click terrain information remains visible after release and no longer
  blinks while the pointer stays over the same cell.
- All bundled maps use installed stock Jamais Vu assets. EI-HD research assets
  are not included.

## Included visual collections

- **Final material stripes:** 75 materials, 601 active physical slots, 7,680
  packed cells, zero unresolved cells.
- **Every-material radius-3 circles:** 75 materials, 729 active physical slots,
  7,680 packed cells, zero unresolved cells.
- **Zone comparisons:** Zone 1, 2, 5_1, 11, 16, 17, and 18; every map resolves
  with zero unresolved cells.

Every visual manifest records physical slot identity and source provenance,
runtime terrain type, canonical four-corner signature, pixel SHA-256, and the
complete packed cell grid. A finalized physical slot number alone is not
evidence of an original atlas identity.

## Focused manual checks

1. Open `collections/final-material-stripes.collection` and
   `collections/all-material-circles.collection`. Confirm all 75 material names
   are present and that transitions use family-appropriate authored shapes.
2. Spot-check Gravel, Paving, Sand, Snow, Ice, Water, Grass, Soil, and Stone at
   outer, adjacent-edge, diagonal, and inside corners. Requested ownership must
   occupy exactly the expected corners without square cuts or arbitrary
   quarter-turns.
3. Confirm Paving keeps crisp authored texture detail while Sand, Snow, and Ice
   retain their reviewed continuous-alpha and feathered authored boundaries.
4. Paint and repaint a broad uniform area. Authored physical variants and
   rotations should vary spatially; undo/redo and save/reopen must preserve the
   exact arrangement.
5. Right-click a terrain tile, then release. The information tooltip should
   remain still and visible without holding the mouse button.
6. Open the seven `zone-set-*.collection` files and compare semantic names,
   authored uniform variants, and corner ownership with the stripe/circle maps.

## Review boundary

Automated tests prove exact semantic corner resolution, packed-grid integrity,
and package cleanliness. They do not replace the requested in-editor visual
review of mask aesthetics and authored family classifications.
