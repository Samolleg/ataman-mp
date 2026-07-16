# Alpha.14 tester Zone 5 validation

Date: 2026-07-17

This report records the first external alpha.14 changed-save investigation.
Only hashes and generalized findings are committed. The tester's maps, resource
archives, Lua, workflow logs, and installation paths remain outside Git.

## Evidence identity

| Evidence | SHA-256 |
|---|---|
| Supplied pre-test `zone5_1.mob.bak` | `1c4c08c8d7453c001149965cbce7d68b9c1cad00a2686eeacd50a5a6bc6e8026` |
| Supplied final `zone5_1.mob` | `0f5f9166b1e21ee9d4ceaa371137913b9305d3e2146b47ef259bcba83e0817c1` |
| Supplied `database.res` | `2f1666ba534ce0dd451f8fcb2b371a09efc17c6abfdde810e05a396423846429` |
| Supplied `tables.lua` | `1c528a04c1641cf2570bee6077de34d46a6ac9447751c32b8cbf8e117a22c6a5` |

The final MOB hash and size (514,302 bytes) exactly match the last staged save
recorded in the supplied workflow log. The supplied backup is not byte-identical
to any logged open/save snapshot, although its size is close to the 507,385-byte
snapshot opened before the tester's large delete/recreate sequence. It is treated
as the supplied comparison baseline, not claimed as the exact original for every
logged action.

The local JMV installation is also not the tester's baseline. Its installed MOB
contains 62 MobExplorer objects, while the supplied backup contains 123. The
supplied database and Lua hashes differ from the local JMV installation. Therefore
the backup-to-final comparison is the only defensible attribution window.

## Results

- Endurance strictly parsed every byte of both files and produced byte-identical
  no-op round trips: 507,383 bytes/123 records before and 514,302 bytes/157 records
  after.
- The fingerprinted MobExplorer build accepted both files and its no-op save
  returned each file byte-for-byte unchanged.
- MobExplorer found 79 pre-existing object subtrees byte-identical between the
  supplied backup and final file.
- The object branch contains 35 additions, one removal, and 43 NID-paired changes.
  Thirty-nine paired changes are explained by the workflow log's explicit deletion
  of old static objects followed by creation of different objects reusing those
  numeric IDs. They must be interpreted as replacements, not serializer-wide
  modification of the old objects.
- The remaining four paired edits affect unit IDs 10, 1000031, 1000033, and
  1000035. Two change unit prototypes and two change unit logic. The supplied
  database and Lua contain the new `UROrcMaleF8`, `UROrcMaleF13`, and
  `UROrcFemaleA6` prototypes.
- Five additions that initially lacked normal object identity are valid light
  records with `LIGHT_ID` values 1000060 and 1000068 through 1000071.
- The complete script section and AI graph are byte-identical.
- One unwanted legacy-serializer change is confirmed: empty `DIRICTORY` and
  `DIRICTORY_ELEMENTS` sections present in the backup are absent from the final
  file. This is structurally tolerated by the tested file, but violates Endurance's
  lossless-save contract.
- No crash or fatal entry appears in the supplied logs. Repeated empty-model and
  empty-texture warnings coincide with creation of placeholder/new objects and do
  not identify a resource-catalog failure in this evidence set.

## Disposition

The alpha.14 legacy changed serializer remains unsuitable for lossless saves.
Current copy-only builds already refuse that path. Raw transactions preserve empty
containers byte-for-byte, and a dedicated regression now covers the exact two
section types exposed by this report. Transaction candidates are also freshly
parsed and independently checked so that every leaf outside the accepted edit is
byte-identical before output can be accepted.

The unexplained 40-byte difference between one logged save and the next reopened
snapshot cannot be attributed without the missing intermediate file. Future tester
packages should include the exact pre-test backup plus every Endurance pre-save
backup hash used for a report.
