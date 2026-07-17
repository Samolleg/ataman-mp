# Alpha.26 first changed-save validation

Use copied MOB files only. Do not overwrite a live Base EI or JMV map.

Alpha.26 enables changed output for exactly one independently validated field
class: the one-byte `OBJ_PLAYER` unit assignment. Every changed leaf in the
document must belong to that class. Other leaf classes and structural changes
remain wholly blocked; Endurance never writes a partial subset of an edit.

## Positive test: player assignment

1. Copy a representative MOB to a disposable test directory and record its
   SHA-256.
2. Open the copy, select one existing unit, and change only its Player value.
3. Use **Save As** to write a new disposable `.mob`.
4. Confirm that the output exists and the changed marker clears.
5. Reopen the output in a fresh Endurance process and in the fingerprinted
   MobExplorer build.

Expected workflow log entries:

- `MOB transaction candidate validated`;
- one `MOB transaction leaf edit` with type `0x0000b011`, replacement size 1,
  source hash, and replacement hash;
- `MOB transaction save authorized class:OBJ_PLAYER`;
- `Staged MOB validation passed`.

Compare the source and output. Require the same object count and NID set, exactly
one modified object, and only its `OBJ_PLAYER` field changed. If testing Save over
an existing disposable output, also require a timestamped pre-save backup whose
SHA-256 matches the file that was replaced.

## Negative controls

Run each control from a fresh source copy:

1. Change only a texture/model or position, then attempt Save As.
2. Copy/paste or delete an object, then attempt Save As.
3. Change Player and an unvalidated field together, then attempt Save As.

Expected: each save is refused, no destination is created or replaced, and the
log identifies an unvalidated transaction class or unsupported structural delta.
The mixed edit must be rejected as a whole.

## Return evidence

Send the clean-session `logs/workflow.log`, source/output hashes, MOB filename,
and a one-line description of the isolated action. Keep logs private because
they contain installation paths. Do not send game resources unless requested.
