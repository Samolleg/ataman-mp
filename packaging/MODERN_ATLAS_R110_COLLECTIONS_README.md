# R110 modern terrain atlas collections

Open the `.collection` files in this directory. Their MPR paths are relative to
the descriptors and are checked by the package finalizer.

- `final-material-stripes.collection` uses the regenerated 81-material stripe
  MPR under `final-material-stripes/` (617 active slots, zero unresolved).
- `all-material-circles.collection` uses the regenerated 81-material radius-3
  MPR under `all-material-circles/` (754 active slots, zero unresolved).
- `zone-set-zone*.collection` files use current comparisons for Zones 1, 2,
  5_1, 11, 16, 17, and 18; every map has zero unresolved cells.

Keep each MPR together with its adjacent `-atlas.res`, semantic sidecar, and
local `terrain-atlas-profiles` directory. Exact semantic resolution does not
replace human review of material naming, mask shape, texture quality, or
rotation aesthetics. Report bad cells with the editor's paint-issue export so
the original authored or generated provenance is preserved.
