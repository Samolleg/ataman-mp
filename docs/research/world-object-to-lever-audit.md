# World Object to Lever Structural Audit

Date: 2026-07-22

## Question

Can Endurance safely convert an arbitrary structurally valid world object into a
lever without requiring an existing lever that uses the same model?

## Corpus and method

This was a read-only structural scan through MobExplorer/EILib 1.1.0.0 of:

- Base EI: all 99 MOB files under `D:\Evil Islands\Maps`.
- Jamais Vu 1.35: all 153 MOB files under
  `D:\Evil Islands\Starter\MODS\JamaisVu135\Maps`.

Both alternate installations were compared before the audit. Their Base MOB
sets and JMV MOB sets were file-for-file identical, so only one copy of each
corpus was scanned.

MobExplorer executable SHA-256:
`253cdf9aaee74640a219283a210c2a026a644161ce06f4df25a297e9cad4ecdd`.

The full local JSON evidence is
`C:\Users\ilufu\Downloads\ПЗ Жамевю\Codex\work\lever-schema-audit-20260722.json`
(24,344,849 bytes, SHA-256
`6d82ea85e53069579552989d90e1c8377b41068dc98f8a686de00e32b6dd6cdd`).
It is not a distributable fixture because it inventories installed game and mod
data.

## Results

| Corpus | MOBs parsed | MOBs with levers | Levers | Structural layouts | Parse errors |
|---|---:|---:|---:|---:|---:|
| Base EI | 99/99 | 50 | 408 | 1 | 0 |
| JMV 1.35 | 153/153 | 57 | 1,110 | 1 | 0 |

All 1,518 levers use the same ordered layout:

| Order | Record | Raw ID | Payload |
|---:|---|---:|---|
| 1 | `LEVER_SCIENCE_STATS_NEW` | `0xBBAC0006` | three DWORDs |
| 2 | `LEVER_CUR_STATE` | `0xBBAC0002` | byte |
| 3 | `LEVER_TOTAL_STATE` | `0xBBAC0003` | byte |
| 4 | `LEVER_IS_CYCLED` | `0xBBAC0004` | byte |
| 5 | `LEVER_IS_DOOR` | `0xBBAC0007` | byte |
| 6 | `LEVER_RECALC_GRAPH` | `0xBBAC0008` | byte |
| 7 onward | ordinary world-object records | unchanged | unchanged |

The outer record is `LEVER` (`0xBBAC0000`) and its common `OBJTYPE` leaf is 60.
An ordinary world object uses the `OBJECT` envelope (`0x0000B001`) and
`OBJTYPE` 54.

No scanned lever contains the old `LEVER_SCIENCE_STATS` record or
`LEVER_CAST_ONCE`. Those tags remain readable compatibility cases, but there is
no corpus evidence for adding either one during a new conversion.

Typical safe defaults are also strongly supported by the corpus and the
existing `CLever` constructor:

- open mode 1 (free/open normally), key 0, hands skill 0;
- current state 0, total states 2;
- cycling off, door off, graph recalculation off.

The dialog must still expose these values. Existing unusual values must be
preserved rather than normalized: Base contains one 100-state lever, and JMV
also contains a 100-state lever and one current-state byte of 253.

## Model compatibility evidence

A lever does not require a lever-specific model or a same-model raw lever
template:

- 10 Base models are used by both `OBJECT` and `LEVER` records; 11 same-file
  model pairings were found.
- 31 JMV models are used by both levers and other object classes; 30 same-file
  `OBJECT`/`LEVER` pairings were found.
- Base `zone4.mob`: `stga2` is used by ordinary objects 1767, 1790, and 1797 and
  by levers 1916, 1925, and 1926.
- Base `zone9.mob`: `stst48` is used by ordinary objects 1480-1482 and lever
  2054; `stst49` is likewise shared by ordinary objects and lever 2055.
- JMV `zone11.mob`: `stst43` is used by ordinary object 769 and by several
  levers.

This is direct shipped-data evidence that object behavior is selected by the
MOB envelope/type and lever records, independently of whether the visual model
was previously used as a lever.

## Least-destructive conversion design

Conversion should be a dedicated raw-subtree transaction, not construction
through the legacy serializer and not cloning an unrelated lever:

1. Require a structurally parsed `OBJECT` subtree with `OBJTYPE` 54 and valid
   source provenance.
2. Preserve the target's NID, common record order, raw payloads, model,
   textures, body parts, transform, directory membership, script identity, and
   any bounded opaque common records.
3. Change only the outer record ID from `OBJECT` to `LEVER`.
4. Insert the six corpus-proven lever records above immediately before the
   preserved common world-object records.
5. Change only the existing `OBJTYPE` payload from 54 to 60.
6. Recalculate the changed envelope/ancestor lengths.
7. Reparse the candidate, verify that the resulting subtree is a lever, and
   semantic-diff it against the source. The only authorized structural changes
   are the envelope, six inserted records, `OBJTYPE`, and derived lengths.
8. Apply the replacement as one undoable transaction. Undo must restore the
   exact original subtree bytes.

The conversion does **not** need a matching model, an existing lever in the
active MOB, or a same-profile raw lever template. Profile gating is only needed
if a future profile documents a different lever schema; Base and current JMV
share the same proven schema.

## Remaining runtime proof

The structural rule is now proven across the complete installed Base/JMV
corpora. Before calling the feature runtime-validated, a disposable copy should
still be tested in both profiles:

- convert a mundane object whose model is already demonstrated in both roles;
- convert a model never previously used as a lever;
- exercise `GetLeverState`, `SwitchLeverState`, and `SwitchLeverStateEx`;
- save/reload the map and verify undo/no-op byte preservation;
- test door and graph-recalculation modes separately, since those flags affect
  more than simple interaction.

Failure of the runtime test should disable conversion for that exact profile,
not reintroduce the unsupported same-model-template requirement.
