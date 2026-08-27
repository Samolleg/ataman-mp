# Alpha 26 terrain paint modes R180: portable draft and R162 semantics

This focused build makes the explicit modern-draft export usable even when the
MPR has no pending terrain edits, simplifies the terrain-paint UI, and updates
the built-in reviewed material catalog through semantic R162.

## Changes

- **Save MPR as modern draft...** now exports a validated portable MPR plus its
  adjacent map-local `-atlas.res` even when the staged MPR transaction is empty.
- An empty transaction is accepted only for that explicit draft action and only
  while a valid working atlas exists. Real transaction or validation errors
  still fail closed without changing the destination.
- The publication dialog labels **Create a new content identity** as the
  recommended, safest option and explains that it avoids conflicts with
  existing map atlases.
- The large terrain-paint instruction block is removed. Mode explanations now
  live on the Modern, Classic Auto, and Manual tab tooltips; Z/X/C, Alt+Wheel,
  and Shift+R are shown beside the controls they affect.
- The reviewed terrain catalog is synchronized through semantic R162 (preset
  SHA-256 `1E0609597348F6405BC2AF04870773C036DB779A1D74FD3D1AE957D2ED4E9DF3`).

Normal Save/Save As publication behavior, fail-closed staged validation, and
installed RES archives are otherwise unchanged.

## Focused test

1. Open a finalized MPR and make no edits.
2. Choose **Save MPR as modern draft...** and select a new MPR name.
3. Confirm the new MPR and adjacent `<name>-atlas.res` are created together,
   while the source MPR and installed `textures.res` remain unchanged.
4. Reopen the new draft, paint with Modern mode, undo/redo, save the draft
   again, and confirm it remains editable with no missing/cyan terrain cells.
5. Open the publication identity dialog. Confirm **Create a new content
   identity (recommended)** is preselected and its conflict-safety explanation
   is visible.
6. Check the terrain-paint panel: the prose block is gone, the three mode tabs
   have tooltips, and the visible shortcut labels match their actions.

Do not overwrite production maps or RES archives during this focused pass.
