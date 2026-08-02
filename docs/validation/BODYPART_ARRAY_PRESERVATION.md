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
- Filtering changes display only. It searches part name, source, and state;
  `true`/`checked`/`on` and `false`/`unchecked`/`off` are state aliases.
  These aliases are never rendered in the Enabled column; that column contains
  checkboxes only.
- Recognized but absent entries are labeled `Model option`. Unrecognized MOB
  entries are labeled `Unknown serialized` and remain fully editable/lossless.
- Wearable humanoid families expose the canonical base-anatomy options even
  when an optional runtime-selected member such as `hr` is absent from the
  currently resolved assembly and from the malformed MOB object.
- Column headers sort the table, and the Parts window retains its last size.
- Every column is interactively resizable and its widths are retained.
- Checking or unchecking a selected row applies the new state to every selected
  part and preserves the multi-row selection.
- No-op Apply retains the exact array byte semantics: order, duplicate tokens,
  unknown tokens, and the empty-list convention.
- Checkbox edits are deltas against each selected object's original array.
- Parts copy/paste uses the complete ordered underlying array, not visible table
  rows. The old version-2 clipboard format remains readable.
- Parts undo/redo records exact before/after serialized arrays. It does not
  reapply a relative checkbox delta to whichever array happens to be current.

Unknown tokens are not automatically errors. They may be valid profile or mod
extensions and must remain inspectable and lossless.

Profile/family-aware diagnostics now inspect that exact serialized array without
normalizing it. Empty/missing masks are Critical only after the Import/database
appearance path resolves to a standard wearable assembly; shipped creature and
legacy skeleton empty masks remain accepted. Missing hair is reported only for
the confirmed JMV zone3 fingerprint; other missing-`hr` combinations remain
lossless and unreported. Missing core anatomy, distinct dotted alternatives,
and duplicates receive non-blocking findings. The complete evidence and severity
matrix is in
[`CHARACTER_BODYPART_VALIDATION.md`](CHARACTER_BODYPART_VALIDATION.md).

## Automated evidence

- `EIMapperEndurance.exe --ui-widget-check`: passes exact-array, unknown-token,
  state/source-filter, column-sorting, canonical-hair-option, filter-only,
  checkbox-only display, selected-row batch toggling, interactive-column,
  exact property-paste,
  order-sensitive equality, duplicate-token, and delta-preservation checks.
- `mob_family_validation_tests`: passes the exact NID 3470/NID 800004 zone3
  control, matched hairless/unconfirmed exclusions, Import/database ownership,
  wearable/creature classification, concise messages, severity, overlap,
  duplicate, unknown-token, and byte-preservation cases.
- The supplied `zone3obrtest2.mob` passes `--validate` and
  `--roundtrip-check`; neither command writes the input.
- The supplied `zone3obrtest1.mob` is byte-identical to original
  `zone3obr.mob` after opening Parts and applying without changes: both have
  SHA-256 `F3F15E3A5DADD1ED52966DB94EFAB32D5E1780C2501FABC4C6B47E52CC3C7DC5`.
