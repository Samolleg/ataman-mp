# Terrain regression r19 tester changelist

This revision fixes the incomplete first Modern square stamp using the exact
Zone1 Lava-on-Water workflow.

## Changed

- The first realization of a Modern material family now makes every advertised
  physical variant paintable before the first map command is constructed.
- A first 6x6 square Lava-on-Water overlay stamp no longer leaves random holes
  because sibling uniform variants were missing their runtime terrain type.
- The change applies to the first stamp; later atlas publication behavior,
  Shift+R, and blend resolution are unchanged.

## Focused manual checks

1. Open Zone1 and choose Overlay, Modern, Tile, square brush size 6.
2. Select Lava / Orange, disable **Auto-resolve blends**, and click once over a
   uniform Water / Still area large enough for the whole square.
3. Confirm all 36 displayed cells change on that first click. Undo and redo once
   and confirm the exact square disappears and returns.
4. Repeat with **Auto-resolve blends** enabled.
5. Separately check for the reported one-frame flash with Object shadows both on
   and off, including a first stamp and a later stamp that reuses existing slots.

## Known boundary

The 6x6 document/input regression is automated and passes on the installed
Zone1 MPR+MOB. The affected GPU/compositor flash has not been reproduced by the
automated capture, so this build deliberately does not claim that flicker is
fixed; the manual result is required.
