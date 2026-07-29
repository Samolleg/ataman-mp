# Modern atlas r24 tester changelist

This revision replaces the rejected procedural blend masks with transition
silhouettes reconstructed from reviewed stock Jamais Vu atlas tiles.

## Changed

- Exact authored JMV transitions are still reused before any generation.
- Missing two-family tuples now transfer a native-resolution mask from the
  best reviewed stock material-school/topology match. Three- and four-family
  junctions compose reviewed stock one-corner silhouettes.
- The procedural noise/style generator has been removed. If no reviewed stock
  mask exists, painting reports the evidence gap instead of inventing artwork
  or substituting a material identity.
- Finalize preview tooltips and the package manifest identify the stock
  namespace/slot provenance used by every generated blend.
- The embedded semantic catalog incorporates the sibling review corrections,
  including the Zone2 Stone/Lime identity and removal of Zone4's false
  Ground/Trampled family.
- The included Zone2 stripe collection contains all 68 current uniform land
  materials, 86 exact stock blend reuses, 268 stock-derived exact tiles, and
  zero unresolved cells. Its native-resolution review sheet shows the exact
  generated pixels staged by the map.

## Focused manual checks

1. Open `modern-atlas-real-map-tests/final-material-stripes.collection` and
   inspect transitions across the complete material stripe field, especially
   ice, snow, grass, sand, stone, and the lower multi-family junction row.
2. Paint one uniform material across several unrelated existing materials.
   Every touched vertex must remain painted and every resulting edge/junction
   must resolve without a no-op.
3. Use **Finalize map-specific atlas package**. Review the added-tile grid and
   hover generated tiles; each blend must show one or more stock JMV
   namespace/slot sources.
4. Compare `generated-blends-review.png` with the map. It is native 128x128
   tile data in manifest order, not an enlarged conceptual rendering.

## Review status

Automated checks prove exact semantic corners, stable hashes, full-resolution
mask provenance, seam closure, save/reopen, and zero unresolved cells. Artistic
acceptance remains a human review: stock-derived template ranking and the
three/four-family composition policy are intentionally exposed in this build
for feedback.
