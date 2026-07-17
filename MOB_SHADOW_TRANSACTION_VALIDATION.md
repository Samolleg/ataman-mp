# MOB projection shadow-transaction validation

Date: 2026-07-17

Implementation: [`3106fac`](https://github.com/Ilufus/ei-mapper-endurance/commit/3106fac)

## Purpose

The legacy UI projection still supplies the editor's typed values, but it is no
longer trusted as output. The shadow bridge compares the clean projection captured
at open with the current edited projection and derives only leaf changes that can
be bound to the exact original raw-section hash.

This is diagnostic shadow mode. Dirty saves remain disabled and no shadow candidate
is written to disk.

## Fail-closed rules

- Source, clean projection, and edited projection must all pass strict complete-byte
  parsing.
- Clean and edited projections must have identical leaf topology. Create, delete,
  copy, list-size, and other structural changes are rejected.
- Every changed projection leaf must exist at the same source path, retain its
  numeric section type, and byte-match the clean source record exactly. Endurance
  does not guess through legacy canonicalization or occurrence shifts.
- Derived edits carry the original section SHA-256.
- Applying the transaction reparses the candidate and independently requires every
  leaf outside the accepted edits to remain byte-identical.
- Unknown records and source-only records, including empty `DIRICTORY` and
  `DIRICTORY_ELEMENTS` containers, remain in the raw source tree.

## Automated tests

The document suite covers:

- authorized leaf replacement and derived ancestor lengths;
- preservation of empty containers and opaque source-only records;
- structural-edit rejection;
- rejection when a changed projection record is absent from the source;
- rejection when a clean legacy leaf differs from its raw source leaf;
- stale source-hash rejection;
- strict CP1251 encoding; and
- fresh candidate reparse and unauthorized-leaf verification.

The dedicated corpus harness performed an in-memory mutation, projection-delta
derivation, and exact transaction reapplication for every currently installed MOB:

| Profile | Files | Passed | Failed |
|---|---:|---:|---:|
| Base EI Maps | 99 | 99 | 0 |
| JMV Maps | 155 | 155 | 0 |
| Total | 254 | 254 | 0 |

No map or installation file was modified. The harness and document tests returned
exit code 0, and the production MinGW build plus UI widget regression check passed.

## Real UI evidence

The first alpha.25 tester case changed one unit's diplomacy/player assignment from
4 to 12 and then attempted Save As. The structured workflow log identified exactly
one replacement leaf:

- object: `WolfTutor-1364` (NID 1364, prototype `WolfGreyF0F`);
- source path: `/OBJECT_DB_FILE[0]/OBJECT_SECTION[0]/UNIT[90]/0X0000B011[0]`;
- field: `OBJ_PLAYER`, byte value 4 -> 12;
- source MOB SHA-256: `DB5F3AB08E8D6EA920096887B17BE498A1E81A315AB50C85754922150C6A4D34`;
- shadow candidate SHA-256: `A970310DB4886A668A539EE2AA16CA29D094A10C6FF32EC344CD2F6228E71729`.

No output file was created by Endurance. The same edit was independently applied
with the fingerprinted MobExplorer/EILib build. Its complete output MOB had the
exact same candidate SHA-256. An independent object comparison reported 1 modified,
0 added, 0 removed, and 1692 unchanged objects; `OBJ_PLAYER` was the only changed
field. Endurance then strictly reparsed all 1,586,436 bytes and passed a
byte-identical unchanged round trip of that candidate.

This validates the `OBJ_PLAYER` byte-leaf edit class in shadow mode. It does not by
itself authorize changed output or validate unrelated property types.

## Remaining gate

Real UI operations must now produce representative shadow logs. Leaf-only property,
position, world-setting, diplomacy, and script edits can then be reviewed by field
class. Structural operations require explicit raw subtree transactions and remain
out of scope for the bridge. Dirty-save output stays disabled until this evidence
is complete and the accepted transaction is connected to atomic staged saving.
