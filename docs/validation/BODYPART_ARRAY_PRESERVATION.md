# MOB body-part array preservation validation

## Incident evidence

The JMV `zone3obr` comparison established two separate facts:

- The original `zone3obr.mob` (SHA-256
  `F3F15E3A5DADD1ED52966DB94EFAB32D5E1780C2501FABC4C6B47E52CC3C7DC5`)
  stores 16 `OBJBODYPARTS` entries for NID 3470, including `hr`.
- The supplied `zone3obrtest2.mob` (SHA-256
  `91732B1F419913974B36846515755B6CF4919012E20EC57F29D4F5DC4129303B`)
  stores 15 entries for NID 3470 and its pasted copy NID 202; `hr` was removed
  when the Parts dialog applied the figure-derived visible list.

An unchanged Save As remains lossless. Whole-object copy also retains the raw
body-part list. The defect was limited to explicit Parts Apply/copy because the
old property projection represented only tokens advertised by the resolved
figure assembly.

The original pre-Ataman and current `Autozone3obradd.mob` are byte-identical
(SHA-256
`538B53313FBC86D732C5C2BF6630E98CD8CE85BA1BE3A55A6475F9F9EC4EF09E`),
so NID 800004's missing `hr` predates Endurance.

## Corrected contract

- The serialized MOB string array is authoritative.
- Every serialized token is visible, even when the current figure or profile
  does not recognize it.
- Filtering changes display only.
- No-op Apply retains the exact array byte semantics: order, duplicate tokens,
  unknown tokens, and the empty-list convention.
- Checkbox edits are deltas against each selected object's original array.
- Parts copy/paste uses the complete ordered underlying array, not visible table
  rows. The old version-2 clipboard format remains readable.

Unknown tokens are not automatically errors. They may be valid profile or mod
extensions and must remain inspectable and lossless.

## Automated evidence

- `EIMapperEndurance.exe --ui-widget-check`: passes exact-array, unknown-token,
  filter-only, order-sensitive equality, duplicate-token, and delta-preservation
  checks.
- `mob_family_validation_tests`: passes the renamed-primary overlay regression
  for installed `zone3obr.mob` plus loaded `zone3obrtest.mob`.
- The supplied `zone3obrtest2.mob` passes `--validate` and
  `--roundtrip-check`; neither command writes the input.

