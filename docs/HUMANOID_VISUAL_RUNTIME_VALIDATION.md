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
   display placeholders; no inferred perk authorization is accepted.
4. Use an item whose TTI component is absent from the selected assembly and an
   unbalanced modifier string. Confirm the original field remains unchanged and
   a structured diagnostic/placeholder is shown.
5. Confirm the visual and picking passes use the same transient component plan.
6. JMV: validate `zone1.mob` ID 205 and representative `unhufe`, `unorfe`, and
   `unorma` units. Each armor record must enable only its database slot
   (`helm`, `plate`, `leggings`, `shirt`, `pants`, `boots`, or `gloves`), and
   the displaced anatomical part must not remain visible underneath it.
7. Give a JMV unit three or more weapon/shield entries. Confirm only the first
   pair is parsed and rendered; an invalid third entry must not create a
   placeholder.
8. Confirm armor and weapons use the per-item texture resource derived from the
   model, slot/type, TTI, TTI2, and material code. For example, a human boot
   with TTI 1 and `ogre leather` (`lo`) resolves the shipped
   `unhumabt_01.lo.0.x4.mmp` family rather than the face/skin texture. TTI and
   TTI2 use the database values directly. Weapon FIG components and MMP
   filenames use the same literal TTI (`rh3.sword01` uses
   `UnhumaSW_01...`). TTI 0 armor keeps the base anatomical component and
   applies its `_00` equipment texture.
9. Confirm female humans, male/female orcs, and skeletons resolve equipment
   textures from the shared `unhuma` equipment atlases. Their face/skin atlas
   remains model-specific. Validate JMV `zone1.mob` IDs 203, 221, and 224;
   ID 224 must retain its body components with the TTI 0 clothing textures.
10. Confirm packed `.x2` helmets, weapons, and shields use the established 2x
    FIG UV transform. Body armor and skin `.x4` atlases must keep their stored
    FIG UVs unchanged: `.x4` identifies that atlas family but is not a general
    4x coordinate multiplier. In particular, check the back half of helmets,
    weapon blades, and shield faces without reintroducing stripes on torsos or
    limbs.
11. Equip a helmet on a unit with database hair. Confirm the helmet component
    is visible and the `hr.NN` component is omitted; removing the helmet must
    restore the selected hair.

## UI and performance sanity

1. Confirm `UNIT_PROTOTYPE` suggestions and imported equipment/spell/item
   controls refresh after profile changes and retain unknown current values.
2. Open several MOBs and switch profiles repeatedly; confirm snapshot provenance
   and provider IDs remain profile-scoped and memory does not grow per frame.
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
   pixels from outside the decoded buffer.
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
