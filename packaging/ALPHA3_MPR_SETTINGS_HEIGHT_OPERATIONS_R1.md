# EI Mapper Endurance 0.3.0-alpha.3 — MPR Settings R1

This tester build adds the first safe whole-map terrain-height workflow.

## Changes

- Renames **MPR Atlas Tiles** to **MPR Settings**.
- Adds an MPR summary covering sector, tile, vertex, height-range, maxZ, namespace, and atlas information.
- Adds undoable whole-map offset, flatten, clamp, and normalize/remap operations.
- Previews affected canonical vertices, sectors, and resulting height ranges before applying.
- Rejects invalid ranges, non-finite values, flat-range normalization, and implicit saturation.
- Retains the existing atlas tile viewer through **Open Atlas Tiles**.
- Documents fixed SEC dimensions, canonical shared vertices, maxZ scaling, structural resize risk, and future staged transactions.

## Deliberately deferred

- Editing maxZ or requantizing all heights.
- Cropping or expanding sector dimensions.
- Adding or removing SEC/archive members.
- Direct tile-type or atlas mutation.

Use `MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md` for the focused manual test cases.
