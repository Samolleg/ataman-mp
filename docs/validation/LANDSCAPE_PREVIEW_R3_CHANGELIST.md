# Landscape Preview R3 changelist

R3 is a focused correction build based on the R2 manual-test report. Changed MPR
saving remains disabled.

## Corrected

- Leaving land/overlay painting for Sculpt now clears the inherited atlas-tile
  cursor as well as the tile preview.
- Undo and Redo no longer cancel the persistent paint tool. The first Undo now
  reverts the most recent paint stroke immediately and painting remains active.
  This applies equally to the menu action and the configured keyboard shortcut.
- Live sculpt and heightmap previews now rebuild the affected SEC vertex normals
  before uploading sectors to the renderer. Lighting therefore follows the new
  terrain geometry immediately.
- Undo restores authoritative source normal bytes when the surrounding geometry
  returns to its original state. It does not silently normalize untouched fixture
  data, and a fully undone edit returns the SEC payload byte-for-byte.
- A height edit whose preview normal would depend on a pre-existing conflicting
  sector seam now fails before mutating the preview.

## Verified from the supplied R2 artifacts

- `zone1.mpr`, `zone1test.mpr`, and `zone1test_copy.mpr` were read-only compared.
  All three are 1,523,282 bytes and have SHA-256
  `A1ED4AC4F39E4DA49BB29B3C80366DB747BE9F3A318726EAA2DB9D1C1B150AAE`.
- The workflow log records an unchanged same-path MPR no-op and the expected
  refusal of a changed MPR save. No crash was recorded.

## Still intentionally provisional

- Land and overlay paint still use the inherited direct selected-tile brush as a
  renderer/UI bridge. Automatic terrain-type transitions, natural variant
  distribution, and seamless corner selection are the next production painting
  increment; R3 does not present the old brush as the finished painting design.
- Overlay painting changes only existing overlay cells. Creation and material
  editing remain separate future increments.
- Changed MPR persistence remains disabled.

