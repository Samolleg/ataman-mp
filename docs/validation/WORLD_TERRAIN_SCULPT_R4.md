# EI-WORLD-SCULPT-001-MOVING-RIM — whole connected shifted seam

R3 failed the owner's recorded scenario (`20260912-1312-16.8301064.mp4`).
Its reconciliation stopped at the brush rim and left incompatible fixed anchors
outside. Testing only the covered center missed cracks left behind as the brush
moved. The owner explicitly authorized height changes along the entire connected
shifted seam beyond the brush, accepting long-edge straightening.

## Regression and implementation

`movingStaggeredStrokeKeepsWholeTouchedSeamClosed` starts with a closed flat
three-map boundary, adds an interior peak and smooths at Y=32,33,34 with radius 5.
R3 failed: maximum gap 0.940718700 at Y=27.5. The corrected test measures the
entire common edge every 1/8 unit after each stamp; maximum 0.000762951, within
the combined uint16 height precision. It also verifies edits extend outside the
circle, merges the three stamps into one command, and checks byte-identical
sector restoration on undo and successful redo.

All enabled-map boundary vertices now participate in contact discovery. Brush
influence still controls ordinary terrain changes. A connected shifted boundary
component is reconciled only if a boundary vertex is reached by the stamp; its
whole affine height profile is fitted from current heights plus the brush's
proposed changes. Other components, outside interior vertices, protected maps,
map placement and packed XY offsets are unchanged. Extended boundary deltas use
the existing compound history, preview and cohort-save path.

## Checks and remaining scope

- Core: 13 passed, including full-length moving-edge checks, both orientations,
  all four tools, saturated ranges, reversed map order, protected/no-contact/
  zero-strength cases, whole-command undo/redo and three-map save/reopen.
- Installed zone1/zone2/zone3obr were loaded read-only; eight in-memory smooth
  stamps moved from (352,288) to (352,295). Samples over the entire common edge,
  Y=63.5..383.5, gave maximum gap 0.000494528. Calculation and application took
  about 21 ms for eight stamps here; this does not measure viewport performance.
- No installed MPRs were written or copied into repository fixtures.
- Widget suite: 16 passed / 2 offscreen context skips; native R2 GPU regression:
  3 passed, no skips.
- Exact video-scene visual acceptance and representative viewport performance
  remain manual. The existing full `--ui-widget-check` crash remains unresolved.
  A sandbox launch first returned loader error `0xC0000135`; an isolated retry
  stalled and was terminated after checking its exact executable/arguments.
  The normal Windows headless launch reproduced `0xC0000005`. Release and x86
  components built; the staged package finalizer/validator passed.
- Long connected edges may visibly straighten, including distant boundary
  vertices. Connections to protected/unselected maps are outside this guarantee.
  XY gaps, noncollinear boundaries and overlapping map interiors are not repaired.

The new R4 folder preserves earlier builds for comparison because editing scope
now extends beyond the brush. Parent acceptance remains open; passing this
regression alone does not close it. See `packaging/WORLD_TERRAIN_SCULPT_R4.md`.
