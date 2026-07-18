# 0.2.0-alpha.8 logic-point validation

This build supersedes alpha.7 for patrol, look, and trap-point testing.

## Attached-file evidence

The supplied `zone1test5.mob` was inspected read-only and round-tripped through
the Endurance document layer.

- Unit NID 224 contains five distinct `GUARD_PT` records. Its selected patrol
  group is now rendered after unselected groups, and a selected patrol/look
  marker is rendered last within that group.
- Unit NID 1038 is configured with Radius behavior but already contains one
  `GUARD_PT` and one `ACTION_PT`. Pressing P now changes the unit to Path and
  reuses that retained patrol point instead of producing a second one.
- Creating a look point from a selected look point copies `WAIT_SEG`,
  `TURN_SPEED`, and flags as well as position. Undo/redo uses the same path.
- Logic-point selections are counted by operation availability, so G/Move is
  available for patrol points, look points, trap activation zones, and trap
  cast points. Normal-object duplicate remains restricted to normal objects.

## Automated checks

- MinGW release build: pass.
- `--validate zone1test5.mob`: pass.
- `--roundtrip-check zone1test5.mob`: pass; source remains unchanged.
- `--ui-widget-check`: pass.

## Manual acceptance checklist

1. In Logic mode, select each patrol marker belonging to NID 224. Every
   selected marker must remain visibly highlighted before and after moving the
   unit and after undo/redo.
2. Select a look marker with non-default watch time and turn speed, press L,
   place it, and confirm the new marker inherited both values.
3. Select NID 1038 and press P. Exactly one patrol marker must appear. Undo must
   restore Radius behavior without deleting the retained raw point; redo must
   reveal the same point again.
4. Select patrol, look, trap activation, and trap cast points in turn and press
   G. Each must enter Move mode and produce one undoable move when confirmed.
