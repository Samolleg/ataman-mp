# EI-NEIGHBOR-OBJECT-CONTACT-54 — validation

Date: 2026-09-23
Base: Ataman 53, `e5923d0d0a8156b34642df237d47ff1a9445b37e`
Branch: `codex/neighbor-object-contact-v54`

## Cause and change

`CNeighborMapLayout::build` deliberately returns every placed map in the active
world component. `CView::loadNeighborLandscapes` formerly called
`prepareNeighborScene` for every placement, which loaded each MOB and prepared
all static objects regardless of the distance from the active map.

The view now checks the placed map rectangles before scheduling object work.
Maps sharing an edge, a corner, or an overlapping area qualify, with a
half-unit tolerance for quarter-unit layout snapping. Other maps still load
their MPR terrain, but skip MOB reads and static-model/resource preparation.
Changing a map origin invalidates the neighbor context and applies the same
selection again.

## Checks

- Focused `onlyTouchingMapsNeedObjects`: PASS for shared edge, corner, overlap,
  quarter-unit rounding gap, separated maps, and invalid dimensions.
- Focused `neighborSceneRejectsInvalidTerrain`: PASS; terrain remains available
  when MOB reads are disabled, and a missing MOB produces no diagnostic in that
  mode.
- Complete `neighbor_map_layout_tests`: 8 passed, 1 failed. The failure is the
  existing `placesNearestZone7BackgroundsFromPairedEntrances` assertion about
  `gz1g`'s X position in unchanged world-origin inference. The new contact
  test passed. A baseline run of that old assertion was not made.
- Release 54: PASS with Qt 5.12.12 / MinGW 7.3 and fresh shadow directory
  `build-release-54-neighbor-object-contact`; runtime components built.
- Staged `--version`: PASS, `EI Mapper Endurance Edition 54`. The executable
  was run from a staging folder with its Qt DLLs, not the raw compiler output.
- Staged `--ui-widget-check`: exit 1. The property clipboard checks
  (`compatiblePaste*`, `fieldPaste*`, `numericComponentPaste` and related
  component copy/paste) failed, as recorded for 53. Neighbor-specific checks
  were not reported as failing.
- Fresh package `build-staging-54-neighbor-object-contact`: finalizer and
  validator PASS; independent coverage and SHA-256 verification PASS for all
  101 managed files. Staged EXE matches the Release EXE:
  `ca2c196241b2899af69495b53b65f568515b504681fa14209d73c07e56ab8baf`.

Manual verification remains: open a real world with both touching and distant
maps, check static objects at the active border and terrain-only distant maps,
then move a map across the contact boundary and verify that its objects appear
or disappear after placement refresh.
