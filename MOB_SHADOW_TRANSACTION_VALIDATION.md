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

## Remaining gate

Real UI operations must now produce representative shadow logs. Leaf-only property,
position, world-setting, diplomacy, and script edits can then be reviewed by field
class. Structural operations require explicit raw subtree transactions and remain
out of scope for the bridge. Dirty-save output stays disabled until this evidence
is complete and the accepted transaction is connected to atomic staged saving.
