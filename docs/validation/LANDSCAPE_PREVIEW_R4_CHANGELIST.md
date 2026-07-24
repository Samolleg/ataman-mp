# Landscape Preview R4 changelist

R4 is a focused follow-up to the R3 paint-history test. Changed MPR saving
remains disabled.

## Corrected

- Undo and Redo now use the same history-navigation gate.
- Land and overlay painting are explicitly persistent operations for both
  directions, so Ctrl+Y no longer cancels painting after history reaches zero.
- A genuine paint-operation cancellation now clears its tile preview, custom
  cursor, temporary visibility state, and outliner state in one place. This
  prevents a stranded tile cursor when another command intentionally exits
  painting.

## Automated coverage

- Added a UI-independent operation-history policy test covering Select, land/
  overlay brush persistence, and cancellation of transient move/rotate/scale
  previews.
- Full document/terrain suite: 44 passing tests.

## Still provisional

- The selected-tile brush remains the inherited renderer/UI bridge. Automatic
  terrain-type transitions and natural tile-variant distribution are the next
  production painting increment.
- Changed MPR persistence remains disabled.

