# EI Mapper Endurance 0.3.0-alpha.26 modern-atlas r6 test

## Changed

- Terrain brushes are now global material families backed only by authored
  uniform four-corner tiles. Atlas transition fragments are no longer exposed
  as brushes.
- Variants with the same theme are grouped into one family. For example,
  Grass / Dark, Grass / Common, and Grass / Fresh appear as one Grass card.
- The palette separates families into `Used on this map` and `Unused families`.
  Painting an unused family moves it to Used; replacing its final map vertices
  moves it back to Unused.
- A family absent from the current map can be sourced from the configured game
  resources. Painting it creates a pure tile and exact 1/3, 2/2, 3/1, or
  four-family transitions as needed, then reuses identical generated tuples.
- Local atlas identities are translated into the global family field before a
  stroke. In particular, Zone 2's transition-only Grass identity now resolves
  through the real global Grass source instead of rejecting or rolling back
  the stroke.
- Every vertex touched during a stroke remains the selected family. Resolution
  on release may change supporting vertices and physical tiles, but it cannot
  silently drop requested vertices.
- The generated tile count remains visible in the terrain tool.

## Automated validation

- `terrain_virtual_atlas_tests`: 30 passed, 0 failed.
- `endurance_document_tests`: 84 passed, 0 failed, 3 fixture-dependent skips.
- Release build completed and `--ui-widget-check` passed.
- Coverage includes absent-family pure/mixed generation, exact 1/3, 2/2 and
  four-family tuples, orientation, seams, rotation/pixel dedupe, stable reuse,
  map-wide usage, unused replacement, capacity refusal, namespace isolation,
  save/reopen, no-op preservation, and the Zone 2 transition-only regression.

## Manual checks

1. Configure the normal JamaisVu135 resources and open a Zone 2 case under
   `semantic-atlas-visual-mprs` (the same real-map set used by the semantic
   atlas work).
2. Confirm the Ground palette shows theme-level families such as Grass, Stone,
   Ground, Sand, and others, split between Used and Unused. It should not show
   separate dark/common/fresh cards.
3. Paint Stone across heterogeneous grass/ground terrain. On mouse release,
   every touched vertex must remain Stone and its mixed boundary must appear
   immediately, without a second paint action.
4. Paint a family from Unused and confirm it moves to Used. Repaint all of that
   family's vertices with another family and confirm it returns to Unused.
5. Repeat an identical boundary and confirm the generated-tile count does not
   increase.
6. Finalize only a disposable map copy. Confirm the isolated sibling
   `<map>-atlas.res` is created and reopening preserves the result.

## Known limits

- Finalization remains capped at the known-safe 512 slots and refuses rather
  than substituting a material when the compacted atlas cannot fit.
- Unused global families require their source MMPs to be available through the
  editor's configured game/mod resources; game artwork is not embedded here.
- Automated tests prove identities, topology, deterministic generation, and
  persistence. Human visual acceptance of generated game-art blends remains a
  manual review step.
