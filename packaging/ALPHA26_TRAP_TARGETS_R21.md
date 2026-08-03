# 0.3.0-alpha.26 Trap targets r21

## Changes

- Magic traps now show a read-only **Will attack (derived)** row in Behavior
  mode. It lists only the numbered/named player groups the trap will attack.
- The displayed targets come from the trap's Player group and the containing
  MOB's diplomacy relationships marked War.
- The serialized legacy target mask is preserved for lossless round trips but
  is no longer offered as a misleading editable Behavior property.
- The legacy target-mask getter now retains the full 32-bit value instead of
  narrowing it to one byte.

## Focused manual checks

1. Open a MOB containing a magic trap and switch to Behavior mode.
2. Select the trap. Confirm **Will attack (derived)** lists only target group
   numbers and available names, not the complete diplomacy table.
3. Change the trap's **Player group** or edit that player's diplomacy row,
   reselect the trap, and confirm the summary follows the War relationships.
4. Confirm **Will attack (derived)** cannot be typed into or pasted over.

## Known limitations

- This revision does not redesign the MOB diplomacy-table editor; it only
  presents the trap's effective subset in the trap properties.
