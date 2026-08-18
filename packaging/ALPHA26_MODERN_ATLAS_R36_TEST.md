# Modern terrain atlas R36 tester changelist

R36 preserves the stable terrain-information tooltip from R35 and refreshes
the material catalog and every visual test collection through semantic review
R67.

## Changed

- Zone6 and Zone6_2 now label the reviewed family `Ground / Dark`.
- Zone7 now labels the reviewed family `Sand / Grainy`.
- bz5g, bz6g, and Zone8 now distinguish `Rock / Underwater emerald` and
  `Sand / Underwater` from their surface counterparts.
- Final material stripes, every-material radius-3 circles, and the seven
  zone-set comparisons were regenerated from installed JMV stock assets.
  No EI-HD assets are included.
- R67 changes names only. Material corner signatures, atlas bindings, allowed
  layers, and per-map underwater roles remain identical to R66.

## Included visual collections

- **Final material stripes:** 445 active physical identities, zero unresolved
  cells.
- **Every material radius-3 circles:** 575 active physical identities, zero
  unresolved cells.
- Zone 1, 2, 5_1, 11, 16, 17, and 18 comparison collections all resolve with
  zero unresolved cells.

## Focused manual checks

1. Open `final-material-stripes.collection` and
   `all-material-circles.collection` from the package's `collections` folder.
   Confirm both MPRs open without a missing-file error.
2. Inspect the renamed Ground / Dark, Sand / Grainy, Rock / Underwater
   emerald, and Sand / Underwater families. Confirm underwater variants are
   visually distinct and appear only where their per-map role permits them.
3. In terrain paint mode, right-click and release a tile. Confirm the R35
   information tooltip remains stable and does not blink while held within one
   cell.

## Review note

The lower stripe/circle identity counts are intentional: R67 consolidates
three previously separate global name identities without changing physical
corner classifications. Visual quality of material naming and generated blends
still requires human review.
