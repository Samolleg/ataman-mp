# Alpha 26 modern-atlas R131 reopened-draft publication fix test

This revision fixes **Save MPR** and **Save MPR As** after an editable modern
draft has been closed and reopened. A reopened draft now retains its working
atlas status and must pass through the complete publication flow instead of
being copied as an ordinary standalone MPR.

## Changes

- Working-atlas profiles now have a distinct persisted provenance marker. The
  editor recognizes that marker after reopening and enables normal MPR saving
  even when the document has no new edits.
- Legacy editable drafts are recognized conservatively through their exact
  adjacent `<MPR name>-atlas.res` companion, including drafts created by R130.
- **Save MPR As** from a reopened draft now opens the material-resource review,
  publishes the required atlas into the selected `textures.res`, and writes a
  self-consistent destination MPR.
- Reopened drafts reconstruct the semantic material field from the stored
  working profile before finalization and preserve active Overlay cells while
  publishing Ground.
- Reopened working drafts cannot silently reuse a finalized identity merely
  because the destination is the current path; identity selection remains an
  explicit publication decision.

## Focused manual test

1. Open an existing editable modern draft together with its adjacent
   `<name>-atlas.res` and stored semantic-atlas profile.
2. Confirm **Save MPR** is enabled before making another edit.
3. Choose **Save MPR As** and select a disposable MPR copy as the destination.
   Confirm the modern publication review appears and names both the exact MPR
   target and `textures.res` target before any file is changed.
4. Cancel once and verify the destination is unchanged. Repeat and complete the
   save with the RES backup option enabled.
5. Reopen the published MPR, paint once, and save it again. It must remain
   editable and must not fall back to a plain draft copy.
6. Create a fresh editable draft, close/reopen it, and repeat **Save MPR As** to
   verify the new persisted working marker follows the same path.

## Known limitations

- This build does not automatically restore an original MPR that an older build
  already overwrote. Restore the user's backup or publish from the intact draft
  into a disposable test copy.
- Rotation-equivalent physical deduplication remains disabled by decision.
- This build does not change the current semantic catalog or approved masks.
