# Landscape semantic-paint grid-Y correction R16

R16 replaces all rejected per-shape packed-rotation corrections with one
coordinate conversion at the catalog lookup boundary.

## Evidence

The user-supplied R15 diagnostic has SHA-256
`f3927b5e14597e37275297a0aeb484144ab6cc2323121276d3c8e0c113cc46b2`.
It records a single size-1 Grass/dark stroke on uniform Grass/common:

- the solver requested the correct shared-corner 3x3 lattice;
- the R14 lookup returned the opposite catalog signature for all four half
  edges;
- center and corner requests passed through unchanged.

The corresponding annotated R14 screenshot established that the two
row-boundary half edges were visually correct, the two column-boundary half
edges were exchanged, and each column's corner pair was exchanged vertically.
This is exactly the result of treating atlas-image Y and terrain-grid Y as the
same direction.

The inherited renderer constructs tile geometry with grid row zero on the
opposite vertical side from the decoded atlas image used by the catalog.
Therefore a requested grid signature must be mirrored across Y before catalog
selection:

- `x0y0 <-> x0y1`
- `x1y0 <-> x1y1`

The conversion applies uniformly to centers, half edges, and corners. It is a
lookup-only transform: direct packed-tile decoding and untouched MPR data are
unchanged.

## Regression contract

The Zone1 nine-cell test now checks every requested corner independently. It
first requires the selected catalog signature to be the grid-Y mirror, then
applies the renderer conversion independently and requires the visible
signature to equal the original solver request. This fails under the rejected
R12-R14 rotation special cases.

Changed-MPR Save and Save As remain disabled.
