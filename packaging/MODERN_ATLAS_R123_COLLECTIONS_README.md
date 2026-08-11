# R123 modern terrain atlas collections

Open the `.collection` files in this directory. Their MPR paths are relative to
the descriptors and are checked by the package finalizer.

- `uniform-brush-inventory-256px.collection` displays all exact reviewed 256px
  uniform artwork as four-rotation cards.
- `uniform-brush-inventory-128px.collection` provides the same exhaustive
  review for native 128px artwork.
- `final-material-stripes.collection` lays out all 82 current land brushes for
  transition edges and multi-material junction review.
- `all-material-circles.collection` paints all 82 brushes as radius-3 circles
  for normal brush-scale mask and uniform-variation review.
- `full-game-family-stamp.collection` provides isolated 3x3 stamps for every
  current land brush family.
- `zone-set-zone*.collection` compares Zones 1, 2, 5_1, 11, 16, 17, and 18.

The two uniform inventories jointly cover 984 of 984 authorized source aliases,
collapsed to 305 exact artworks by semantic identity, runtime terrain type, and
decoded RGBA hash. Keep each MPR together with its adjacent `-atlas.res`,
semantic sidecar, local `terrain-atlas-profiles` directory, and visual review
index.

These generated collections cover land brushes. Test Lava and the two
BaseGipat `Water / Still` hues directly on an overlay-capable map. Exact
coverage is not visual approval: mark bad cells in the editor so the diagnostic
retains canonical namespace/slot aliases, packed rotation, physical pixel hash,
and generated-mask provenance.
