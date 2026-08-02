# Evil Islands character construction and rendering

This document is the authoritative Endurance map of the character pipeline.
It describes verified behavior, not a promise that every shipped asset follows
one humanoid convention. Detailed controls and unresolved work are in
`CHARACTER_EVIDENCE_MATRIX.md`; the editing workflow is in
`CHARACTER_AUTHORING.md`.

Status vocabulary:

- **Verified Base/JMV**: established by provider code and tests plus a shipped
  corpus or controlled runtime/viewer case.
- **Renderer specification**: current Endurance behavior with regression tests;
  useful as an executable oracle, but not automatically engine truth.
- **Deferred**: evidence is insufficient for a general authoring rule.

## Resolution boundary and Import semantics

Resource-profile provider bindings select database, appearance, equipment, and
animation semantics. Feature code must never branch on a literal profile ID.
`base.standard.v1` and `jmv.v1` fail closed on unknown providers, missing rows,
incompatible materials, malformed assets, or missing members.

For a normal unit, the visual chain is `Monster prototype -> RaceModels family
-> model/assembly + skin -> equipped item rows`. The active profile database is
the only relational source. JMV overlays ADB members from `databaseJmv.res` but
does not backfill missing relational rows from Base.

For `Import` / `Is need import`, the MOB's embedded appearance, component mask,
armor, weapons, and local items are authoritative. A database may interpret a
local symbolic name (for example an embedded parent used to obtain a base skin),
but must not supply a missing local model or item. Missing local data produces a
diagnostic or placeholder. Neither path mutates the MOB or database.

Creature precedence is separate. A creature normally retains its embedded model,
textures, component mask, and integral/default weapon parts. Humanoid equipment
substitution must not replace those with an unrelated RaceModels or inventory
interpretation.

Validation follows the same ownership boundary but inspects the serialized
mask, not only the resolved preview. This is necessary because database-driven
anatomy reconstruction can visually hide an omitted serialized root. Standard
wearables receive non-blocking empty/missing-mask, core-anatomy, overlap, and
duplicate diagnostics. Missing hair is reported only for a fingerprinted
broken/working control such as the confirmed JMV zone3 pair, not from helmet or
hair-index presence alone. Integral creatures and legacy skeleton
assemblies retain their corpus-normal empty-mask behavior. Unknown tokens are
never normalized or rejected. See
[`validation/CHARACTER_BODYPART_VALIDATION.md`](validation/CHARACTER_BODYPART_VALIDATION.md).

## Assemblies, geometry, morph vertices, and complexion

Character archives contain named FIG parts plus a `.mod`/`.lnk` parent map.
Common wearable anatomy includes `hp`, `bd`, `hd`, `lh1..3`, `rh1..3`,
`ll1..3`, and `rl1..3`, with authored descendants. Names are semantic only in
combination with the selected assembly; no global suffix scan is valid.

FIG8 stores eight extreme vertex blocks used for strength, agility, and height
interpolation, normals, UVs, triangle/component indices, and a primary/secondary
texture selector. Topology must be identical across extremes. Complexion is
geometry/body-size interpolation, not skin colour. The placed MOB instance's
constitution/complexion values outrank the Monster prototype defaults.

Flying and floating families may appear not to change height because the visible
silhouette is also governed by root/bone offsets and animation. That observation
is not permission to add a corrective scale; a root/mesh controlled comparison
is still required per family.

Upper armor replaces an anatomical component only when the exact authored
same-suffix member exists. It also includes same-suffix descendants reachable
through that replacement's parent tree. Thus `l_shell.armor24` and
`r_shell.armor24` below `ll1.armor24`/`rl1.armor24` are included, while unrelated
`hd.armor24` or arm parts are not. An absent optional replacement leaves the
lower/base mesh visible and is not synthesized.

## Textures, materials, atlases, and UVs

A visible humanoid normally uses a composed final body atlas. Bottom-to-top JMV
composition is base skin, shirt, pants, boots, gloves, then upper armor ordered
by decreasing database `Wear Order`; equal values use helmet, plate, leggings.
Observed plate/leggings values `1,0` and `0,0` both place plate before leggings.
Plate outranks shirt/gloves in overlapping regions; leggings outrank pants/boots.
Some upper items add geometry, some only paint atlas regions, and some do both.

TTI and TTI2 are literal indices. JMV atlas families are `SH/PT/BT/GL/PL/LG`
for body equipment, `HL` helmets, `SW/AX/DG/SP/HM` one-handed weapons,
`BW/CB` ranged weapons, `SD` shields, and `TP/TS/TA/TH/TF` two-handed families.
The item/material row supplies a suffix; cross-family suffixes are allowed when
the exact member exists. Silver and gold remain valid provider-extensible codes,
but the audited shipped visible equipment has no matching control. Missing exact
members remain visible diagnostics and do not alias to another material.

Stored UV domains must be preserved. Packed `.x2` helmets/weapons/shields use
the established two-times FIG transform. `.x4` identifies the body-atlas family
but is not a four-times UV multiplier. PNT3 is row-major premultiplied BGRA. The
fingerprinted `2dintmmx.dll` decoder checks only the first dword of each 16-byte
literal block for an aligned transparent-byte run; it does not test every pixel
or impose EIrepack's one-million limit. Composition is
`src + ((dst * (255 - alpha)) >> 8)`. Character atlases use nearest minification
without generated mip sampling so small equipment islands survive.

The Final Character Texture viewer is the preferred renderer diagnostic: it
activates for exactly one selected unit, shows renderer-order layers and per-part
UVs, preserves an upright atlas/labels, and exposes provenance through toggles.

## Equipment and attachments

Lower armor is always resolved before upper armor regardless of loot/equipment
array order. Quick, quest, and loot items never become avatar parts. Equipping a
helmet hides database hair; removal restores it.

Base preserves one one-handed or ranged weapon and diagnoses extras. JMV has
layouts for ordinary, one-hand+shield, dual one-hand, single two-hand, requested
dual two-hand, and requested two-hand+shield. Only the first pair is considered.
Without authoritative nonzero JMV `UnitPerkType.StrongMan` ownership, requested
dual-two-hand renders only the first weapon and two-hand+shield remains closed.
Equipment alone is not TitanGrip evidence. Bow layouts include authored quiver,
arrow, bow-part, and string members.

Numbered equipment inherits authored attachment nodes. For example,
`lh2.shieldNN -> lh2.shield00 -> lh2`: animation belongs to `shield00` and must
not be applied again to the numbered geometry. The same rule covers swords,
axes, pikes, and other `*00` attachment families. Hand geometry remains the
selected anatomical/glove mesh; weapon selection adds weapon geometry rather
than activating unrelated `base*` variants.

Weapon geometry is resolved independently along three axes:

1. Presentation: a standalone inventory/thumbnail or dropped-world figure may
   come from `InitWeapons.res`, while an equipped weapon comes from a nested
   member of the active character-family assembly.
2. Character family: human male and female assemblies are separate resources
   (`unhuma` and `unhufe`). An edit in one does not imply a change in the other.
3. Hand: JMV dual-wield assemblies use distinct authored members. In the
   controlled dagger case, `rh3.dagger08` is the character's right/main-hand
   geometry and `lh3.dagger08` is the left/off-hand geometry. A front-facing
   character's right hand appears on the viewer's left.

The male JMV runtime control changed only `unhuma.mod/rh3.dagger08`; the
right/main-hand dagger showed the serration while the left/off-hand copy remained
smooth. The separately patched `InitWeapons.res/initwedg8weapon.fig` changed the
thumbnail but not either equipped assembly member. This rejects any general rule
that one item prototype has a single shared visible FIG.

## Animation

The provider selects a compatible standing `IDLE` from ADB metadata and weapon
masks, never from a unit-ID or clip-name exception. JMV accepts extended masks;
Base does not. Missing ADB/ANM, malformed channels, incompatible masks, or an
incomplete hierarchy produces a diagnostic static bind pose.

The selected ANM action is a nested RES whose component members contain
quaternion, translation, and optional per-frame vertex morph channels. Logical
playback is 15 Hz and preserves the extra closing-loop sample. Root translation
is relative to its first sample. EI child rotations pre-multiply inherited
rotation (`child * parent`), as confirmed against ei_figer and a focused test.
Part matrices apply identically to body and equipment, and visible/picking passes
share one pose. Morph buffers (dragon wing membranes are the control) advance
with the same frame while topology stays fixed.

## Family matrix summary

| Family | Appearance/equipment source | Weapon convention | Current confidence |
|---|---|---|---|
| Human male/female | DB-selected wearable assembly; imported stays local | DB items and authored attachments | Verified Base/JMV |
| Orc male/female | DB-selected wearable assembly; shared `unhuma` equipment atlases | DB items | Verified Base/JMV |
| Skeleton/standard undead | Wearable DB path when assembly capability exists | DB items | Verified Base/JMV |
| `UndeadF16/F17` | Keep compatible embedded `unhuma`; DB skin 29/items | DB items | Verified JMV control |
| Goblin | Embedded creature assembly/mask/texture | Integral sling or explicit local pike; general selection deferred | Narrow JMV controls only |
| Lizardman | Embedded creature assembly/mask/texture | Integral/local trident | Runtime control; general resolver deferred |
| Ogre | Embedded creature assembly; primary body and secondary/integral weapon textures | Integral club; one corpus-backed `club02` DB-atlas alias | Narrow JMV controls only |
| Dragon | Embedded creature assembly; morph channels significant | Intrinsic/local parts | Animation/morph control; equipment N/A |
| Bat, harpy, banshee, beholder, shadow, spider, octopus | Embedded/default creature data | Intrinsic/local when present | Inventory required per model; no humanoid substitution |

The last row is deliberately conservative. It records the safe provider rule,
not an unverified claim that every named family has identical archive structure.

## Deferred generalization

- Replace the wearable model allowlist only after subtype/capability/assembly
  metadata is corpus-verified. Candidate MOB subtypes are 50 human, 51 animal,
  and 52 monster; inconsistent data must still fail closed.
- Replace `unmogo` suppression and family crutches with a general resolver for
  Monster Items + MOB parts + FIG group/texture selectors.
- Parse bounded profile-scoped JMV perk tables without executing Lua before
  enabling TitanGrip layouts.
- Establish exact root/height behavior for floating/flying complexion controls.
- Treat ADB writing and MMP/PNT3 encoding as unsupported until their owning
  encoders are reverse-engineered and no-op/changed round trips pass.

## Primary evidence

Provider behavior is implemented in `source/runtime/providers/`, animation in
`source/runtime/unit_animation.*`, geometry in `source/render/figure*` and
`part*`, and texture decoding in `source/resources/mmp_codec.*`. Focused tests
are `profile_visual_provider_tests`, `unit_animation_tests`,
`figure_vertex_component_tests`, and resource-name/texture tests. Decisive
visual history includes `e260722`, `e9f12b4`, `900a263`, `35f2ce9`,
`a4a3248`, `63e19fd`, `f91bfc5`, and `8ad5166`.

Blender import/export evidence comes from
[konstvest/ei_figer](https://github.com/konstvest/ei_figer), locally audited as
v5.2. It supports model/link and ANM import/export plus morph-authoring helpers;
its own README states that automatic intermediate morph generation is absent and
that replacing RES members leaves unreachable old payload bytes until repacked.
