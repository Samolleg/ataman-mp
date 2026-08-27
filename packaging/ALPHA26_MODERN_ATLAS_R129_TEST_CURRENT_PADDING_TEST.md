# Alpha 26 modern-atlas R129 Test Current padding fix

This revision fixes transient staging after modern painting creates atlas
extension tiles. It does not change material identities, authored masks, or
final atlas publication.

## Changes

- Separated the MPR's logical terrain-slot count from the complete 64-tile MMP
  pages required for texture storage.
- **Test Current Map** and **Save Editable Modern Draft As** now omit unused
  end-of-page positions from the logical terrain-type table instead of
  rejecting the first padding position as an unreviewed tile.
- Kept strict validation for every real logical slot. A genuinely allocated
  tile without a reviewed runtime terrain type is still refused.
- Kept full authored pages byte-for-byte and retained complete generated MMP
  pages, including their unused physical padding pixels.

## Focused manual test

1. Open a clean Zone2 MPR and paint any modern material that creates at least
   one working-atlas extension tile.
2. Without finalizing or saving, choose **Test Current Map**.
3. Confirm staging completes without `terrain.working_type_unknown`; inspect the
   temporary result in game as usual.
4. Return to the editor, paint another patch, and repeat **Test Current Map** to
   confirm transient staging remains reusable.
5. Optionally use **Save Editable Modern Draft As** and confirm its paired MPR
   and atlas reopen in the editor.

## Known limitations

- Rotation-equivalent physical deduplication remains disabled by decision.
- This build does not change the R123 semantic catalog or approved blend masks.
