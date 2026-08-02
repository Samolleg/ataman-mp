# 0.3.0-alpha.26 character body-part validation test

## Changes

- MOB Family Validation now reports character-mask risks with MOB path, NID,
  name, prototype, resolved family, Import mode, and the exact serialized
  parts.
- A helmeted standard wearable with neither `hr` nor `hr.NN` receives a
  Warning. This covers the confirmed `Autozone3obradd.mob` NID 800004 clipping
  pattern even when the editor preview looks complete.
- Missing or explicit-empty masks on standard wearable human, orc, skeleton,
  and undead assemblies receive non-blocking Critical findings.
- Missing core anatomy and distinct dotted alternatives in one logical slot
  receive Warnings; duplicate tokens receive Info.
- Ordinary units use the active profile database for model/equipment ownership.
  Import units use their embedded MOB model and equipment. Integral creatures
  and legacy empty-mask skeleton assemblies are not given humanoid rules.
- Unknown/model-specific tokens remain visible, copyable, savable, ordered,
  duplicated where authored, and byte-preserved.
- These findings appear in MOB Family Validation and Message Log only. They do
  not edit data, open a model-part preflight popup, block Save, or block Test
  Current Map.

No repair UI or automatic normalization was added. Suggested repair means
comparison with a matched working unit, not a one-click transformation.

## Focused test

1. Start the editor, select the JMV profile, and open `zone3obr.mob` together
   with `Autozone3obradd.mob`.
2. Open View > MOB Family Validation. Confirm NID 800004 reports
   `missing_hair_selection`, while NID 3470 does not.
3. Select the finding and confirm it identifies the file, NID, object name,
   prototype, `Import=false`, and the 15 serialized parts.
4. Save an unchanged copy of either MOB and confirm no parts are added,
   removed, reordered, or normalized.
5. Open `zone12.mob` and confirm NID 6257 reports the `rh3.pike00` /
   `rh3.tube00` overlap.
6. If you have a copied test MOB with an empty standard wearable mask, confirm
   it receives a Critical finding but Save and Test Current Map remain
   available. Do not make this test by editing an installed MOB.
7. Confirm an ordinary creature with an empty mask does not receive a wearable
   empty-mask finding.

## Known limitations

- Missing core anatomy is a clipping-risk diagnostic, not a proven crash rule.
- Unknown tokens and unavailable assembly members are intentionally not judged
  by a global allowlist; configured-resource diagnostics remain separate.
- The validator proposes no automated repair because model-specific masks and
  future mod tokens cannot be normalized safely.
