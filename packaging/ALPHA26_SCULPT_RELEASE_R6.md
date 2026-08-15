# EI Mapper Endurance 0.3.0-alpha.26 — sculpt release responsiveness r6

## Changed

- Terrain and overlay history entries no longer trigger lossless MOB
  serialization/comparison. MPR-only commands cannot change authored MOB data;
  object reprojection and spatial-index invalidation remain intact.
- Publishing the MPR dirty marker updates only the loaded-map root label instead
  of rebuilding the full map tree and rescanning every normal and logic object.
- Repeated dirty notifications with an unchanged value no longer rebuild window
  and document controls.
- Terrain command aggregation, nearby-object reprojection, navigation preview
  invalidation, shadow invalidation, and one-entry-per-stroke undo/redo are
  unchanged from r5.

## Focused manual checks

1. Repeat short terrain and overlay strokes on the same populated map. Stroke
   release should no longer show the fixed 200–300 ms pause.
2. Confirm the MPR `*` dirty marker appears after the first change and disappears
   after a successful save.
3. Undo and redo several terrain and overlay strokes. Confirm each stroke is one
   history entry and nearby terrain-bound objects follow terrain undo/redo.
4. Make and undo a normal MOB object edit. Confirm its MOB dirty marker still
   updates; this optimization applies only to MPR-owned commands.

The existing `Sculpt release timing` workflow entry remains enabled. If a pause
remains, preserve the new log so the reduced `undoMs` and `stateMs` values can be
compared directly with r5.
