# 0.3.0-alpha.1 Direct MPR Save Candidate

This candidate closes the remaining distribution blocker for the landscape
baseline: a changed open MPR can now be saved back to its current path without
using the legacy rebuilding serializer.

## Changelist

- enable direct Save for canonically owned changed terrain;
- show changed members, candidate SHA-256, semantic diff, backup behavior, and
  the navigation warning before an explicit save;
- verify the source has not changed before staging or replacement;
- create and hash-check a pre-save backup;
- atomically replace the MPR and then verify the committed bytes and fresh
  reopen;
- rebind the open MPR and terrain model to the committed clean baseline;
- preserve undo/redo across Save: undo becomes dirty, redo to the saved state
  becomes clean;
- avoid a second MPR confirmation after the user already chose Save during
  exit or unload;
- retain separate validated Save As copies and unchanged byte-identical no-op
  saves;
- keep every changed MPR unreachable from the legacy rebuilding serializer.

## Manual test

Use only a disposable copy of an authored MPR. Do not test against the live
game installation.

1. Sculpt a small change and choose **Save**. Accept the single validation
   prompt; confirm the dirty marker clears.
2. Reopen the MPR and confirm the change remains.
3. Make another change and Save. Undo once and confirm the terrain returns and
   the MPR becomes dirty; redo and confirm the saved terrain returns and the
   dirty marker clears.
4. Repeat direct Save and reopen with land painting and overlay painting.
5. With MPR and MOB loaded, change terrain and load another map or exit. Choose
   Save and confirm the MPR question is not repeated.
6. Check `workflow.log` for the committed SHA-256, changed member list, backup
   path, and `adopted:yes`.
7. Run Test Current Map on the disposable saved copy and check terrain,
   overlays, placement, and navigation.

Send the private workflow log and the saved disposable MPR only if a failure
needs diagnosis. The full safety contract is documented in
`MPR_DIRECT_SAVE_VALIDATION.md`.
