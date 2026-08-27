# Overlay authoring R3 validation

This increment repairs the live renderer path used by lossless overlay-surface
authoring and adds semantic-atlas onboarding. It does not change the staged MPR
save contract, create atlas content, or add liquid-material records.

## Automated evidence

- Qt 5.12.12 / MinGW 7.3 release build: pass.
- `endurance_document_tests`: 71 passed, 0 failed, 3 expected private-fixture
  skips.
- `--ui-widget-check` with the offscreen Qt platform: pass.
- `git diff --check`: pass.

The document suite covers land-relative type-0 to type-3 SEC activation, the verified `-1`
inactive-material sentinel, exact deactivation, undo/redo provenance, active-only
overlay sculpt locality, independent overlay normals, sector-seam equality,
untouched archive members, staged reopen, and failed-save preservation.

## Manual viewport checks

Use only a copied map or isolated mod workspace.

1. On dry Zone 1 land, choose **Paint overlay tiles**, **Add surface**, a valid
   overlay family, and an existing liquid material. A size-1 click must display
   the new cell immediately at the outline.
2. Remove that cell. It must disappear immediately. Undo and redo must restore
   the exact visual state without changing land.
3. Activate adjacent cells, switch to **Sculpt overlay surfaces**, and exercise
   Raise, Lower, Flatten, and Smooth. The active surface must move visibly and
   one drag must create one history entry. Dry/inactive land must remain a
   no-op.
4. Paint a long radius-2 land stroke and a long radius-2 overlay stroke. Record
   FPS and whether the outline remains attached to the cursor. Each drag must
   remain one undo entry.
5. Open Landscape Tools for a map whose decoded atlas has no supplied profile.
   The editor must generate the material catalog once, make safe assignments
   immediately paintable, show assigned/unassigned coverage, and offer to open
   Material Atlas. Declining must not disable the safe palette or manual mode.
6. In Zone 5_1 Material Atlas, confirm unusual tiles appear in the visual
   unassigned pool. Drag a tile to a material, then drag material rows onto its
   four corner targets. A Ground/Overlay mix must be rejected inline without
   changing the tile. **Apply + Close** must save the usable catalog even while
   unusual tiles remain unassigned.
7. Save As and reopen the changed MPR. Confirm overlay coverage, tile, material,
   and sculpted height survive fresh reopen, then run Test Current Map from the
   isolated workspace.

The manual atlas guide is `docs/TERRAIN_ATLAS_REVIEW.md`. The review dialog
contains a compact workflow summary and no separate help button.
