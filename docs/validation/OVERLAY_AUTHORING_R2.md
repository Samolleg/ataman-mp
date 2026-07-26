# Overlay authoring R2 validation

This increment repairs the live renderer path used by lossless overlay-surface
authoring and adds semantic-atlas onboarding. It does not change the staged MPR
save contract, create atlas content, or add liquid-material records.

## Automated evidence

- Qt 5.12.12 / MinGW 7.3 release build: pass.
- `endurance_document_tests`: 70 passed, 0 failed, 3 expected private-fixture
  skips.
- `--ui-widget-check` with the offscreen Qt platform: pass.
- `git diff --check`: pass.

The document suite covers type-0 to type-3 SEC activation, the verified `-1`
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
5. Open Landscape Tools for a map whose decoded atlas has no approved profile.
   The editor must offer to generate a review proposal once for that atlas in
   the current session. Declining must leave manual tile painting available.
6. In Zone 5_1 atlas review, select an ambiguous slot such as slot 65. If its
   proposed family and four corner labels are correct, **Mark selected
   Reviewed** must clear the blocker without requiring a false split or merge.
   If the assignment is wrong, split or merge it deliberately. Save draft must
   remain available throughout.
7. Save As and reopen the changed MPR. Confirm overlay coverage, tile, material,
   and sculpted height survive fresh reopen, then run Test Current Map from the
   isolated workspace.

The manual atlas guide is `docs/TERRAIN_ATLAS_REVIEW.md` and is also summarized
inside the review dialog through **How to review...**.
