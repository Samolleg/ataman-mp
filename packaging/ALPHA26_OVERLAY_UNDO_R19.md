# EI Mapper Endurance 0.3.0-alpha.26 - Overlay Undo R19

## Changelist

- Fixed Add Surface strokes that begin over an unpickable overlay gap and then
  connect to an existing overlay: the first valid move or release now opens the
  undo transaction before applying any overlay cell.
- Added a model-side safety gate so terrain and overlay stamps cannot mutate the
  canonical MPR when no undo-owned brush stroke is active.
- Added explicit workflow diagnostics for every committed terrain/overlay
  stroke, including its stroke ID, description, sector count, and undo-history
  index change.
- Retains R18's exact before/after overlay-sector snapshots for mixed
  absent/existing-surface strokes.

## Focused manual check

1. Open a disposable copy of an MPR containing an existing overlay surface.
2. Choose **Landscape Tools > Add overlay surface**.
3. Begin a quick drag in an area with no overlay and continue into the existing
   overlay without releasing the mouse.
4. Confirm History contains one **Activate overlay surface** entry for the whole
   gesture.
5. Undo once: every newly activated/recoloured cell from that gesture must
   return exactly to its pre-stroke state. Redo once and confirm the full stroke
   returns.
6. Optionally repeat with a very short drag whose initial press has no visible
   overlay under it.

## Expected diagnostic

`workflow.log` should contain `Terrain stroke undo transaction committed` with
`description=Activate overlay surface` and a history transition such as
`history=12->13` for each changed stroke.
