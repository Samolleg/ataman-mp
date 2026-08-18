# Alpha 26 modern-atlas R127 save/recovery test

This revision replaces the split draft/finalize workflow with one normal MPR
save path and makes the exact R125-damaged finalized Zone1 map editable again.

## Changes

- **Save MPR** publishes modern terrain edits into the current MPR and its
  resolved `textures.res`; **Save MPR As** publishes to the MPR name already
  selected by the user. Neither path asks for a second MPR destination.
- Already-published maps update their existing terrain resource identity by
  default. MPR Settings can stage another identity for the next Save without
  exposing physical atlas member names.
- The redundant Landscape Tools and MPR Settings finalization buttons are
  removed. Landscape Tools instead offers **Save editable modern draft as...**
  for the exceptional unfinished/map-local workflow.
- Successful publication adopts the validated MPR, physical atlas, and editor
  profile in the current session, clears dirty/undo state, and needs no reload.
- The exact incomplete profile produced for the user's R125 Zone1 MPR is
  recognized by both MPR and profile SHA-256 and repaired in memory. Arbitrary
  profiles are never guessed or rewritten. The complete profile is persisted
  only as part of a successful validated Save.
- The actual archive supplying every terrain page is remembered on map load,
  including configured `textures.res` files outside the MPR directory.
- Repetitive Qt 5.12 platform/accessibility warning categories are suppressed;
  application warnings and errors remain visible.

## Focused manual test

1. Open the same already-finalized R125 `zone1.mpr` that previously reported
   an undecodable cell. Confirm modern painting is available.
2. Paint a small patch and use **File > Save MPR**. Confirm no second MPR path
   prompt appears. Choose whether to back up `textures.res` when asked.
3. Confirm the save succeeds, the same MPR remains open and clean, and no
   reopen/save-again error appears.
4. Paint again and repeat **Save MPR** to prove the healed finalized map remains
   editable.
5. Paint again, choose **Save MPR As**, select one new MPR path, and confirm the
   publication flow does not ask for another MPR destination.
6. Use **Save editable modern draft as...** only to verify the separately named
   temporary draft workflow still exists.

## Known limitations

- Rotation-equivalent physical deduplication remains disabled by decision.
- The R125 migration is intentionally limited to the exact known damaged MPR
  and profile hashes. Other incomplete profiles still fail closed.
