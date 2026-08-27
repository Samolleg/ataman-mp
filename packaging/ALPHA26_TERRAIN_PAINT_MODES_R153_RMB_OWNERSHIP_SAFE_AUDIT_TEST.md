# Terrain paint modes R153: held-RMB ownership and Safe audit

This revision fixes the repeatable held-right-click crash reported in R152 and
makes Classic Auto Safe no-ops diagnosable without weakening its exact-atlas
safety rule.

## Changes

- Right-click material sampling no longer replaces an already-active terrain
  brush from inside that brush's own mouse handler. Palette updates can still
  change the selected material, but the operation object remains alive until a
  real tool transition occurs.
- The R152 logical sampling gate remains in place: Modern and Classic Auto
  update only after entering a different nearest-vertex vicinity; Manual updates
  only after entering another physical cell.
- Every completed terrain stroke now records palette mode, Tile/Vertex target,
  Safe state, layer, material ID, changed count, and the exact number of
  unsupported vertices or boundary cells preserved by Classic Auto Safe.
- Classic Auto Safe still never substitutes a uniform tile where an exact blend
  is required. Valid independent exact regions remain paintable; unsupported
  connections stay unchanged.

## Focused manual checks

1. Open a test MPR, choose Modern, hold RMB, and move continuously across many
   vertices for at least 30 seconds. Repeat in Classic Auto and Manual. The
   brush should track the sampled material without blinking or crashing.
2. In Classic Auto Safe, try several known-existing connections and several
   absent ones in both Vertex and Tile target modes. Existing exact connections
   should paint. Unsupported connections should remain unchanged, with the
   status line stating how many vertices/boundary cells were preserved.
3. If any Safe stroke still feels too conservative, export the paint diagnostic
   or send `logs/workflow.log`; each stroke summary now contains enough mode and
   skip context to reproduce it.
4. Confirm the R152 Tile outline, Manual preview, Z/X/C shortcuts, right-click
   tooltip, wheel camera controls, and Resolve Missing Blends remain functional.

## Known boundary

This revision deliberately does not relax Classic Auto Safe based on the lost
R152 diagnostic. A safety relaxation requires a provenance-qualified example
where an exact compatible atlas connection existed but was skipped.
