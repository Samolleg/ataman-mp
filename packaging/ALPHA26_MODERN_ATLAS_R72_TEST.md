# Modern terrain atlas R72 tester changelist

R72 promotes every explicitly approved R44-R71 family/topology mask, fixes the
review/runtime rotation boundary, and rebuilds the real-JMV material maps with
the authoritative R87 semantic catalog.

## Changed

- Runtime generation now uses 41 hash-pinned reviewed masks across Grass,
  Soil, Stone, Gravel, Paving, Sand, Snow, Ice, and Water. Each asset applies
  only to its approved family, topology, and v1/v2 variation. Unreviewed
  combinations keep the procedural fallback.
- Paving uses the approved raw authored edges. Sand, Snow, and Ice use their
  approved detailed-opacity routes. The other approved hard families use their
  conservative repaired authored masks.
- Quarter-turn selection is derived from the exact requested corners. Diagonal
  masks preserve the grid-covariant artwork orientation despite their 180-degree
  semantic symmetry; no orientation score or material-ID ordering is used.
- The questionnaire now labels physical-atlas source orientation separately
  from logical-grid mask/outcome orientation, fixing the misleading Gravel
  preview. The all-in-one map builder now passes the selected paint owner to
  both mask and result rendering, fixing its former family mismatch.
- The R87 semantic catalog is embedded at SHA-256
  `08B0ACF9C9D382F11DD01CC1E82EE93DE0F42A1B1E4EC4DB8458630A5B76B7B5`:
  38 presets, 58 bindings, 453 families, and 45 transition-support families.
- All bundled maps use installed stock Jamais Vu assets. No EI-HD research
  asset is included.

## Included visual collections

- **Final material stripes:** 72 materials, 603 physical slots, 7,680 packed
  cells, zero unresolved cells.
- **Every-material radius-3 circles:** 72 materials, 726 physical slots, 7,680
  packed cells, zero unresolved cells.
- **Zone comparisons:** Zone 1, 2, 5_1, 11, 16, 17, and 18; every map resolves
  with zero unresolved cells.

Every visual manifest records physical slot identity/provenance, runtime terrain
type, canonical four-corner signature, pixel SHA-256, and the packed cell grid.
A finalized physical slot number is not evidence of an original atlas identity.

## Focused manual checks

1. Open `final-material-stripes.collection` and
   `all-material-circles.collection`. Compare transitions across families: they
   must no longer share one procedural silhouette.
2. Spot-check diagonal Gravel, Sand, Water, Snow, Ice, Grass, Stone, and Paving
   transitions. Confirm the requested material occupies exactly the marked
   corners and the authored artwork is not rotated to the other valid diagonal.
3. Check Paving edges remain crisp/raw, while Sand/Snow/Ice retain detailed
   opacity structure and feathered authored boundaries.
4. Paint and repaint a broad uniform area. Confirm genuine authored variants
   and rotations vary spatially; undo/redo and save/reopen must preserve the
   exact packed result.
5. Right-click a terrain tile and release. Confirm the information tooltip
   remains visible without holding the button and does not blink while held in
   the same cell.
6. Open the seven zone-set collections and compare material naming and corner
   ownership with the stripe/circle maps.

## Review boundary

Only explicitly approved family/topology/variation masks are promoted. Other
v1/v2 variants and multi-family junction composition remain on the existing
procedural path pending their own visual review. Automated exact-corner and
integrity checks do not replace the manual visual pass above.
