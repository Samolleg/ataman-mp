# Alpha 26 terrain paint modes R150 Classic parity and Manual preview test

This revision replaces R149's over-strict Classic Auto refusal path with the
working authored-atlas resolver used by the semantic-atlas implementation. It
retains the reviewed R148 batch-one semantic catalog (`ee5d1139`, preset
SHA-256
`1D980F2DD24ADE17DD2AD3A0F37E80C0885840FB550CDC937B45FF6B6223EA89`).

## Changes

- Classic Auto now prefers every exact authored signature candidate and valid
  quarter-turn. When the current atlas lacks a boundary blend, only that local
  cell receives the selected proven uniform tile as a hard edge; the rest of
  the stroke is no longer refused.
- Same-material Classic Auto repaint still reselects among valid physical
  variants and rotations using the stable per-stroke distribution seed.
- The Classic Auto palette is laid out correctly on its first visible frame.
  It scrolls within its allocated area instead of drawing over brush size and
  shape controls until another paint-mode tab is visited.
- Manual mode now shows both its complete cell-centered brush footprint and a
  slightly lifted physical-tile preview under the cursor. The lift prevents
  the normal terrain depth test from hiding a coplanar preview.
- Help, status, and palette tooltips now describe the actual Classic Auto
  resolver behavior.

## Focused manual test

1. Open Zone2 and select **Classic Auto** before visiting another paint tab.
   Confirm the palette does not overlap the brush size/shape controls.
2. Paint several existing families across mixed terrain. Confirm normal
   strokes apply instead of producing repeated whole-stroke refusals. Exact
   authored blends should remain preferred; genuinely absent blends may form a
   local hard edge, and the atlas slot count must not grow.
3. Repaint an already matching area in Classic Auto. Confirm valid authored
   uniform variants/rotations can change without changing semantic material
   boundaries; undo and redo must restore exact packed results.
4. Select **Manual**, choose a physical atlas tile, and move the cursor over
   terrain without clicking. Confirm the selected tile is visibly previewed
   under the cursor and the full circle/square brush footprint follows it.
5. Change Manual brush size and shape, paint once, then undo/redo. Confirm the
   footprint matches the affected cell set and the exact chosen tile remains
   stable.

## Known limitations

- Classic Auto deliberately does not add missing artwork. Its local uniform
  hard edge is the non-growing fallback; use Modern when a generated exact
  blend is required.
- Automatic semantic inference for unknown third-party atlas identities remains
  out of scope. Manual physical-slot painting remains available.
- Rotation-equivalent physical deduplication remains disabled by decision.
