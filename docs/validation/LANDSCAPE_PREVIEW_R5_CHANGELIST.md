# Landscape Preview R5 changelist

R5 corrects the two remaining R4 landscape-tool state leaks. Changed MPR saving
remains disabled.

## Corrected

- Clicking backward or forward in the History window now uses the same
  persistent-operation gate as Ctrl+Z and Ctrl+Y. History jumps no longer leave
  land or overlay painting while the landscape-editing checkbox remains checked.
- Every actual transition between Sculpt, land painting, and overlay painting
  clears both atlas-table selections and resets rotation. The destination paint
  layer starts with no brush and requires an explicit user selection.
- A paint preview is now created only after a selected brush has been validated.
  Moving over terrain with no brush cannot resurrect a stale ground/overlay tile.
- The custom cursor now depicts the selected brush rather than the tile currently
  underneath the pointer.

## Automated coverage

- Added deterministic landscape-mode reset policy coverage.
- Full document/terrain suite: 45 passing tests.

## Still provisional

- Direct selected-tile painting remains an inherited preview bridge. Automatic
  terrain-type transitions and natural variant distribution are the next
  production painting increment.
- Changed MPR persistence remains disabled.

