# EI Mapper Endurance 0.3.0-alpha.26 terrain paint modes R158 outline/resolve stamp test

Focused follow-up for the R157 terrain paint modes build.

## Changes

- Tile brush hover outlines now draw the requested brush footprint only. The preview no longer expands to the resolver's affected-cell set before a stroke is actually applied.
- Brush size changes immediately refresh the terrain hover preview at the current mouse position.
- Resolve Missing Blends now preserves already decoded non-uniform authored/contextual tiles when using a recovered material field. It only repairs decoded uniform no-blend cells when the recovered shared-corner field requires a non-uniform blend.

## Validation

- `terrain_virtual_atlas_tests`: 102 passed, 0 failed.
- `endurance_document_tests`: 107 passed, 0 failed, 6 expected fixture/export skips.
- `git diff --check`: pass.

## Focused manual checks

- In Tile target mode, switch circle/square brush shapes and brush sizes 1/2/3. The yellow outline should update immediately and show only the tiles affected by the brush input footprint.
- Run Resolve Missing Blends on a map containing existing stamp/contextual islands. Existing decoded stamps/blends should remain in place; only missing uniform/no-blend boundaries should be repaired.
- Undo Resolve Missing Blends as one action.
