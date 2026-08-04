# Alpha.26 JMV character equipment visualization r2

## Changed

- StrongMan-authorized two-handed weapon plus shield and dual-two-handed
  characters now use the ordinary one-hand/shield or dual-wield standing pose.
  The active JMV humanoid animation set has no dedicated Titan Grip idle, and
  this avoids the crossed single-two-handed stance.
- Bow and crossbow characters now both display the assembly's authored quiver
  and arrows. The shared meshes use the equipped weapon's distinct `BW` or
  `CB` atlas, TTI, and material code instead of a generic editor texture.
- A database-backed JMV character without a valid `tables.lua` character row is
  now a Warning in the `JMV` category. A missing or malformed JMV `tables.lua`
  file remains Critical. Base and Base-derived profiles are unchanged.

## Manual checks

1. Configure the Jamais Vu profile and open a map with StrongMan characters.
2. Confirm a two-handed weapon plus shield uses the normal shield/one-hand
   stance, and a dual-two-handed character uses the ordinary dual-wield stance.
3. Inspect bow and crossbow users with different weapon materials/types. Confirm
   both show quiver and arrows and that their ammo gear matches the equipped
   bow/crossbow appearance rather than sharing one generic texture.
4. Run validation and confirm a database character without a valid character
   row in `tables.lua` is a `JMV` Warning, while a missing or malformed file is
   still Critical.

## Known limitation

- JMV does not provide a dedicated Titan Grip standing animation; the editor
  intentionally uses its authored ordinary stance as the least conflicting
  available pose.
