# Alpha 26 modern terrain paint lifecycle R1

This build repairs the integrated terrain brush lifecycle regression that
allowed one tile-paint stroke and then rejected every later stroke.

## Changes

- Completing a Modern, Classic Auto, or Manual paint stroke now releases its
  shared brush ownership only after resolution and undo ownership finish.
- Cancelling a stroke rolls its preview back before releasing brush ownership
  and still creates no undo entry.
- Added a regression contract covering both completion and cancellation
  ordering, including admission of later strokes.

Terrain atlas semantics, material selection, generated slots, save behavior,
and the R162 semantic authority are otherwise unchanged.

## Focused test

1. Paint at least two consecutive strokes in Manual mode.
2. Switch to Modern and Classic Auto and paint multiple strokes in each mode.
3. Start a stroke, cancel it, then paint again; confirm the cancelled preview
   is gone and the next stroke works.
4. Undo and redo the completed strokes. Confirm each gesture owns exactly one
   history entry and the cancelled gesture owns none.

