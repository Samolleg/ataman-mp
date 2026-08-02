# Character body-part validation

## Scope and evidence boundary

This policy classifies serialized `OBJBODYPARTS` masks without treating
Endurance's final-appearance preview as engine truth. The evidence set is:

- a read-only structural audit of all 99 installed Base MOBs and all 157
  installed JMV MOBs through Endurance's own parser;
- a read-only correlation with the active Base/JMV Monster, RaceModels, and
  item records through Endurance's profile database providers;
- the matched JMV `zone3obr.mob` / `Autozone3obradd.mob` control;
- the fingerprinted executable's explicit-mask, database hair, and helmet
  replacement behavior, independently checked against the shipped character
  assemblies, their FIG/BON morph geometry, and controlled geometry fixtures;
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

The first helmet rule was too broad because plain `hr` is not a universal
hair switch. The audited client path is:

1. A missing/empty mask enables the complete assembly. Otherwise load the
   serialized part names literally.
2. Only the legacy `unhuma` and `unhufe` templates translate plain `hr` to the
   database-selected `hr.NN` member.
3. Equipping helmet geometry `TT` looks for `hr.NN.armorTT`.
4. If that replacement exists, it replaces native hair. If it does not exist,
   already-loaded `hr.NN` is removed; when no matching hair was loaded, the
   helmet path falls back by loading native `hr.NN`.

This explains why adding plain `hr` repairs the legacy-human zone3 control but
does nothing for the `unorfe` controls. Orcs require the exact database-selected
member (`hr.00`, `hr.01`, and so on). Hairless templates skip the branch.
Import-local units and assemblies without the required members remain outside
the rule.

Of 1,022 JMV database-haired helmeted actors with complete active-resource
inventory, 204 take that fallback; 769 load matching hair before equipment,
and 49 lack the required native-hair/helmet member combination. A fallback is
not by itself a visual defect: `hr.NN` is a database slot, and shipped members
in that slot include below-crown facial/beard geometry as well as cranial hair.
At each actor's serialized complexion, Endurance interpolates the same eight
FIG morph extremes and accumulated BON offsets used by its renderer and
compares referenced hair vertices with the model's `hd` crown. The 204
fallbacks split into 102 above-crown and 102 at-or-below-crown cases. Only the
above-crown half is warning-eligible. This is not a zone, NID, race, or
reproduced-tuple allowlist: the same data-driven decision applies to any
database-equipped character assembly. Missing or malformed geometry is not
guessed and produces no hair warning.

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
assembly contains `hr.01` and `hd.armor06`, but no `hr.01.armor06`. The exact
runtime branch above explains why the omission loads visible native hair.

NIDs 4500 (`GZOrcFemaleF11`) and 4634 (`OrcFemaleRuneEM3`) provide the orc
counter-controls. Both use `unorfe` helmet 04 and visibly retain unwanted hair.
Adding plain `hr` does not change either result because `unorfe` has no legacy
root translation. Their exact database selections are `hr.01` and `hr.00`;
loading that exact member gives the helmet path a figure to remove when the
helmet-specific replacement is absent. NID 260 reaches the same `unorfe` /
`hr.01` / helmet-04 path.

The false-positive controls exercise the other half of the same rule. JMV
`zone3obr` NID 294 resolves to `unhuma` hair `hr.02` and helmet 05; JMV
`zone11` NID 46 and the reported `zone9` skeletons resolve to `unhusk`
`hr.02`. Their equipment paths do load the database member, but the shipped
`hr.02` geometry stays at or below the interpolated head crown and is visibly
facial/beard geometry rather than scalp hair. They therefore receive no
helmet-hair warning. The editor still renders those members exactly as the
equipment decision selects them.

The suggestion is derived from the same decision. `unhuma` and `unhufe` need
plain `hr`, because those are the only templates with the legacy root-to-
database-hair translation. Every other database-equipped assembly needs the
exact database hair member, such as `hr.00` or `hr.01`. If the assembly has an
authored `hr.NN.armorTT` replacement, the editor shows it and no finding is
emitted. If the exact native hair was initially selected, the helmet removes
it and no finding is emitted. Otherwise the finding additionally requires the
actual FIG/BON geometry, at the unit's complexion, to extend above that model's
head crown. The classifier reads referenced rendered vertices rather than
using a token vocabulary or an assembly bounding-box overlap guess.

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
| Database helmet can expose cranial hair | Warning | A database-equipped character reaches the verified native-hair fallback, and its active `hr.NN` FIG vertices extend above the model's `hd` crown at the serialized complexion. Enable the suggested effective token: `hr` only for `unhuma`/`unhufe`, otherwise exact `hr.NN`. |
| Fallback loads an at-or-below-crown `hr.NN` member | No finding | The slot can contain intentional facial/beard geometry. NID 294 and the `unhusk/hr.02` controls prove that fallback alone is not a visual defect. |
| Hairless database character | No finding | `hairIndex=-1` skips the runtime hair branch. |
| Import-local or assembly/geometry-unproven character | No finding | Import equipment is MOB-local rather than database-owned, and unavailable native-hair, helmet, FIG, or BON evidence is not guessed. |
| Authored helmet-specific hair replacement | No finding | `hr.NN.armorTT` satisfies the replacement path, so the missing-base fallback is not used. |
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

The standard-anatomy classification remains intentionally bounded to
`unhuma`, `unhufe`, `unorma`, `unorfe`, `unhusk`, `unhuzm`, and `unhuzf` for
empty-mask and core-limb checks. It does not gate helmet hair. Helmet hair is
evaluated for every database-equipped character, including equipped legacy
undead/skeleton assemblies, using the resolved database row and actual assembly
members plus the model-local crown comparison. Import units use embedded
equipment and remain outside that database rule.

## Product behavior and verification

Each body-part message contains only the NID and a concise description of the
problem. The finding's structured location still retains the MOB path and
object section for navigation. Messages do not paste the serialized parts,
name, prototype, model, or Import mode. Findings appear in MOB Family
Validation and the Message Log. They do not mutate data, offer an Apply action,
block Save, block Test Current Map, or open a body-part preflight popup.
Assembly evidence for disk-discovered family members is resolved on the
validation worker from ordered active-profile resources; it does not depend on
which model the viewport happened to load first.

Automated and corpus checks:

- `mob_family_validation_tests`: focused suite passed, including the exact zone3 pair,
  orc exact-hair controls and a database-equipped legacy undead assembly,
  NID 294 and `unhusk/hr.02` at-or-below-crown controls,
  hairless, native-member, and helmet-hair-replacement exclusions, plus a
  resource-backed FIG geometry model that was never loaded by the viewport,
  empty/missing wearable masks, creature exclusion, Import/database equipment
  ownership, missing core anatomy, overlaps, duplicates, unknown-token
  visibility, severity, location, and input-byte preservation;
- `profile_visual_provider_tests`: focused suite passed, including empty/all-parts hair,
  legacy `hr`, exact `hr.NN`, retained non-database hair variants,
  equipped-undead, and authored replacement paths;
- `endurance_document_tests semanticAnomaliesAreWarningsOnly`: exact duplicate,
  overlap, future-token array and byte-identical serialization passed;
- corpus probe: all 99 Base and 157 JMV MOBs parsed with zero unreadable files;
  Base resolved 241/241 database-haired helmeted actors by initially loaded
  hair and found zero fallbacks; JMV found 204 fallbacks (102 above-crown and
  102 at-or-below-crown), 769 initially loaded hair paths, and 49 unavailable
  member combinations among 1,022 actors;
- installed zone3 probe required
  `mob.semantic.helmet_hair_fallback:800004` and passed;
- installed zone12 probe required
  `mob.semantic.overlapping_unit_parts:6257` and passed.

No automatic normalization is justified. The concise hair message is
`NID ...: missing 'PART' may show hair with helmet TT.` `PART` is computed from
the resolved model and database hair, not hard-coded. The message is emitted
only after both the runtime selection branch and above-crown geometry test; it
does not paste the parts array. This is an assembly-backed runtime and geometry
rule, not a zone, family, or reproduced-tuple list.
Missing-core severity and the danger of particular dotted pairs beyond the
matched cases remain evidence-labeled diagnostics until controlled runtime
comparisons establish a narrower contract.
