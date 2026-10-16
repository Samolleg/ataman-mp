# EI-LEVER-APPROACH-191 — точки подхода

## Scope and runtime contract

Owner request: investigate the runtime lookup/geometry first, then add the property
button and mouse placement of one or two use points. Read-only runtime reference
was explicitly authorized; no private source or game data is included here.
Reference provenance is retained in the private build evidence directory.

- The lever lookup uses the exact, case-sensitive model identifier serialized in
  MOB `OBJ_TEMPLATE`, not the display name, object ID, or parent template.
- `levers.ldb` field 1 stores four Float32 coordinates; field 2 selects the valid
  point count. Zero coordinates are valid, not an absence sentinel.
- The radial scale is `databaseScale * tall + (1 - tall)`. Only complexion Z/tall
  participates. The character's interaction distance is then added to the radial
  length. The stored orientation transforms the two local basis axes.
- A zero offset remains at the object origin. For two points, runtime pathfinding
  selects the reachable candidate with the lowest route cost. Door/state flags do
  not replace this coordinate calculation. A point marker is not proof of access.
- The tool uses the existing runtime quaternion expansion, including non-unit
  quaternions, and inverts its XY projection. Singular projections, nonpositive
  scales and clicks inside the selected interaction-distance exclusion are rejected.
- The preview distance defaults to an explicitly labelled example of 0.5; it is
  not saved and is not asserted to be every character's distance.

## Implementation

One editable lever exposes «Точки подхода». A modeless tool provides count, active
point, local numeric coordinates and preview distance. Left click/drag places the
active marker; camera mouse navigation remains available. Native GL lines, rings
and digits do not create MOB objects. Escape/Cancel clears the draft. Object,
selection or profile changes cancel; concurrent changes to relevant DB fields
reject Apply. No matching row, duplicate identity or malformed active coordinates
produce an explicit diagnostic; no row is silently created or selected by approximation.

Apply updates coordinates/count together in the existing shared DatabaseDocument,
publishes its snapshot and adds one command to the common Undo/Redo history.
Inactive second-point bytes are retained, including pre-existing non-finite data.
The existing DB save/backup/source-hash/atomic replacement path is reused.
All instances of the model share the change. Game databases are never written by
the automated tests or by merely opening/moving markers.

## Validation and remaining delivery

- QtTest targeted database regression: 7 PASS, 0 FAIL, 0 SKIP. Extended use-point
  case additionally covers inactive non-finite bytes, rejected counts, stale
  transaction, one-point edits, numeric row state and archive bytes after Undo,
  redo and source save/reopen. A shared-data Undo defect found during development
  was corrected by detaching tables/rows before mutation.
- Native final production-object check: 28 PASS, exit 0; hidden OpenGL, separate map copy and read-only
  active DB. Button signal, placement/drag, exact inverse XY, selection preservation,
  draft isolation, Escape, Apply, Undo/Redo, and absence of the row without selection.
  Separate FBO proves visible marker pixels and clean GL state; a hidden window
  back-buffer capture was unsuitable and is not used as visual evidence.
- After confirmed installation of 190, the fresh `build-release-191-lever`
  application and both runtime components built successfully; `--version` reports
  191. The native check was relinked against these exact final production objects.
  Version 190 is retained as the source baseline. Scoped commit, clean staging and
  guarded installation follow; receipts record the precise SHA and file identities.
  Manual real-window and in-game acceptance remain separate.

Private receipts: `build-lever-191-tests/lever-regression.txt`,
`build-lever-191-tests/lever-extended.txt`,
`build-lever-191-native/final.stdout.txt`, `panel.png`, `markers.png`;
final delivery receipts are in `build-lever-191-20261016`.

## Manual check

Select a lever with a matching DB record. Open the button, place one/two points,
rotate the camera, drag a marker, cancel once, then apply. Verify common Undo/Redo
and the values in the database editor; save the DB and reopen. Check another
instance of the same model. In the game verify approach from both sides with a
known character; the runtime may reject a geometrically valid but unreachable point.
