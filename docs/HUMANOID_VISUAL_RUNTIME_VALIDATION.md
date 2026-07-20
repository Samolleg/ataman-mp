# Humanoid visual runtime validation

Run these cases against disposable copies or read-only resource roots. Do not
edit `database.res`, JMV/Base archives, live MOBs, maps, or the game install.

## Profile and source precedence

1. Select Base, open a MOB containing human male/female, orc male/female,
   skeleton, and undead prototypes, and confirm model, skin, complexion, hair,
   armor, and one weapon match Base database rows.
2. Select JMV for the same MOB and confirm the rows and ADB provenance change
   atomically to JMV. Switch back to Base and confirm no JMV model, item, or ADB
   remains cached.
3. Give a normal (Import=false) JMV humanoid deliberately conflicting embedded
   texture, armor, and weapons. Confirm the database plan wins those fields
   without changing the MOB. Confirm a compatible embedded wearable assembly
   remains selected instead of being replaced by `RaceModels.FLD30`.
4. Give an imported (Import=true) unit conflicting database values. Confirm its
   embedded model, texture, body mask, and equipment win. Remove an embedded
   model/item and confirm a placeholder/diagnostic appears, with no database
   substitution.
5. Bind an unknown provider ID in a disposable profile and confirm database,
   dropdown, appearance, and equipment resolution disable with diagnostics.
6. JMV non-humanoids: validate goblins, ogres, lizards, dragons, and animals.
   Confirm the provider retains each MOB-resolved model, texture, component
   mask, and integral weapon part; none may become `cannotdisplay` merely
   because its database race points at another creature variant.

## Equipment layouts

1. Base: ordinary armor plus one sword/axe/dagger/pike/club/bow/crossbow. Add a
   second weapon and confirm it is ignored with a diagnostic.
2. JMV: independently validate ordinary, 1H+shield, dual 1H, and single 2H on
   models that contain the requested TTI components.
3. JMV: request dual 2H and confirm the first weapon renders through the
   single-2H path while the second is suppressed with a diagnostic. Request
   2H+shield and confirm it remains diagnostic-only. This is the current safe
   fallback until the JMV provider parses prototype perk ownership from
   `lua/tables.lua`; `zone17` ID 47533 is the control for the later
   TitanGrip-authorized dual-2H path.
4. Use an item whose TTI component is absent from the selected assembly and an
   unbalanced modifier string. Confirm the original field remains unchanged and
   a structured diagnostic/placeholder is shown.
5. Confirm the visual and picking passes use the same transient component plan.
6. JMV: validate `zone1.mob` ID 205 and representative `unhufe`, `unorfe`, and
   `unorma` units. Shirt, gloves, pants, and boots always use the base anatomical
   geometry (`Bd/Hp/Lh1-3/Rh1-3/Ll1-3/Rl1-3`); their TTI selects the body-atlas
   layer and never selects `.armorNN` geometry. When the selected assembly
   contains an exact upper-armor member, top armor wins across its lower-layer
   group: `plate > shirt/gloves` and `leggings > pants/boots`; helmet is likewise
   top armor. Base anatomy remains visible only where the selected top-armor TTI
   has no replacement for that anatomical slot.
   A missing optional overlap retains the lower item and is not diagnosed as a
   missing required component. Base keeps the ordinary disjoint slot mapping.
   The displaced anatomical part must not remain visible underneath a winner.
7. Give a JMV unit three or more weapon/shield entries. Confirm only the first
   pair is parsed and rendered; an invalid third entry must not create a
   placeholder.
8. Confirm armor and weapons use the per-item texture resource derived from the
   model, slot/type, TTI, TTI2, and material code. Humanoid skin and non-helmet
   armor are one shared, non-overlapping whole-body UV atlas (except the `hp`
   region): compose the base skin, shirt/gloves/pants/boots, then plate/leggings
   with source-over alpha. Do not bind an isolated armor layer as the final
   texture of only its replacement mesh; transparent pixels must reveal the
   lower atlas rather than make limbs disappear. For example, a human boot
   with TTI 1 and `ogre leather` (`lo`) resolves the shipped
   `unhumabt_01.lo.0.x4.mmp` family rather than the face/skin texture. TTI and
   TTI2 use the database values directly. Weapon FIG components and MMP
   filenames use the same literal TTI (`rh3.sword01` uses
   `UnhumaSW_01...`). TTI 0 armor keeps the base anatomical component and
   applies its `_00` equipment texture.
9. For JMV top armor, validate exact assembly-driven replacement. A
   `rh3.armor01` member replaces `rh3` only when that exact member exists in
   the selected ANHUM assembly. If it is absent, retain the lower `rh3` mesh;
   do not synthesize geometry or a placeholder. Apply the same rule to every
   exact helmet, plate, and leggings member. Ignore Monster/MOB equipment-array
   order: resolve shirt, gloves, pants, and boots first, then order upper armor
   by `FLD24-0` (`Wear Order`) descending. Equal upper values use helm, plate,
   then leggings, so observed plate/leggings `1/0` and `0/0` pairs both compose
   plate before leggings.
10. Confirm female humans, male/female orcs, and skeletons resolve equipment
   textures from the shared `unhuma` equipment atlases. Their face/skin atlas
   remains model-specific. Validate JMV `zone1.mob` IDs 203, 221, and 224;
   ID 224 must retain its body components with the TTI 0 clothing textures.
11. Confirm packed `.x2` helmets, weapons, and shields use the established 2x
    FIG UV transform. Body armor and skin `.x4` atlases must keep their stored
    FIG UVs unchanged: `.x4` identifies that atlas family but is not a general
    4x coordinate multiplier. In particular, check the back half of helmets,
    weapon blades, and shield faces without reintroducing stripes on torsos or
    limbs.
12. Equip a helmet on a unit with database hair. Confirm the helmet component
    is visible and the `hr.NN` component is omitted; removing the helmet must
    restore the selected hair.
13. Validate an explicit JMV cross-family material suffix, such as the Metal
    `hmsh`/`hmgl`/`hmpt`/`hmbt` prototypes equipped with the Hide-family
    `dragon red` material by `Human Kania NPC Sorceress M10` (`bz8k` ID
    1000292348). The suffix is a database-selected texture code and must not be
    rejected merely because gameplay material families differ. Missing material
    rows or empty texture codes must still diagnose and produce placeholders.
14. Validate ordinary staff classification with `cz19h` ID 1600 (paired
    TypeID 11 agility staffs) and ID 8888 (TypeID 11 staff plus shield). Confirm
    these use dual-one-hand and one-hand+shield respectively, while TypeID 15
    remains the dedicated two-handed staff family.
15. Compare actor body proportions before and after database visual resolution.
    The placed MOB instance's complexion/constitution is authoritative; the
    Monster row is a prototype default and must not thicken every instance.

## UI and performance sanity

1. Confirm `UNIT_PROTOTYPE` suggestions and imported equipment/spell/item
   controls refresh after profile changes and retain unknown current values.
2. Open several MOBs and switch profiles repeatedly; confirm snapshot provenance
   and provider IDs remain profile-scoped and memory does not grow per frame.
   Repeatedly switch among `bz8k`, `zone18`, and `zone3obr`; terrain-atlas upload
   must complete without a `QOpenGLTexture` access violation after MOB cache
   cleanup.
3. Load a dense MOB and inspect the `Preloaded visual resources` timing log.
   Pan across the map afterward: bringing a unit into frame for the first time
   must not trigger its database/figure/texture resolution. Transient geometry
   may rebuild when a profile or unit visual field changes, but not per frame.
4. Configure the profile with the complete Base/JMV `Res` directories, not a
   hand-picked figures/textures subset. The catalog must discover JMV face,
   armor, weapon, and material atlases such as `faces.res`, `UnhumaBT.res`,
   `UnhumaPL.res`, `UnhumaSW.res`, `UnhumaSD.res`, and `UnhumaMT.res` read-only.
5. Validate PNT3 equipment atlases with large transparent runs. Armor, weapon,
   and TTI-0 anatomical components must not show stripes, random alpha, or
   pixels from outside the decoded buffer. The original `2dintmmx.dll`
   `UnpackToSurface` backend identifies a run as a non-zero dword whose alpha
   byte is zero; its value is a 16-byte-aligned transparent-byte count. Do not
   use EIrepack's one-million cutoff: `UnhumaPL_05.bl.0.x4` contains a valid
   3,245,872-byte run. Literal PNT3 pixels are premultiplied BGRA and composition
   uses the engine formula `src + ((dst * (255 - alpha)) >> 8)`. Decoded pixels
   remain row-major: do not transpose, rotate, or mirror individual layers. In `zone1` ID 203,
   `UnhumaPL_05.bl.0.x4` must occupy the lower `bd.armor05` UV region and
   `UnhumaLG_07.bl.0.x4` must align with the hip/leg replacement UVs.
6. Validate `zone1` ID 221 with its six bow members (`bwparta`, `bwpartb`, both
   `bwtetiva` members, `quiver`, and `arrows`), `zon3obr` IDs 233/234 with
   `UnhumaSD` shield textures, and `zon3obr` ID 198 with its `hd.armorNN`
   helmet.
7. Validate spear goblins with only the explicit pike visible and ogres with
   their club using OBJ secondary texture. This follows the FIG header texture
   selector (`1` primary, `2` secondary); body texture must not propagate to
   a texture-2 integral weapon. Also check `zone17` ID 763: its full-atlas
   `rh3.club02` FIG must use the Monster Items weapon `2h slander.meteoric`,
   resolved as `UnhumaTH_02.mr.0.x2`, rather than the incompatible MOB
   `club7` atlas. Ordinary `rh3.club00` ogres must remain on their MOB club
   textures.
8. Normal database-backed humans, orcs, and skeletons rebuild their anatomical
   base from the database-selected assembly. Their embedded MOB part mask is
   not reused after a race/model substitution. Imported units continue to use
   the embedded mask exactly.
9. Equip different weapon families while keeping the same gloves. The ordinary
   or armor-selected `lh3`/`rh3` hand geometry must remain unchanged. Optional
   `base*`/`l.base*` assembly variants must not appear or replace glove parts;
   only the resolved weapon geometry is added.
10. Populate quick, quest, and loot inventory fields with valid items. Confirm
    they remain available to dropdowns and validation but add no avatar render
    components. Move an item to an equipped armor/weapon slot and confirm it is
    rendered normally when the profile supplies compatible geometry and a
    material texture.
11. Switch repeatedly between dense maps after their visual preload completes.
    Confirm map-local composed `layers|...` atlases are released when the last
    MOB unloads, ordinary archive textures remain cached, and switching does not
    crash or accumulate one full decoded body atlas per prior unit.
12. Select exactly one unit and choose `View > Final Character Texture`.
    Confirm the popup shows the same bottom-to-top layer list used
    by rendering and the composed RGBA atlas over a checkerboard. Use this view
    when comparing `zone1` ID 203, `bz8k` ID 919536, and ID 1001621802; the
    lower-layer order must be shirt, pants, boots, gloves before upper armor.

## Post-PNT3-unpack validation probes

- Recheck `bz8k` ID 919536, IDs 1001621802/1000304365, and `zone3obr` ID 253.
  Retain them as regression probes until runtime testing confirms exact PNT3
  run expansion fixes each family. Do not apply a global UV flip: faces and
  many humanoids already validate with the stored `.x4` UV convention.
- JMV `UndeadF16`/`UndeadF17` are the control for the distinction between an
  existing MOB assembly and `RaceModels.FLD30` (labelled `Graphic data Mask
  Name` in the exported database). Their database row links `UndeadMale` and
  skin 29, while zone17 instances embed the wearable `unhuma` assembly. Keep
  that compatible instance assembly, resolve `unhumaskin_29_x4.mmp`, then apply
  database armor and weapons. Do not replace it with the `unmozo0` mask name or
  synthesize `vurdalak_29`.
- Shield placement follows the assembly's `lh2.shieldNN -> lh2.shield00 -> lh2`
  link hierarchy. The `lh2.shield00` animation channel belongs to the attachment
  node; numbered shield geometry inherits it and must not receive the canonical
  channel a second time. The same rule applies to numbered sword, axe, pike,
  and other equipment beneath their `*00` attachment nodes. Validate
  `zon3obr` IDs 233/234 at several idle frames: the shield should remain on the
  forearm without clipping caused by a doubled attachment rotation.
- A JMV dual-two-hand request does not by itself imply TitanGrip authorization.
  Until the provider reads JMV `UnitPerk`, render only the first two-handed
  weapon through the established `singleTwoHand` path and report the suppressed
  second weapon. `UnitPerkType.StrongMan` is column 42; JMV applies it to perk
  branch 29, and its special `strong1` handling promotes the otherwise dummy
  branch records to the sole TitanGrip behavior. A nonzero StrongMan value is
  therefore the ownership signal; it is not an ordinary three-tier value.
  `zone17` ID 47533 (`Human Hadagan EliteL A20`, StrongMan=1) is the runtime
  control. True dual-two-hand rendering and two-hand-plus-shield remain
  deferred until that Lua source is profile-scoped and parsed.

## Deferred classification and integral-weapon refactors

These are recorded follow-up actions, not prerequisites for the current visual
test build:

1. Thread the MOB `Unit subtype` through appearance, equipment, and animation
   resolver requests. Audit the Base/JMV corpus, then use `50 = Human` as the
   primary database-equipped actor discriminator and `51 = Animal` / `52 =
   Monstr` as embedded creature discriminators. Unknown or inconsistent values
   must diagnose and fail closed.
2. Remove the wearable-model-name allowlist as a classification source. Model
   families may remain as bounded format/texture capabilities after corpus
   validation, but must not decide whether an actor is human or monster.
3. Replace the `unmogo` sling suppression with a general integral-creature
   weapon resolver. JMV Lua contains no goblin/sling special case. Reverse
   engineer how the engine combines Monster `Items`, MOB `OBJ_BODYPARTS`, FIG
   component/group metadata, and the active creature weapon before changing
   behavior. The narrow `dclub02` database-family to integral `club02` alias is
   already corpus-backed for `zone17` ID 763; it is not evidence for guessing
   equivalent aliases in other families.
4. Cover goblin sling/spear, ogre club, and lizardman trident controls. Select
   one mutually exclusive integral weapon group, retain its primary/secondary
   texture selector, and pass its semantic weapon family to ADB idle selection.
5. Add a profile-scoped, bounded JMV `lua/tables.lua` reader for `UnitPerk` and
   `UnitPerkType`. Enable TitanGrip layouts only from the resolved prototype's
   nonzero StrongMan branch; malformed/missing Lua remains diagnostic and
   closed. Do not execute arbitrary profile Lua in the editor.

## Corpus-established JMV equipment atlas mapping

- Database `TTI` is the literal FIG and MMP index; it is not incremented.
- 1H: `SW`, `AX`, `DG`, `SP`, `HM`; ranged: `BW`, `CB`; shield: `SD`.
- 2H: pike `TP`, sword `TS`, axe `TA`, hammer `TH`, staff `TF`.
- Helmets use `HL`; `UnhumaMT` contains generic `materialNNNN` textures and is
  not the shield family.
- Equipment materials are profile-extensible and slot-driven. An equipped
  silver (`sv`) or gold (`gd`) item must render if its profile supplies the
  corresponding atlas member; a missing member produces a diagnostic rather
  than an alias to another material. The audited JMV corpus currently contains
  no equipped `sv`/`gd` examples or matching equipment atlases. Quick items are
  never rendered on gameplay avatars regardless of material.

Default-idle animation validation is specified with the animation follow-up and
is not a prerequisite for this equipment integration.
