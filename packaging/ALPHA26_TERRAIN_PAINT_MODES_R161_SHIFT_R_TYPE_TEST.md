# EI Mapper Endurance 0.3.0-alpha.26 terrain paint modes R161 Shift+R type test

Focused root-cause follow-up for Resolve Missing Blends and ordinary Manual/
contextual tile behavior.

## Changes

- **Resolve Missing Blends** now treats each cell's predecessor runtime terrain
  type as a hard constraint. An exact authored tuple with the wrong type can no
  longer block realization of the required typed tuple.
- The hard type constraint is opt-in for map repair only. Normal Modern and
  Classic painting, atlas finalization, and developer builders retain their
  established semantic majority/tie behavior.
- Manual placement carries no ownership or preservation privilege. Manual
  tiles are ordinary physical atlas tiles and can be repaired or replaced under
  the same semantic rules as every other tile.
- Contextual stamps likewise carry no repair privilege. A unanimous recovered
  shared-corner field repairs stamps, authored blends, generated blends, and
  uniforms identically; genuinely ambiguous vertices remain untouched.
- Resolve remains staged and atomic: refusal or no-op does not mutate the MPR,
  working atlas, renderer, or undo history.

## Validation

- `terrain_virtual_atlas_tests`: 111 passed, 0 failed.
- `endurance_document_tests`: 112 passed, 0 failed, 6 expected fixture/export
  skips.
- `res_archive_document_tests`: 16 passed, 0 failed.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: required before handoff.
- `--ui-widget-check`: required before handoff.
- Package finalizer/distribution validator: required before handoff.
- `git diff --check`: pass.

## Focused manual checks

- Open an untouched Zone 2 and press **Shift+R** immediately. It must not report
  `terrain.working_type_mismatch`; all accepted repairs must form one undoable
  action.
- Undo and redo once, then press **Shift+R** again. The second run should be an
  exact no-op when no further unambiguous repair is available.
- Place both a Manual uniform tile and a Manual blend into semantically
  incompatible surroundings, then run **Shift+R**. They must receive no special
  immunity compared with equivalent authored or generated tiles.
- Repeat with a contextual stamp. A unanimous recovered corner field may repair
  it; an actually disputed vertex must remain unchanged.
