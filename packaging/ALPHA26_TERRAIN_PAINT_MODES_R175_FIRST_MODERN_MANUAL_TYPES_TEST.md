# Alpha 26 terrain paint modes R175: First Modern stroke and Manual variants test

This focused build fixes two working-atlas publication failures reported after
R174.

## Changes

- Importing or generating terrain artwork during a Modern stroke no longer
  rebuilds the material palettes while that stroke owns the mouse gesture.
  Switching into Modern, changing the selected material, or painting directly
  after opening an MPR can no longer cancel the first stroke before it receives
  its undo entry.
- Deferred palette updates run only after the completed stroke is committed to
  undo (or after an abandoned stroke has been rolled back).
- Working-atlas publication now installs the complete active runtime terrain
  type table into the live MPR model. Every physical variant shown in Manual
  mode is therefore paintable immediately; saving or reopening is not required.
- Resolve Missing Blends installs the same complete type table in its staged
  model before it creates the final map command.

## Focused test

1. Open a clean Zone 2 MPR and select a Modern material that has not yet been
   used in this session. Paint once. The released stroke must already contain
   resolved blends and create exactly one undo step.
2. Undo, switch to another paint mode and back to Modern, then paint once. The
   first stroke after the switch must again resolve and undo normally.
3. Change to another Modern material and paint once. Its first stroke must not
   remain as a hard/unresolved preview and must be undoable.
4. Introduce a new family with Modern paint, switch immediately to Manual, and
   try several of the newly added tiles, including uniform-looking sibling
   variants. Every visible tile must paint immediately without first saving,
   finalizing, or reopening the MPR.
5. Undo and redo those Manual placements and confirm the exact selected
   physical artwork returns.

No semantic material assignments or blend masks changed in this revision.
