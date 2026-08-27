# Modern terrain atlas R102 tester changelist

This build fixes the remaining path that could substitute uniform artwork when
a non-uniform terrain blend was required. It also synchronizes the reviewed
R102 semantic catalog and regenerates every packaged material map from the
current resolver and catalog.

## Changed

- A requested non-uniform corner signature now resolves in this order: exact
  local authored tile and rotation, exact imported authored tile and rotation,
  then a newly generated exact blend. Uniform artwork is never an acceptable
  fallback for a blend.
- Rotated authored matches reuse the same physical artwork with the required
  packed quarter-turn instead of generating a duplicate.
- The legacy immediate-paint helper now separates blend and uniform candidates.
  It prefers a compatible authored blend and otherwise fails closed; the normal
  modern virtual-atlas path generates the missing exact blend.
- Uniform interiors still select deterministic spatial variations from genuine
  four-corner uniform artwork. This is distinct from the fixed blend-boundary
  behavior.
- The embedded R102 semantic catalog contains 429 families, including 25
  transition-support-only families. Its preset SHA-256 is
  `0411D38DE59C255D36448203F4522939B4D43191AD07E45EDA882291AA09519B`.
- Installed stock Jamais Vu assets are included only in isolated test maps.
  EI-HD research assets are not included.

## Regenerated visual collections

- **Final material stripes:** 80 materials, 623 active physical slots, 383
  generated exact blends, 203 imported stock blends, 7,680 packed cells, zero
  unresolved cells.
- **Every-material radius-3 circles:** 80 materials, 762 active physical slots,
  543 generated exact blends, 181 imported stock blends, 7,680 packed cells,
  zero unresolved cells.
- **Zone comparisons:** Zone 1, 2, 5_1, 11, 16, 17, and 18; every map has zero
  unresolved cells.

Every manifest records physical source identity/provenance, runtime terrain
type, canonical four-corner signature, pixel SHA-256, and the complete packed
cell grid. A finalized physical slot number alone is not source provenance.

## Focused manual checks

1. Open `collections/final-material-stripes.collection`. Inspect every stripe
   boundary and multi-material meeting. No cell requiring a blend should appear
   as an unblended uniform square.
2. Open `collections/all-material-circles.collection`. Inspect the complete
   transition rings, especially rotated corner and half/half shapes. Existing
   authored rotations should look like the correct orientation; missing exact
   signatures should use generated blends rather than uniform tiles.
3. Paint over an existing material boundary, undo, redo, save, and reopen. The
   exact physical variants and rotations must remain unchanged after redo and
   reopen.
4. Use the seven `zone-set-*.collection` files for comparison. Report visually
   wrong uniform/family artwork separately from a missing blend: those are
   semantic-source classifications, not this resolver failure.

## Review boundary

Known false-uniform, probable-family, and no-family leads deliberately remain in
the semantic review queue. This build does not guess those identities. Automated
checks prove exact blend routing, rotations, packed persistence, semantic corner
resolution, and package integrity; visual family and mask quality still require
human review.
