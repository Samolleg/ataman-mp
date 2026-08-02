# R41 modern terrain atlas collections

Open the `.collection` files in this directory. Their MPR paths are relative to
the descriptor and are checked by the package finalizer.

- `final-material-stripes.collection` uses the stripe MPR beside it.
- `all-material-circles.collection` uses the MPR under
  `all-material-circles/`.
- `zone-set-zone*.collection` files use the established reviewed comparisons
  under `zone-set/`.
- `questionnaire/blend-mask-r41-mask-questionnaire.html` is the grouped visual
  mask review. It references the final R75 circle-map review images.

Keep each MPR together with its adjacent `-atlas.res` sidecar. Exact semantic
corner resolution and zero unresolved cells do not replace human review of
material naming, mask shape, texture quality, or rotation aesthetics.
