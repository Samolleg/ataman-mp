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
   TTI2 use the database values directly for armor. Weapon FIG components are
   zero-based, while the corresponding weapon MMP filename is one-based
   (`rh3.sword00` uses `UnhumaSW_01...`). TTI 0 armor keeps the base anatomical
   component and applies its `_00` equipment texture.
9. Confirm female humans, male/female orcs, and skeletons resolve equipment
   textures from the shared `unhuma` equipment atlases. Their face/skin atlas
   remains model-specific. Validate JMV `zone1.mob` IDs 203, 221, and 224;
   ID 224 must retain its body components with the TTI 0 clothing textures.

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
   `UnhumaPL.res`, `UnhumaSW.res`, and `UnhumaMT.res` read-only.
5. Validate PNT3 equipment atlases with large transparent tails. Armor, weapon,
   and TTI-0 anatomical components must not show stripes, random alpha, or
   pixels from outside the decoded buffer.
6. Validate `zone1` ID 221 with its four bow members (`bwparta`, `bwpartb`, and
   both `bwtetiva` members), `zon3obr` IDs 233/234 with `UnhumaMT` shield
   textures, and `zon3obr` ID 198 with its `hd.armorNN` helmet.
7. Validate spear goblins with only the explicit pike visible and ogres with
   their club using OBJ secondary texture. This follows the FIG header texture
   selector (`1` primary, `2` secondary); body texture must not propagate to
   a texture-2 integral weapon.

Default-idle animation validation is specified with the animation follow-up and
is not a prerequisite for this equipment integration.
