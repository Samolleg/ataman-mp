# Character body-part validation

## Scope and evidence boundary

This policy classifies serialized `OBJBODYPARTS` masks without treating
Endurance's final-appearance preview as engine truth. The evidence set is:

- a read-only structural audit of all 99 installed Base MOBs and all 157
  installed JMV MOBs through Endurance's own parser;
- a read-only correlation with the active Base/JMV Monster, RaceModels, and
  item records through Endurance's profile database providers;
- the matched JMV `zone3obr.mob` / `Autozone3obradd.mob` control;
- the fingerprinted executable's explicit-mask and database head-selection
  behavior, independently checked against the shipped character assemblies;
- the shipped JMV `zone12.mob` overlapping-alternative case; and
- controlled runtime A/B fixtures for wearable empty masks.

The audited runtime fingerprints were Base `game.exe`
`B871D70D3876B2A5634F0920C92118BC49058B50C7D8CBE68376349F51C2E570`,
JMV `game.exe`
`BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2`,
and JMV `addon.dll`
`1B3AEFA65D097D48D3CCC7D970FAB30182CC31A4B0B1ECC33B6BD97270126C84`.
These fingerprints identify the evidence set; they are not a general promise
about unknown engine or mod variants.

## Corpus inventory

Endurance parsed every audited MOB, including the JMV file rejected by the
older external audit parser.

| Profile | MOBs | Units | Import=true | Empty masks | Missing records | Part-count histogram |
|---|---:|---:|---:|---:|---:|---|
| Base | 99 | 7,953 | 85 | 5,483 | 0 | 0: 5,483; 16: 2,470 |
| JMV | 157 | 4,557 | 4 | 2,336 | 0 | 0: 2,336; 1: 3; 15: 386; 16: 1,704; 18: 1; 19: 58; 20: 69 |

Base contains only the 16 ordinary wearable roots: `bd`, `hd`, `hp`, `hr`,
`lh1`, `lh2`, `lh3`, `ll1`, `ll2`, `ll3`, `rh1`, `rh2`, `rh3`, `rl1`,
`rl2`, and `rl3`. Each occurs 2,470 times. Every audited Base human/orc mask
is that exact 16-token set; empty masks belong to integral creature and legacy
skeleton/undead assemblies. Of the 85 Base imports, 81 are full-mask wearables
and four are empty-mask creatures.

JMV contains these 29 tokens (frequency in parentheses):

`bag` (127), `bd` (2,218), `bd.box00` (1), `hd` (2,218), `hp` (2,218),
`hr` (1,653), `hr.00` (1), `hr.01` (16), `lh1` (2,218), `lh2` (2,218),
`lh3` (2,218), `ll1` (2,218), `ll2` (2,218), `ll3` (2,218), `ll4`
(127), `rh` (1), `rh1` (2,218), `rh2` (2,217), `rh3` (2,218),
`rh3.club00` (36), `rh3.club02` (1), `rh3.daxe00` (1),
`rh3.pike00` (69), `rh3.tube00` (1), `rl1` (2,218), `rl2` (2,218),
`rl3` (2,218), `rl4` (127), and `sling` (127).

The additional JMV names are authored attachments, model-specific roots, or
numbered alternatives. They are not globally required anatomy. The four JMV
imports split into two 16-part humans and two 15-part orcs; imported equipment
must therefore be interpreted from the MOB, not from a normal-unit database
fallback.

The first helmet rule was too broad. Under that rule's predicate, the JMV audit
found 286 helmeted units without serialized `hr`: 179 resolve to database
`hairIndex=-1`, 105 have a non-negative hair index, and two are Import units
without a database hair owner. Forty-two also have an otherwise identical
same-template peer that includes `hr`. These shipped combinations show that
neither a helmet, database hair index, nor a matching peer is enough to call an
omission defective. Base has no candidates under the same audit.

For a non-empty serialized mask, the audited runtime uses that explicit
selection instead of regenerating the prototype's ordinary parts. Automatic
database head selection distinguishes hairless prototypes (`hairIndex=-1`)
from authored hair and combines authored hair with the selected helmet geometry.
That makes the resolved model/hair/helmet tuple the relevant contract; the map,
NID, prototype name, and presence of a peer are not runtime conditions.

## Matched controls

The JMV files are fingerprinted as:

- `zone3obr.mob`:
  `F3F15E3A5DADD1ED52966DB94EFAB32D5E1780C2501FABC4C6B47E52CC3C7DC5`;
- `Autozone3obradd.mob`:
  `538B53313FBC86D732C5C2BF6630E98CD8CE85BA1BE3A55A6475F9F9EC4EF09E`.

NID 3470 in the first file and NID 800004 in the second share the prototype
`Human Gipath NPC Boss F5`. NID 3470 selects all 16 ordinary roots including
`hr`; NID 800004 selects the same ordered set without `hr`. The latter equips a
helmet and clips in the game. The active database resolves this actor to the
JMV `unhuma` assembly, hair variant 01, and helmet geometry 06. The shipped
assembly contains the corresponding `hr.01` and `hd.armor06` members, and a
read-only mesh inspection confirms that hair participates visibly in this
helmeted head. Endurance's normal-unit renderer can hide the defect because it
rebuilds database anatomy before applying equipment. The validator therefore
inspects serialized parts and the resolved database head combination, not the
final preview.

The rule is not tied to zone3: a unit with the same JMV `unhuma` / hair 01 /
helmet 06 combination warns in zone4 or any other map. Across all 157 audited
JMV MOBs, this resolved dependency selects exactly NID 800004; the other 285
helmeted missing-hair masks remain unreported.

JMV `zone12.mob` SHA-256
`8C1A8AB373FD50EC915AB26C7308C012EE88EFC61F57733FDACF311A7461F2A8`
contains NID 6257, `DriadDarkM15` / `unmodr2`, with both `rh3.pike00` and
`rh3.tube00`. Those are two authored alternatives in the same logical `rh3`
slot. The rule is deliberately model-independent and does not mistake `rh3`
plus one dotted attachment for an overlap.

## Validation policy

| State | Severity | Reason and action |
|---|---|---|
| Missing `OBJBODYPARTS` on a standard wearable assembly | Critical | Missing and explicit-empty masks enable all assembly members, including optional alternatives. Suggest comparison with a matched working unit; never repair automatically. |
| Explicit empty mask on a standard wearable assembly | Critical | A controlled wearable A/B produced a runtime failure with the empty mask and ran after an authored explicit selection. Creature and legacy integral masks are excluded. |
| Confirmed JMV resolved head dependency without `hr` | Warning | A normal database unit resolves to `unhuma`, hair 01, and helmet geometry 06. This is independent of map/prototype/NID and follows the verified runtime request plus shipped mesh combination. |
| Any other helmeted unit without `hr`/`hr.NN` | No finding | The Base/JMV corpus and runtime observations do not support a general missing-hair warning. Hairless prototypes and other resolved head combinations stay lossless and unreported until separately verified. |
| Missing one of the 15 non-hair standard anatomy roots | Warning | Suspicious gap/clipping risk. This is corpus-backed anatomy, not a proven universal crash condition. |
| Two distinct dotted alternatives in one logical equipment/anatomy slot | Warning | The zone12 case proves the state exists and is logically overlapping. Base roots plus one attachment are not competing alternatives. |
| Duplicate token | Info | Suspicious redundancy only; order and duplicates remain exact. |
| Literal full 16-root wearable mask | No finding | This is the Base corpus norm. It is not the dangerous “all parts” state. |
| Empty mask on an integral creature or legacy skeleton assembly | No finding | This is the shipped Base/JMV norm and must not receive humanoid rules. |
| Unknown/future/model-specific token | No finding | Keep visible, copyable, editable, and byte-preserved. Vocabulary alone is not evidence of invalidity. |
| Unavailable assembly member/resource | Existing visual diagnostic | Resource availability belongs to the appearance provider and configured profile, not to a static token allowlist. |

`OBJBODYPARTS` is a string array. It has no serialized true/false entries:
presence selects a token and absence does not. An explicit empty array is
therefore different from a UI list of unchecked known options, even though both
can look empty in a projection.

The standard wearable classification is intentionally bounded to `unhuma`,
`unhufe`, `unorma`, `unorfe`, `unhusk`, `unhuzm`, and `unhuzf`. Normal units
resolve model family and equipment from the active database. Import units use
their embedded model and equipment. Integral creatures and legacy assemblies
such as `unmosk`/`unmozo0` remain separate because their shipped empty-mask
behavior contradicts a universal humanoid rule.

## Product behavior and verification

Each body-part message contains only the NID and a concise description of the
problem. The finding's structured location still retains the MOB path and
object section for navigation. Messages do not paste the serialized parts,
name, prototype, model, or Import mode. Findings appear in MOB Family
Validation and the Message Log. They do not mutate data, offer an Apply action,
block Save, block Test Current Map, or open a body-part preflight popup.

Automated and corpus checks:

- `mob_family_validation_tests`: 19 passed, including the exact zone3 pair,
  the same resolved dependency in a zone4 fixture, a different-helmet control,
  hairless and otherwise unconfirmed missing-`hr` exclusions,
  empty/missing wearable masks, creature exclusion, Import/database equipment
  ownership, missing core anatomy, overlaps, duplicates, unknown-token
  visibility, severity, location, and input-byte preservation;
- `endurance_document_tests semanticAnomaliesAreWarningsOnly`: exact duplicate,
  overlap, future-token array and byte-identical serialization passed;
- corpus probe: all 99 Base and 157 JMV MOBs parsed with zero unreadable files;
- installed zone3 probe required
  `mob.semantic.missing_hair_selection:800004` and passed;
- installed zone12 probe required
  `mob.semantic.overlapping_unit_parts:6257` and passed.

No automatic normalization is justified. The hair warning is a verified
resolved visual dependency, not a zone rule or general anatomy rule.
Missing-core severity and the danger of particular dotted pairs beyond the
matched cases remain evidence-labeled diagnostics until controlled runtime
comparisons establish a narrower contract.
