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

## UI and performance sanity

1. Confirm `UNIT_PROTOTYPE` suggestions and imported equipment/spell/item
   controls refresh after profile changes and retain unknown current values.
2. Open several MOBs and switch profiles repeatedly; confirm snapshot provenance
   and provider IDs remain profile-scoped and memory does not grow per frame.
3. Pan across a dense humanoid scene. Transient geometry may rebuild when a
   profile or unit visual field changes, but must not rebuild every frame.

Default-idle animation validation is specified with the animation follow-up and
is not a prerequisite for this equipment integration.
