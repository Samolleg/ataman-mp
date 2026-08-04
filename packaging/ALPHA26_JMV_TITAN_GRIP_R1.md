# Alpha.26 JMV titan-grip visualization r1

## Changed

- Fixed JMV character 997569 and other eligible characters omitting their
  two-handed weapon plus shield, or second two-handed weapon, in the editor.
- JMV equipment visualization now reads the character's StrongMan ownership
  from the active mod's bounded `lua/tables.lua` `UnitPerk` data. Base and
  Base-derived profiles are unchanged.
- JMV validation now reports a Critical finding when `tables.lua` is missing or
  its required `UnitPerk` data is malformed, and when a database-backed
  character has no corresponding `UnitPerk` row.
- Shield-first data is normalized for display: the weapon attaches to the right
  hand and the shield to the left-hand shield attachment.

## Manual checks

1. Configure the Jamais Vu profile and open Zone 19.
2. Find unit NID 997569 and confirm its two-handed weapon and shield both render.
3. Inspect another StrongMan character equipped with two two-handed weapons, if
   available, and confirm both weapons render in the expected hands.
4. Temporarily test a copied JMV setup with a missing or malformed
   `lua/tables.lua`; validation should show a Critical JMV character-perk
   finding. Do not alter the live installation for this check.
