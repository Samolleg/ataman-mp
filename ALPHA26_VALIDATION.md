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

## Completed JMV evidence (2026-07-17)

The tester changed one unit's Player value and saved `zone1test2.mob` from
`zone1test1.mob`. Endurance logged one authorized `OBJ_PLAYER` leaf transaction
and a successful fresh staged validation. Independent comparison found:

- source SHA-256 `DB5F3AB08E8D6EA920096887B17BE498A1E81A315AB50C85754922150C6A4D34`;
- output SHA-256 `1DA45CA074ECE8DC07085E639CEED18432570B79EECA1ECC26AE1CB00C4F7BBD`;
- identical 1,586,436-byte file sizes and object counts;
- 1,692 unchanged objects and exactly one modified object, `WolfTutor-1364`
  at `/OBJECT_DB_FILE[0]/OBJECT_SECTION[0]/UNIT[90]`;
- exactly one changed byte at file offset `0x2E31A`, from Player 4 to Player 9.

A combined model/texture plus Player edit was refused as a whole, as was a
copy/paste structural edit. Neither unsupported candidate was serialized.
