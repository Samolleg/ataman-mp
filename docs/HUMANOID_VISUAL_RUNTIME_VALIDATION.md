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
3. Give a normal (Import=false) unit deliberately conflicting embedded model,
   texture, armor, and weapons. Confirm the database plan wins without changing
   the MOB fields.
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
3. JMV: request dual 2H and 2H+shield. Confirm both remain diagnostic-only and
   display placeholders. A paired TypeID 15 database row (for example `zone18`
   ID 47533) does not prove that the placed unit has the required `TitanGrip`
   perk; no inferred Lua/perk authorization is accepted.
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
5. Validate PNT3 equipment atlases with large transparent tails. Armor, weapon,
   and TTI-0 anatomical components must not show stripes, random alpha, or
   pixels from outside the decoded buffer. PNT3 transparent runs are byte
   counts from 1 through 1,000,000 and are not restricted to 16-byte alignment.
   Decoded pixels remain in row-major EIrepack/Direct3D orientation: do not
   transpose, rotate, or mirror individual layers. In `zone1` ID 203,
   `UnhumaPL_05.bl.0.x4` must occupy the lower `bd.armor05` UV region and
   `UnhumaLG_07.bl.0.x4` must align with the hip/leg replacement UVs.
6. Validate `zone1` ID 221 with its six bow members (`bwparta`, `bwpartb`, both
   `bwtetiva` members, `quiver`, and `arrows`), `zon3obr` IDs 233/234 with
   `UnhumaSD` shield textures, and `zon3obr` ID 198 with its `hd.armorNN`
   helmet.
7. Validate spear goblins with only the explicit pike visible and ogres with
   their club using OBJ secondary texture. This follows the FIG header texture
   selector (`1` primary, `2` secondary); body texture must not propagate to
   a texture-2 integral weapon.
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

## Post-orientation-fix validation probes

- Recheck `bz8k` ID 919536, IDs 1001621802/1000304365, and `zone3obr` ID 253.
  Their earlier skin-through/clothing sampling was captured while asymmetric
  PNT3 layers were vertically inverted; retain them as regression probes until
  runtime testing confirms the row-major decoder fixes each family.
  Do not apply a global UV flip: faces and many humanoids already validate with
  the stored `.x4` UV convention.
- JMV `UndeadF16`/`UndeadF17` currently resolve the database-declared
  `UndeadMale -> unmozo0` model, but the available RaceModels texture list only
  establishes `vurdalak_00` through `_03`, not a synthetic `vurdalak_29`, and
  that creature assembly lacks the requested humanoid equipment components.
  Keep this diagnostic/fail-closed until the intended race/model/skin mapping is
  established from the database, addon code, or developer documentation.
- Shield placement follows the assembly's `lh2.shieldNN -> lh2.shield00 -> lh2`
  link hierarchy. Any offset correction must be derived from the FIG/LNK rest
  transforms or animation channels, not a per-character coordinate adjustment.

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
