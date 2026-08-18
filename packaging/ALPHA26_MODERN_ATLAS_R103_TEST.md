# Modern terrain atlas R103 tester changelist

This build fixes isolated uniform squares that could remain on blend edges
after partially overlapping semantic paint stamps.

## Changed

- An overlapping stamp no longer replaces an unchanged exact edge blend with
  the stroke material's temporary uniform preview tile.
- Cells whose semantic corner tuple actually changes still use the fast uniform
  live preview and are resolved to an exact authored or generated blend when
  the stroke is released.
- A wholly same-material repaint still rerolls valid physical uniform variants
  and rotations. Undo, redo, save, and reopen continue to preserve the captured
  packed result exactly.
- The R102 semantic catalog and approved blend masks are unchanged. Its preset
  SHA-256 remains
  `0411D38DE59C255D36448203F4522939B4D43191AD07E45EDA882291AA09519B`.

## Included visual collections

- Final material stripes: 80 materials, 623 active physical slots, 383
  generated exact blends, 203 imported stock blends, 7,680 packed cells, zero
  unresolved cells.
- Every-material radius-3 circles: 80 materials, 762 active physical slots,
  543 generated exact blends, 181 imported stock blends, 7,680 packed cells,
  zero unresolved cells.
- Zone comparisons: Zone 1, 2, 5_1, 11, 16, 17, and 18; every map has zero
  unresolved cells.

The collections are retained from the catalog-identical R102 generation. The
R103 change is in interactive overlapping-stroke preview routing and does not
alter atlas generation or fixture output.

## Focused manual checks

1. Paint a material along an existing material boundary, then overlap the
   previous stroke so part of the brush is already the selected material and
   part extends into another material. No isolated uniform square should remain
   on the unchanged part of the blend edge after mouse release.
2. Repeat that overlap from several directions and with radius 1 and radius 3.
   Existing exact authored blends and generated blends should remain blends,
   with their required corner orientation.
3. Undo and redo the final stroke, then save and reopen the MPR. The exact tile
   slots and rotations must remain visually identical.

## Review boundary

Wrong-family or false-uniform authored artwork remains semantic-review work;
poor generated artwork remains mask-review work. This fix addresses only the
logic error where a valid unchanged blend was overwritten by a collateral
uniform live preview.
