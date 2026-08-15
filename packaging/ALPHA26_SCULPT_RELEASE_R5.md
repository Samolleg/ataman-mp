# EI Mapper Endurance 0.3.0-alpha.26 — sculpt release responsiveness r5

## Changed

- Terrain sculpt release no longer clones and rewinds the complete canonical
  terrain merely to produce the existing steep-slope diagnostic.
- Terrain objects are reprojected only inside a conservative changed-height
  footprint, including displaced-cell sampling and out-of-map coordinate
  clamping, instead of reprojecting every object in every loaded MOB.
- The existing one-command stroke undo/redo transaction is unchanged.
- Overlay-sculpt clicks that miss all active overlay surfaces now consume the
  complete drag gesture. They no longer fall through to object box selection.
- Workflow diagnostics now report release timing separately for reconciliation,
  diagnostics, undo insertion, object reprojection, and dirty/shadow state.

## Focused manual checks

1. On a populated map, perform short and long terrain strokes. Release should
   no longer produce the prior large hitch; nearby terrain-bound objects must
   still follow the edited surface.
2. Undo and redo terrain strokes. Confirm geometry and nearby object placement
   return together and each stroke remains one history entry.
3. In Overlay Sculpt mode, drag outside level bounds and over land with no
   active overlay. Nothing should be selected and no selection rectangle should
   appear.
4. Sculpt an active overlay and confirm normal overlay editing and undo/redo.

If a release hitch remains, preserve `logs/workflow.log`; the new
`Sculpt release timing` line identifies the remaining phase without changing
the transaction.
