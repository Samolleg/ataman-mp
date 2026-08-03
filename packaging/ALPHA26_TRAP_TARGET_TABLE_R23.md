# Alpha 26 trap-target table and tooltip readability (r23)

## Changed

- Replaced the read-only text-looking **Will attack (derived)** field with a
  plain **Will attack** button.
- The button opens a non-editable, diplomacy-style table with one **Will
  attack** row and columns only for factions marked **War** in the trap's
  Player-group diplomacy row.
- Removed the redundant **(derived)** label suffix. The property tooltip still
  explains how the target set is calculated.
- Split patrol/look-point and trap collection help into multiple lines so the
  tooltip remains readable without spanning most of the viewport.

No MOB values or diplomacy values are changed by this viewer.

## Focused test

1. Enter Behavior mode and select a magic trap.
2. Confirm **Will attack** is a button rather than a text field.
3. Open it and confirm the table has one read-only row and columns only for the
   factions marked **War** against that trap's Player group.
4. Hover the patrol/look-point collection and trap collection. Confirm each
   instruction tooltip is split across several readable lines.

If the selected trap's MOB lacks a complete diplomacy row, the viewer reports
that targets are unavailable instead of presenting an editable fallback.
