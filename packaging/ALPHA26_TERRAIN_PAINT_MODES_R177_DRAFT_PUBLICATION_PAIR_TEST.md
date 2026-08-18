# Alpha 26 terrain paint modes R177: Draft/publication pair test

This focused build prevents publication from rewriting an editable modern
draft MPR while leaving its adjacent working-atlas RES on a different slot
layout.

## Changes

- **Save MPR** on an editable modern draft now updates the current MPR and its
  adjacent `<name>-atlas.res` as one validated draft pair. It does not publish
  into a game RES.
- **Save MPR As** remains the publication path. The MPR selected in the first
  dialog is retained as the final destination and the review shows that path
  together with the selected game RES before either file is replaced.
- Publication is refused before mutation if its selected destination is the
  currently open editable draft. This prevents a compacted final MPR from
  being paired accidentally with the draft's uncompacted sidecar.
- Workflow logging now records whether Save or Save As was used and the exact
  source MPR, destination MPR, selected RES, identity, and replacement mode.
- Unload and exit prompts use the same safe Save routing instead of bypassing
  the editable-draft pair rule.

## Focused test

Use copies of a known-good editable draft MPR and its matching adjacent
`-atlas.res`; do not use a pair already damaged by an older build.

1. Open the draft, make a small landscape edit, and choose **Save MPR**.
   Confirm there is no game-RES selection prompt. Reopen the draft and verify
   that its terrain and modern painting remain correct.
2. Reopen the good draft, make a small edit, and choose **Save MPR As**. Select
   a different test MPR path, then select a disposable/test game RES. Confirm
   that the review displays those exact two destinations.
3. Complete publication. Reopen the original draft and the new finalized MPR
   independently. The draft must still use its adjacent sidecar; the finalized
   copy must use the selected game RES. Neither may show cyan/missing tiles.
4. Choose **Save MPR As** again but select the currently open draft path.
   Confirm publication is refused before a RES chooser or file mutation.

The old `zone1_aa27312831eb-modern-atlas-aa273128` folder is unrelated to this
test and is deliberately excluded. No semantic assignments, authored artwork,
or blend masks changed in this revision.
