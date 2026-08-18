# Modern terrain atlas R37 tester changelist

R37 refreshes every modern-terrain visual collection through semantic review
R69 and adds deterministic spatial variation to proven uniform stroke
interiors.

## Changed

- Uniform stroke interiors now distribute all available structurally proven
  four-corner-uniform physical tiles and quarter-turn rotations by stroke seed
  and cell coordinate. Repeating the same stroke reproduces the exact packed
  field; undo, redo, save, and reopen preserve it byte-for-byte.
- Exact transition/blend tiles keep the rotation required by their requested
  corner signature. Existing authored blends are reused through rotation rather
  than synthesized again, including reviewed cross-atlas artwork.
- The terrain option and tooltip now describe spatial uniform variation.
- The R69 material catalog incorporates 125 reviewed corner corrections and
  preserves 333 explicit keep-current confirmations. Fourteen uncertain
  note-only corners remain deliberately unchanged.
- Final material stripes, every-material radius-3 circles, and all seven
  zone-set comparisons were regenerated from installed JMV stock assets. No
  EI-HD assets are included.

## Included visual collections

- **Final material stripes:** 446 active physical identities, zero unresolved
  cells.
- **Every material radius-3 circles:** 576 active physical identities, zero
  unresolved cells.
- Zone 1, 2, 5_1, 11, 16, 17, and 18 comparisons all resolve with zero
  unresolved cells.

## Focused manual checks

1. Open `final-material-stripes.collection` and
   `all-material-circles.collection` from `collections`. Confirm both maps and
   their atlas sidecars load without a missing-file error.
2. Paint a broad uniform stroke with uniform rotation variation enabled.
   Confirm its interior uses stable spatial texture variation without changing
   the material at its edges; undo, redo, save, and reopen the map.
3. Inspect several mixed-material edges. Confirm their corner orientation stays
   correct and an existing authored blend is reused when a quarter-turn matches.
4. Right-click a terrain tile and release. Confirm the information tooltip
   remains visible without holding the button and does not blink while held
   within one cell.

## Review note

Automated checks prove packed identity, deterministic persistence, structural
uniform purity, and exact corner resolution. Artistic material naming, mask
shape, and texture quality still require human visual review.
