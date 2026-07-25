# EI Mapper Endurance 0.3.0-alpha.13 — MPR vertical scale R5

This tester build adds the first editable MPR header parameter without crossing
the structural-resize boundary.

## Changes

- **MPR Settings > Vertical scale** can change `maxZ` as one undoable action.
- **Scale terrain with vertical range** retains all packed height samples and
  scales their world-space height.
- **Preserve world heights by requantizing** updates packed samples
  deterministically and previews rounding/error counts.
- Potential clipping fails closed unless the user explicitly allows clamping.
- Live terrain, MOB reprojection, MPR dirty state, and navigation diagnostics
  update with Undo/Redo.
- Changed saves combine the MP `maxZ` patch with required SEC derived-normal
  rebuilds in the existing staged reopen/diff/backup/atomic-save contract.

## Still deferred

- Crop/expand and any sector/member addition or removal.
- Liquid-material mutation and atlas authoring.

Use cases 14–17 in `MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md` for the focused pass.
Only use copied or disposable maps.
