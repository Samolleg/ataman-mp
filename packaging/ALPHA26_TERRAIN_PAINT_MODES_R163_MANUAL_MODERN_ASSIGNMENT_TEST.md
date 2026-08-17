# Alpha 26 terrain paint modes R163 test

This revision makes Manual painting an ordinary physical MPR edit while
keeping the transition into Modern painting explicit and recoverable.

## Changes

- **Manual no longer starts a Modern/finalization session.** On a map without
  active Modern edits, Manual writes only the chosen physical slot and
  rotation. Normal Save/Save As remains available; neither Shift+R nor atlas
  finalization is required merely because Manual was used.
- **Modern resumes from the post-Manual map.** Any inactive semantic cache is
  discarded when Manual writes a tile. The first later Modern stroke rebuilds
  its shared-corner field from the physical cells that now exist, so it cannot
  resurrect terrain that was present before the Manual edit.
- **Only live unassigned artwork blocks Modern.** Before a Modern stroke or
  Shift+R, the editor scans physical slots actually referenced by that MPR
  layer. Unused atlas capacity and null page-padding positions are ignored.
- **Unassigned artwork routes to Material Atlas.** If a referenced slot has a
  real loaded image but no semantic assignment, painting is not started and
  Material Atlas opens focused on the first offending slot. If the referenced
  image itself is absent, the editor instead identifies the slot and cell and
  asks for the matching RES archive.
- **Assignment prompts are gesture-safe.** Only one deferred warning/dialog
  may be pending, preventing held or repeated mouse events from opening a
  stack of modal windows.
- **Manual status text is accurate.** Catalog-aware physical Manual commands
  now use the Manual-specific completion message.

## Focused manual checks

1. Open an untouched known map, use **Manual** to place an assigned uniform or
   blend tile, and choose **Save MPR**. Confirm it saves normally without
   Shift+R or atlas finalization.
2. Reopen that MPR and paint across the Manual result with **Modern**. Confirm
   Modern starts normally and no pre-Manual terrain is restored. Undo/redo
   must reproduce the exact physical tiles and rotations.
3. Place a real atlas slot that is unassigned in Material Atlas, then attempt
   a Modern stroke. Confirm the stroke is not applied, the warning identifies
   the physical slot/cell, and Material Atlas opens on that slot. Assign it,
   retry, and confirm Modern can continue.
4. Open a paged atlas with unused/null tail positions. Confirm those unused
   positions alone never trigger the assignment warning.
5. Press **Shift+R** on a map containing referenced unassigned artwork. Confirm
   it routes to the same assignment flow rather than silently replacing the
   tile or failing in the resolver.

## Safety boundary

Material Atlas remains locked while unsaved Modern semantic edits or generated
working slots are active, because changing the profile would invalidate their
undo and atlas references. If an unassigned Manual slot is introduced during
such a session, the editor identifies it and asks the user to undo that one
placement or finish the active Modern publication before assigning it.

## Automated validation

- Document suite: 116 passed, 0 failed, 6 expected skips.
- Virtual-atlas suite: 111 passed, 0 failed.
- Landscape Tools widget suite: 3 passed, 0 failed.
- Checked Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.
