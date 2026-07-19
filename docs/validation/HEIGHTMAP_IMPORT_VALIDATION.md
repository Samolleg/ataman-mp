# Heightmap import validation

This increment adds the format-neutral import core. PNG/TIFF/RAW decoding,
orientation choices, and file UI remain separate adapters so the document model
never guesses image semantics or map metadata.

## Import contract

- Input is an explicit row-major grid of finite normalized samples from 0 to 1.
- Callers explicitly choose the raw MPR height range, strength, and placement.
- `StretchToTerrain` uses deterministic bilinear sampling over the complete
  canonical vertex lattice.
- `OneSamplePerVertex` places samples at a chosen canonical vertex origin and
  safely crops samples outside the map.
- Inverted raw ranges are permitted intentionally; invalid samples and settings
  fail closed.
- Shared sector seams produce one canonical delta and update every owning SEC.
- Values outside the 16-bit raw range are marked as saturated and retain the
  existing explicit maxZ/clamping decision gate.
- The result is a normal undoable height command; localized normal rebuilding,
  staging, and fresh-reopen validation remain unchanged.

## Synthetic validation

The document suite covers exact corner values, bilinear midpoint resampling,
shared-sector seams, one-sample placement, boundary cropping, undo/redo,
saturation rejection, and invalid normalized samples. The full suite passes 42
tests with no failures or skips.

Private copied-map regression continues to use in-memory edits only. Manual
testing is deferred until an image decoder and preview UI expose orientation,
range, and placement choices to the user.
