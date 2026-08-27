# Modern terrain atlas R35 tester changelist

R35 fixes the terrain tile-information tooltip, synchronizes the frozen R66
semantic catalog, and replaces the missing R34 stripe/circle maps with newly
generated collections whose relative MPR paths are validated during packaging.

## Changed

- Right-clicking a painted terrain tile now shows one stable information
  tooltip. It remains visible after the button is released, does not flash when
  the held pointer moves inside the same cell, and refreshes only after crossing
  into a different terrain cell. Another click or leaving the applicable tool
  mode dismisses it.
- The material catalog is synchronized through semantic review R66. This
  includes the reviewed BaseGipat Water / Still consolidation, the bz11k and
  Zone14 Stone / Black consolidation, and the latest exact Zone10 and bz11k
  corner decisions.
- Final material stripes and every-material radius-3 circles were regenerated
  from installed JMV stock assets using the frozen R66 catalog. No EI-HD assets
  are included.
- Distribution validation now rejects a collection when its relative MPR is
  absent or escapes the package, preventing the R34 staging error from
  recurring.

## Included visual collections

- **Final material stripes:** 466 active physical identities, zero unresolved
  cells.
- **Every material radius-3 circles:** 600 active physical identities, zero
  unresolved cells.
- The established Zone 1, 2, 5_1, 11, 16, 17, and 18 comparison collections
  are regenerated from the same R66 catalog.

## Focused manual checks

1. In terrain paint mode, right-click a tile and release immediately. Confirm
   the information remains still and readable. Hold right-click and move within
   one cell, then cross a cell boundary; it must not blink within the cell and
   must update once for the new cell.
2. Open `final-material-stripes.collection` and
   `all-material-circles.collection` from the package's `collections` folder.
   Both must find and open their MPR without a missing-file error.
3. Review Water / Still versus Swamp, Stone / Black and Stone / Dark, and the
   corrected Zone10 mixed corners. Mark the smallest visibly wrong region if a
   reviewed identity or generated blend still looks incorrect.

## Review note

Three R66 exact-corner selections were explicit visual decisions despite a
different nearest-family metric ranking: bz11k slot 61 NE Stone / Dark, Zone10
slot 420 NW Ground / Dirt, and Zone10 slot 375 NE Ground / Dirt. They have no
exact-pixel conflict and are deliberately preserved for this visual test.
