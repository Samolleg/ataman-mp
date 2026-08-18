# Alpha 26 terrain paint modes R152 outline and RMB sampling test

This revision is a focused follow-up to the R151 terrain-paint beta package.
It retains the reviewed R148 batch-one semantic catalog (`ee5d1139`, preset
SHA-256 `1D980F2DD24ADE17DD2AD3A0F37E80C0885840FB550CDC937B45FF6B6223EA89`).

## Changes

- Tile targeting now always shows the same cell-centered brush footprint as
  Manual painting. A valid semantic brush no longer loses its outline merely
  because no optional physical tile is available for the lifted preview.
- Holding right mouse now samples once per nearest terrain vertex in Modern
  and Classic Auto, or once per physical cell in Manual. Raw mouse-move events
  inside the same vicinity no longer rebuild the palette and preview.
- Crossing into another snapped vicinity still updates the selected brush.
  Switching terrain layer or paint mode also invalidates the sample gate.
- Releasing RMB or moving focus away ends the sampling gesture cleanly.

## Focused manual test

1. Enter Modern or Classic Auto, choose **Tile**, and move over the terrain.
   Confirm the complete cell-centered brush outline is visible and matches the
   Manual footprint for the same brush size and shape.
2. Hold RMB over one vertex and make small mouse movements without crossing
   into another vertex vicinity. Confirm the selected brush remains stable and
   the editor does not blink, stall, or crash.
3. Keep RMB held and cross several vertex boundaries. Confirm the brush updates
   once as each new nearest vertex is entered.
4. Switch to Manual and repeat. Confirm sampling changes per physical cell and
   retains the sampled slot and packed rotation.
5. Confirm the RMB tile-information tooltip remains available throughout.

## Known limitations

- Classic Auto Safe cannot invent artwork. Unsupported connections stay
  unchanged; use Modern or **Resolve missing blends...** when the atlas may
  grow.
- Automatic semantic inference for unknown third-party atlas identities remains
  out of scope. Manual physical-slot painting remains available.
- Rotation-equivalent physical deduplication remains disabled by decision.
