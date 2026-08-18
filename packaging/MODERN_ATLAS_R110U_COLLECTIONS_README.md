# R110U modern terrain atlas collections

Open the `.collection` files in this directory. Their MPR paths are relative to
the descriptors and are checked by the package finalizer.

- `uniform-brush-inventory-256px.collection` displays every exact reviewed
  256px uniform artwork as a 2x2 four-rotation card.
- `uniform-brush-inventory-128px.collection` provides the same exhaustive
  review for native 128px artwork.
- `final-material-stripes.collection` retains the regenerated 81-material
  stripe map for transition edges and junctions.
- `all-material-circles.collection` retains the 81-material radius-3 circle
  map for normal brush-scale masks and uniform distribution.
- `zone-set-zone*.collection` retains comparisons for Zones 1, 2, 5_1, 11,
  16, 17, and 18.

The two inventory manifests jointly cover 1,011 of 1,011 authorized source
aliases, collapsed to 317 exact artworks by material identity, runtime terrain
type, and decoded RGBA hash. Keep each MPR together with its adjacent
`-atlas.res`, semantic sidecar, local `terrain-atlas-profiles` directory, and
`uniform-artwork-index.csv`.

Exact coverage is not a visual approval. Mark bad cards or export their index
rows so the authored namespace/slot aliases and pixel hash reach semantic
review without inferring identity from a finalized physical slot.
