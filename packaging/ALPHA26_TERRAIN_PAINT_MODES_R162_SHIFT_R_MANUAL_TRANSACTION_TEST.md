# Alpha 26 terrain paint modes R162 test

This build focuses on the reported first-load resolver, Manual paint, undo,
and Manual-palette regressions. It retains the R161 runtime-type-preserving
Shift+R policy and the current reviewed semantic atlas catalog.

## Changes

- **Resolve missing blends now works on the first Shift+R after loading.** The
  resolver establishes its deterministic semantic field only in an isolated
  staging copy before scanning. A failed or no-op repair cannot mutate the
  loaded map or working atlas. The reported Zone2-shaped regression covers
  cells `(28,17)` and `(29,17)` and verifies the first invocation matches the
  result after an unrelated Modern paint.
- **Manual tiles no longer resurrect the terrain previously underneath.** A
  Manual command updates the affected shared-corner semantic field from the
  post-paint physical tiles. Unanimous decoded corners are retained; disputed
  or unassigned corners become unknown. Manual tiles receive no special
  ownership or preservation privilege, and a later explicit Shift+R may repair
  them under the same rules as every other physical tile.
- **Manual paint remains direct.** It is not passed through Modern's automatic
  end-of-stroke generation step. Physical and semantic deltas are captured in
  the same undo command, so undo/redo restore the exact before/after state.
- **Interrupted strokes cannot create unowned changes.** Delayed mouse events
  after a modal/focus cancellation are rejected unless a live terrain-stroke
  transaction still owns them.
- **The Manual palette no longer contains blank page-padding rows.** Null atlas
  holes are omitted while real physical slot numbers, filtering, selection,
  and the physical slot count remain intact.

## Focused manual checks

1. Open the current Zone2 MPR and press **Shift+R immediately**, before making
   any paint stroke. Confirm missing blends are repaired in one pass and one
   Undo restores the exact opened map.
2. In **Manual**, paint both a uniform tile and a transition tile, then use
   Shift+R. Confirm no old hidden terrain is restored merely because the tile
   was placed manually. Undo/redo each action and confirm the exact tile and
   rotation return.
3. Begin a Modern stroke, invoke and cancel a modal screen-capture interaction,
   then continue painting. Confirm every visible change belongs to an undoable
   terrain-stroke entry.
4. Open **Manual** on an expanded atlas and scroll through the physical tiles.
   Confirm there is no large blank block between authored and generated slots,
   and filtering/selecting a generated slot still works.

## Known review boundary

The precise Windows Print Screen/focus event ordering is guarded by the live
transaction check and a headless source-contract/model rollback regression, but
the packaged editor was not interactively launched during validation.
