# Alpha 26 terrain paint modes R164 test

This revision makes Manual painting, later Modern painting, and Shift+R share
one exact persisted semantic field without storing placement order or giving
Manual tiles special ownership.

## Changes

- **Manual is exact and transactional.** An assigned Manual tile writes its
  selected physical slot, rotation, and reviewed NW/NE/SE/SW semantics in one
  undoable command. A later Manual placement that touches the same vertex is
  simply the later explicit edit; no timestamp, origin flag, or hidden
  predecessor terrain is stored.
- **The final semantic field survives save/reopen.** Editor metadata is stored
  in the selected RES archive and bound to the exact MPR, atlas pixels, profile,
  and dimensions. Normal MPR Save patches this small metadata member with a
  backup and rolls the RES change back if the MPR commit fails.
- **Modern and Shift+R continue from Manual results.** They consume the final
  persisted shared-corner values, so a Manual uniform or authored blend cannot
  be outvoted by the terrain that existed before it. Undo and redo restore both
  the packed cells and the semantic field exactly.
- **Unassigned live artwork fails visibly.** A physical artwork slot referenced
  by the map but lacking semantics blocks Modern/Shift+R and opens Material
  Atlas on that slot. Null or unused atlas capacity never triggers this path.
- **Shift+R is paint-tool scoped.** The shortcut and action run only while the
  terrain-paint interface is visible and active.
- **Atlas holes are reused safely.** Generated tiles fill available logical
  capacity before extending pages. Padding and untyped holes do not appear as
  Manual tiles, and the selected physical tile remains selected across a
  Shift+R refresh, including when identical artwork exists in another slot.
- **Resolve is auditable.** Each run writes a bounded structured journal under
  `diagnostics/terrain-resolve/`, including trigger, field hashes, packed cell
  deltas, signatures, runtime types, provenance, findings, and undo state.
- **Held-RMB refreshes are coalesced safely.** Loaded-map tree rebuilding is
  deferred past the active item callback, avoiding the reported use-after-free
  crash while sampling across the map.

## Focused manual checks

1. On a clean Zone2 load, place one assigned Manual uniform inside a different
   3x3 material area and press Shift+R. Confirm the placed material remains the
   semantic center and the transition resolves outward rather than collapsing.
2. Place two touching Manual authored tiles with conflicting shared corners.
   Confirm the later placement wins, undo/redo is exact, and save/reopen keeps
   the final field without remembering placement order.
3. Reproduce the earlier Manual -> Resolve -> undo -> Modern sequence. Confirm
   Modern still produces a proper edge and RMB sampling never resurrects stale
   pre-undo provenance.
4. Keep a Manual tile selected, run Shift+R, and confirm the same physical slot
   remains selected and no blank padding buttons appear.
5. Hold RMB and move across terrain long enough to refresh map state. Confirm
   there is no crash and selection changes only when the sampled cell changes.
6. Inspect `diagnostics/terrain-resolve/<namespace>-resolve-sessions.json` after
   Shift+R and confirm the session contains before/after hashes and cell deltas.
7. Leave the terrain paint tool, press Shift+R, and confirm it is ignored.

## Automated validation

- Document suite: 119 passed, 0 failed, 6 expected skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape Tools widget suite: 5 passed, 0 failed.
- Resolve audit suite: 4 passed, 0 failed.
- Checked Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Known diagnostic boundary

The current resolver exposes the total disputed-vertex count but not individual
coordinates for every preserved ambiguous vertex. The structured journal
therefore records that aggregate and the concrete cells that were actually
changed; it does not invent per-vertex samples.
