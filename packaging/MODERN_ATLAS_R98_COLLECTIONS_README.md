# R98 modern terrain atlas collections

Open the `.collection` files in this directory. Their MPR paths are relative to
the descriptors and are checked by the package finalizer.

- `final-material-stripes.collection` uses the regenerated 75-material stripe
  MPR under `final-material-stripes/` (601 active slots, zero unresolved cells).
- `all-material-circles.collection` uses the regenerated radius-3 MPR under
  `all-material-circles/` (729 active slots, zero unresolved cells).
- `zone-set-zone*.collection` files use current R98 comparisons for Zones 1, 2,
  5_1, 11, 16, 17, and 18.

Keep each MPR together with its adjacent `-atlas.res`, semantic sidecar, and
local `terrain-atlas-profiles` directory. Exact semantic resolution does not
replace human review of material naming, mask shape, texture quality, or
rotation aesthetics.
