# 0.3.0-alpha.26 Object UI review r19

## Changes

- Object Properties now gives behavior/trap collections a concrete
  viewport-height row and uses per-pixel outer scrolling, so the scrollbar can
  reach the complete collection without resizing the dock.
- Undo after creating a patrol, look point, activation area, or cast target
  restores the exact selection that was active before the P/L action. Redo
  returns selection to the recreated points.
- The bottom viewport help is selection-aware in Behavior mode. It names the
  exact Ctrl+Tab destination and only advertises the P/L actions valid for the
  current unit, patrol, look point, or trap selection.
- L on a selected unit appends a look point to the last patrol in the active
  logic slot. If the unit has no patrol, L is omitted from the help and pressing
  it leaves the unit selected.
- The native UI checker now validates the responsive Vector3 component row by
  its zero-gap layout invariant instead of requiring the superseded form-layout
  class.

## Focused manual checks

1. Select a unit with enough properties to push its patrol collection below
   the fold. Scroll Object Properties to the bottom and confirm the whole
   collection becomes reachable without resizing the dock. Repeat for a trap.
2. Select a unit, patrol/look point, trap, activation area, and cast target in
   turn. Confirm the bottom line changes its P/L descriptions and that Ctrl+Tab
   always names the mode it would switch to.
3. With a unit whose active slot has patrols selected, press L. Confirm a look
   point is appended to the last patrol and placement begins. Undo and confirm
   the unit is selected again; redo and confirm the recreated point is selected.
4. Select a unit whose active slot has no patrol. Confirm L is absent from the
   help and pressing L neither creates anything nor clears the selection.
5. Repeat P/L creation, Undo, and Redo from patrol points and each trap-related
   selection type. Confirm the pre-action target is restored by Undo.

## Known limitations

- The collection height follows the Object Properties viewport after resize;
  it is not user-resizable independently from the dock.
