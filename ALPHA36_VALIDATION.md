# Alpha.36 script-workflow and clean first-run validation

Alpha.36 supersedes the withdrawn alpha.34 package and the unshared alpha.35
candidate. Use copied MOB files or Save As outputs.

## First startup

1. Extract into a new directory and confirm no `endurance-options.json` exists.
2. Start normally and confirm Game and Mod Setup appears.
3. Complete setup, restart, and confirm the saved local setup is reused.

## Script workflow

1. Open a MOB that has a previously used external workspace. Confirm MOB load
   does not display an **External script changed** question.
2. Open the internal script editor and confirm syntax colors are visible.
3. Click **Apply and Close** after a real edit. Confirm the window closes and one
   **Edit MOB script** undo entry appears.
4. Open the internal editor, make no semantic change, and vary only line endings
   or a UTF-8 BOM through an external editor. Confirm no undo entry or dirty MOB.
5. Make an internal change and click **Open External Editor**:
   - Save must commit, close, and open the external editor with the new text.
   - Discard must close and open the last committed MOB text.
   - Cancel must leave the internal editor open and launch nothing.
   - Confirm the external file contains the current embedded script, not the
     version that existed when the MOB was first opened.
   - Repeat with **Ctrl+Alt+M** and with a customized binding from
     **Settings > Key Bindings**.
   - In **Settings > General**, choose an editor executable and confirm it is
     used. Clear the field and confirm Windows file association is used again.
6. With the internal editor closed, save a real external change. Confirm one
   import question and, when accepted, one main undo-history entry.
7. Open the internal editor, then modify the managed file directly. Accept the
   external import and confirm the open internal editor refreshes to that text.
8. Open MOB Parameters and close it with the title-bar X. Confirm it follows
   the same Apply/Discard/Cancel behavior as its Close button and can reopen.
9. Undo/redo, Save As, reopen, and compare the intended script leaf against the
   source. Test the output in JMV.

## Multi-property model-parts undo

1. Select multiple property labels including **Model** and **Model parts**, copy
   them, then paste them onto a compatible object in one action.
2. Undo once. Confirm the original model and its exact original selected-part
   set return; it must not switch to all parts selected.
3. Redo once and confirm the copied model and exact copied part set return.

Also repeat the first-patrol, Ctrl+D, keybinding, and preservation checks from
`ALPHA34_VALIDATION.md`.
