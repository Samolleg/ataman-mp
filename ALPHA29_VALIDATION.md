# Alpha.29 Logic-mode culling and Settings validation

Use copied MOB files only. This build does not enable additional MOB save classes.
`OBJ_PLAYER` remains the only authorized changed-save transaction.

## Settings

1. Open **Settings** and select every tab, especially **Render**.
2. Toggle **Frustum culling**, press Apply, reopen Settings, and confirm the value
   persisted.
3. Confirm the main editor cannot be operated behind the window while Settings is
   open, but all Settings controls remain enabled.

## Zone 18 rendering

Use the same camera position and loaded Zone 18 MPR/MOB for each comparison:

1. Object mode, culling enabled.
2. Object mode, culling disabled.
3. Logic mode, culling enabled.
4. Logic mode, culling disabled.

Allow the first frame to populate cached bounds, then record a ten-second FPS
range. Alpha.29 should retain most of the object-mode culling gain in Logic mode.
Logic helpers still have their own submission cost, so Logic mode need not match
object mode exactly.

With culling enabled, inspect units with guard radii and patrol/look paths near and
outside every screen edge. Also inspect magic-trap cast points and activation
zones. Off-screen owning object geometry may be skipped, but any helper that would
previously be visible must remain visible and editable. Selection and viewport
picking must remain correct.

Return a clean-session workflow log, four FPS ranges, and any camera/object case
where a helper disappears. If Logic mode remains substantially slower, the next
optimization is cached logic-helper bounds, not disabling visualization.

## MOB safety regression

Repeat an isolated Player edit on a disposable copy. The staged save must report
one authorized `OBJ_PLAYER` leaf and no other edits. All unvalidated property and
structural changes must continue to fail closed.
