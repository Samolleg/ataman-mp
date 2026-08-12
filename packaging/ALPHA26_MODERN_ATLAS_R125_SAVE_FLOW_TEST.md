# Modern terrain atlas R125 save/finalize flow test

This build replaces the portable-folder finalization workflow with direct,
validated publication and adds non-finalized modern-paint draft saving.

## Changed

- Finalize asks for the destination `textures.res`, updates that exact archive
  in place, and offers either a verified backup or explicit no-backup mode.
- Complete RES construction/reopen runs on a worker behind a visible
  indeterminate progress dialog; the editor no longer appears frozen while a
  large archive is rebuilt.
- Finalize then either overwrites the current MPR or performs a normal Save As.
  Both choices become the open baseline in the existing session; the crashing
  `Open finalized MPR now` reload path and package-folder prompt were removed.
- Finalization clears landscape undo history because it changes external
  resource identity and physical atlas ownership.
- Ordinary Save and Save As can now preserve unfinished modern painting without
  compacting/publishing it. The MPR is paired with `<map>-atlas.res` and an
  editor-only semantic profile, and exact packed tiles survive reopen.
- The game-facing `textures.res` contains only MMP pages. No JSON manifest or
  semantic profile is embedded in it.
- Complete MPR and RES candidates are staged, freshly reopened, race-checked,
  backed up when requested, and atomically replaced. A later profile/MPR
  failure attempts exact rollback of the just-published RES, prior editor
  profile, and MPR destination.

## Focused manual checks

1. Paint and sculpt, then use ordinary Save. Confirm `<map>-atlas.res` appears
   beside the MPR and reopening preserves the exact unfinished painted layout.
2. Finalize into a disposable copy of `textures.res`. Confirm a visible progress
   dialog remains responsive and no package-folder chooser appears.
3. Choose `Overwrite current MPR`; confirm the map remains open with no reload
   prompt, the title is clean, painting can continue, and Undo is empty.
4. Repeat with `Save MPR as...`; confirm the chosen MPR name becomes the open
   document automatically and includes all sculpting made before finalization.
5. Repeat both with and without the optional RES backup. Verify the selected
   `textures.res` itself changes and no nested output folder is created.

## Known boundary

Publication changes two separate files. Both are fully staged first; the RES is
committed before the MPR so a map is never pointed at missing resources. If a
later commit fails, Endurance restores the exact pre-publication RES/profile/MPR
bytes when those outputs have not been externally changed. Rotation-equivalent
physical deduplication remains disabled by prior user decision.
