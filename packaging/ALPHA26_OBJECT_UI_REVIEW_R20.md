# 0.3.0-alpha.26 Object UI review r20

## Changes

- Starting Paste or Duplicate while another placement is unfinished now
  explicitly cancels and removes the earlier preview before reading selection,
  allocating nodes, or replacing pending transaction ownership.
- Selecting a different destination in the outliner or collection cancels the
  unfinished placement first, then applies the requested selection. Repeated
  look-point paste therefore targets the newly selected patrol without silently
  committing or orphaning the first preview.
- Direct Logic-slot paste follows the same cancellation boundary.
- Every completed paste/duplicate remains one atomic Undo entry. Redo restores
  that complete final action; cancelled previews never enter history.
- A visible floating History window is raised again after Undo/Redo, including
  multi-action progress and the first Undo that only cancels an active preview.
- Includes r19 collection scrolling, contextual shortcut help, and P/L
  selection restoration, plus current main's database-accurate helmet/hair
  preview resolution.

## Focused manual checks

1. Copy a look point, select patrol A, paste, and leave placement unfinished.
   Select patrol B in the outliner and paste again. Confirm the first preview is
   removed, the second appears under patrol B, and no click silently commits A.
2. Place the second paste, then Undo and Redo. Confirm one Undo removes the
   complete paste and one Redo restores it; no orphaned point remains.
3. Repeat step 1 with Ctrl+D on a unit and a world object. Confirm the unfinished
   duplicate is cancelled before the next duplicate begins, with one history
   entry for the final placed action.
4. Repeat with patrol points, activation areas, cast targets, and a Logic-slot
   context-menu paste.
5. Float the History window. Click earlier and later entries, then use Undo and
   Redo shortcuts. Confirm History remains in front instead of dropping behind
   the main editor.

## Known limitations

- Clicking inside the viewport still intentionally confirms placement; use Esc,
  a repeated action, or a new outliner/collection selection to cancel it.
