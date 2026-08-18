# EI Mapper Endurance 0.3.0-alpha.26 modern-atlas r7 test

## Corrected after r6

- Fixed the single-MMP extraction success test. R6 decoded configured terrain
  pages but discarded the successful result, leaving the Paint Land Tiles
  palette empty and logging `0 land materials, 0 overlay materials`.
- A family is now a palette group, not a collapsed vertex identity. Visually
  distinct uniform materials such as `Grass / Dark` and `Grass / Fresh` are
  separate brushes with separate thumbnails and deterministic IDs, displayed
  together under the `Grass` heading.
- Only repeated authored occurrences of the same exact material variant are
  deduplicated across map atlases. Different colors or appearances are never
  silently treated as the same paint material.
- Exact generated transitions operate between the selected material variants,
  including two variants in the same family when necessary.

## Automated validation

- `terrain_virtual_atlas_tests`: 30 passed, 0 failed.
- `endurance_document_tests`: 84 passed, 0 failed, 3 fixture-dependent skips.
- Release build completed and `--ui-widget-check` passed, including the new
  `materialVariantsRemainDistinctAndGrouped` palette regression.
- Zone 2's transition-only `Grass / Irregular` identity is still proven to map
  to a game-wide authored uniform source rather than becoming an unavailable
  or synthetic brush.

## Focused manual checks

1. Open Paint Land Tiles on Zone 1 or Zone 2. Confirm material thumbnails are
   present rather than an empty palette.
2. Under the Grass heading, confirm visually different Grass variants appear as
   separate selectable cards.
3. Paint two Grass variants next to each other, then paint Stone across both.
   Every touched vertex must keep the selected material and generated boundaries
   must appear immediately on mouse release.
4. Repeat an identical boundary and confirm the generated-tile count does not
   increase.

## Known limits

- Finalization remains capped at the known-safe 512 slots and refuses rather
  than substituting a material when the compacted atlas cannot fit.
- Human visual acceptance of generated game-art transitions remains a manual
  review step.
