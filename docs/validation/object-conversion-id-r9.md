# Object identity during lever conversion — r9

EI-ADD-FROM-DB-001-R10: the reported newly added dagger displayed NID 0 after
conversion to a lever. Read-only workflow evidence shows positive allocation
before conversion. JSON constructors deliberately leave identity allocation to
creation; the conversion command failed to transfer the existing object's NID.

The command now retains that NID. Allocation for fresh objects is unchanged.
The real-command test also exposed missing raw provenance on redo after saving
each side of a conversion. Saving rebinds an active node and clears its template;
before removing it, the command now retains its current raw source/clean pair.
This lets subsequent undo/redo saves restore its exact structure through the
existing guarded candidate pipeline, without relaxing provenance checks.

## Reproduction and validation

The database preview diagnostic uses the actual picker, pointer placement,
JSON-created lever and conversion undo command. It checks the same positive ID
in the live node and reopened document through conversion, save, undo, save,
redo, save and undo. It repeats from an explicitly saved world-object baseline.
Before the identity fix the live replacement has ID 0; with only that fix,
redo after saving both sides fails `mob.object_sequence.ui_provenance`.
With both fixes the complete sequence passes.

- JMV desktop OpenGL diagnostic passes on the final release executable, including
  textured previews, exact map material lookup, pointer placement, cancellation,
  conversion and save/reopen appearance checks.
- Base diagnostic passes with synthetic map textures: it verifies binding and
  persistence, not the appearance of author-supplied textures in the game.
- `--database-creation-check` passes with Qt offscreen: creatures, ordinary
  items and levers each receive distinct positive IDs; real creation-command
  undo/save/redo/save and source-hash rejection remain clean. Qt minimal is not
  a working backend for this diagnostic on the local kit.
- Release editor and both current runtime components build. The runtime helpers
  use the installed Build Tools because the wrapper names an absent VS edition.
- The broad `--ui-widget-check` still exits with its known `0xC0000005` failure.
  This is an open regression gap, not a passed check.
- Separate unpacked r9 staging and delivery folders pass distribution validation.
  Executable SHA-256:
  `6f333e81e6de4226996556a9957b8d80d3fc2c9c0ebdf39157a1d89ffe2a1e35`.
  The finalized package was not launched. Previous tester folders are preserved.

## Acceptance and limits

Add an item on a test map copy, record its ID, convert it to a lever, and check
that ID through undo/redo, saving between steps and reopening. Repeat for an
object saved before conversion. Actual interactive acceptance on the user's
original map is pending. Already saved zero IDs are not automatically migrated.
No installed maps or assets were modified or included in the repository/package.
No low-confidence implementation choices remain; the manual acceptance and broad
UI diagnostic gaps above remain explicit. Parent database visual coverage remains
open, and r8's exact HD atlas approvals are unchanged.
