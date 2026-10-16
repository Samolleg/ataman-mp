# EI-LEVER-CREATE-192 — missing lever prototype

Owner reported that Ataman191 refused the map lever `uhwbw02` (ID741) when its
prototype was absent from `levers.ldb`. This left the requested authoring workflow
incomplete. The owner requested immediate release and authorized coordination
with the parallel HD task: this correction is192; HD coverage moves to193.

## Behavior

- Missing model opens the same approach-point tool with a visible new-record form.
  New values start at scale1, switch time10, Stone material, empty sound and the
  object's display name as description. These values are editable; no other
  prototype's unknown fields or behavior are silently cloned.
- One/two points are placed with the existing runtime-correct inverse transform.
  «Создать в БД и применить» appends a complete eight-field record and its points
  in one shared history transaction. Escape/Cancel leaves the DB unchanged.
- Empty lever tables are supported. Existing records and unknown payloads retain
  their exact bytes. An exact model identity added concurrently rejects creation.
  Invalid names/encoding, non-finite coordinates and invalid count/scale/time fail
  before committing the transaction.
- A related serialization defect was exposed by Save→edit→Undo: a saved state was
  considered clean and incorrectly returned the immutable original archive.
  Only original state0 now takes that shortcut; saved edited states serialize their
  actual rows. Undo of creation after saving restores the original archive.

## Evidence

- Targeted QtTest8 PASS/0 FAIL/0 SKIP: creation in populated/empty tables,
  duplicate rejection, exact existing field encoding and unrelated RES members,
  shared Undo/Redo, save/reopen, subsequent one-point editing and Undo after save;
  existing point editing/no-op/atomic save/identity regressions.
- Hidden native41 PASS/exit0 against development production objects: existing
  workflow plus absent uhwbw02/741 with the reported orientation, new-record form,
  queued UI updates, cancel, two mouse placements, creation, live snapshot and
  one-step Undo/Redo. Panel capture inspected; no live database/map was written.
- An expanded screenshot check exposed an incomplete test harness: queued terrain
  initialization called CLandscapeTools::setPaintMaterials without that panel.
  Crash address mapped to that function. The harness now attaches the same panel
  as the real application and passes event processing; no game data repair occurred.

Private evidence: `build-lever-191-tests/create-results.txt` and
`build-lever-create-193-native/{native.stdout.txt,create-panel.png}`. The latter
directory retains the initial reserved number; it is a diagnostic folder, not a
distributed version. Final release/delivery receipts: `build-lever-create-192-20261016`.

Fresh scoped192 application and both runtime components built successfully.
The final native helper was compiled with the snapshot's headers and linked with
its production objects:41 PASS, exit0. The final executable reports192.
Staging and guarded installation receipts record exact source/binary identities.
Manual actual-window and in-game acceptance remain separate.

## Manual acceptance

Select the missing lever; open «Точки подхода»; place one/two points and cancel once.
Reopen, set fields/points, create, Undo/Redo, then save the DB and reopen. Verify the
new row and approach in the game. The DB row is shared by every instance of that
model; the example preview distance is not every character's interaction distance.
