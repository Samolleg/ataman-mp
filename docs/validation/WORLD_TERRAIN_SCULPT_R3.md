# EI-WORLD-SCULPT-001-TRIPLE — shifted boundary profiles

The owner accepted the R2 two-map GPU fix and reported the remaining triple
junction in `20260912-1239-41.0343493.mp4`. Zone1/zone2 have 161 shared XY
vertices. Zone3obr touches their eastern edges but starts at Y=63.5, so its
boundary knots do not coincide with either grid. All three maps were enabled.
The previous exact-vertex grouping consequently smoothed the third edge
independently. The owner explicitly replaced that policy with coordinated heights
inside the brush, accepting local straightening and retaining map placement.

## Reproduction and correction

The synthetic regression places A/B at (0,0)/(0,32), with C at (32,16.5).
It compares interpolated boundary heights every 1/8 unit around the triple
junction, rather than comparing only vertices. Before the fix the first
surface-equality assertion failed (2 passed, 1 failed).

After the immutable brush calculation, the implementation finds collinear,
overlapping segments on opposite sides of enabled maps. Segments participate
only when both endpoints have positive brush influence. Noncoincident knots
connect into a common boundary component; a least-squares affine profile fits
the proposed heights, retaining slope instead of forcing a horizontal plateau.
Shared corner vertices remain one variable, including at the three-map junction.
Height range limiting scales the whole profile, preserving its linearity; each
MPR quantizes once at command generation. Aligned segments keep their existing
shared-vertex calculation. No map origins or packed XY offsets are changed.

## Checks

- Core suite with opt-in installed-map inspection: 12 passed, 0 failed/skipped.
  Standard runs without `EI_WORLD_SCULPT_MAPS` skip only that optional check.
- Synthetic triple: all four modes, both orientations, ordinary and saturated
  requests, reversed target order, full source-payload restoration on undo,
  redo, validated cohort save/reopen and all reopened heights checked.
- Protected neighbors, zero strength, separated edges and overlapping map
  rectangles remain unaffected by the new boundary reconciliation.
- Installed zone1/zone2/zone3obr: eight smooth stamps at (352,288), radius 5,
  strength 1, in memory only. Maximum measured covered-edge difference was
  0.000254583 world units, within the format's height precision. Eight stamp
  calculations plus application took 1–2 ms here; this is not viewport timing.
  No installed game data was copied into fixtures or saved.
- Widgets: 16 passed, 2 offscreen context skips. Separate native GPU regression
  retained from R2: 3 passed, no skips.
- Release editor and x86 runtime components built. Full `--ui-widget-check`
  still exits with the pre-existing `0xC0000005`; this remains a parent blocker.

## Limits and remaining acceptance

Exact profile agreement is guaranteed only over the reconciled complete segments,
up to height quantization. A cell crossing the brush rim transitions to unchanged
geometry and can retain a mismatch. Resolving arbitrary staggered polylines
everywhere while fixing all outside vertices is generally impossible without
changing topology or extending the edited area. Neither is authorized here.
Gaps in XY, noncollinear edges and overlapping map interiors are not stitched.

The exact video scene has not been replayed interactively. The new profile policy
is implemented and tested, but full visual acceptance, mouse interaction,
transitions and the unresolved full UI check keep the parent task open. R3 uses
a new folder because it changes geometric behavior and preserves the accepted R2
for comparison. Current instructions: `packaging/WORLD_TERRAIN_SCULPT_R3.md`.
