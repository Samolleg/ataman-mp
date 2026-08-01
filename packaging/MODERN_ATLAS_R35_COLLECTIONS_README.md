# R35 modern terrain atlas collections

Open the `.collection` files in this directory. Their MPR paths are relative to
the descriptor and are checked by the package finalizer.

- `final-material-stripes.collection` uses the stripe MPR beside it.
- `all-material-circles.collection` uses the MPR under
  `all-material-circles/`.
- `zone-set-zone*.collection` files use the regenerated comparison maps under
  `zone-set/`.

Keep each MPR together with its adjacent `-atlas.res` sidecar. These maps prove
exact semantic corner resolution and zero unresolved cells; artistic family,
mask, and rotation quality still requires visual review.
