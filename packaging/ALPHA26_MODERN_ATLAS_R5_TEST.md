# EI Mapper Endurance 0.3.0-alpha.26 modern-atlas r5 test

## Changed

- Terrain tile changes resolved on mouse release now rebuild their sector GPU
  buffers while the editor OpenGL context is current. A generated four-corner
  tile should therefore become visible on that release, without another paint.
- The visual-test corpus is the same real-map set used by the semantic-atlas
  work: Zone 1, Zone 2, Zone 5_1, Zone 11, Zone 16, and Zone 17.
- The corpus contains the real-map background-page, cross-stripe, and
  nested-circle MPR cases plus its manifest and `panels.csv` review index.
- The earlier procedural/synthetic terrain preview corpus is not shipped.

## Manual checks

1. Configure the normal JamaisVu135 resources, then open cases under
   `semantic-atlas-visual-mprs` and use `panels.csv` to select a Zone 2
   cross-stripe or nested-circle case for visual inspection.
2. On a real Zone 2 map, paint one ordinary material identity across another.
   Release the mouse over a place requiring a newly generated multi-corner
   tuple. Confirm the final tile appears immediately on release.
3. Continue painting the same boundary and confirm the generated-tile count
   does not grow when an identical tuple is reused.
4. Finalize only a disposable map copy. Confirm the isolated sibling
   `<map>-atlas.res` is created and that reopening the MPR preserves the result.

## Known limits

- Finalization remains capped at the known-safe 512 slots. It refuses rather
  than substituting a material when the compacted atlas cannot fit.
- Automated tests prove structural corner identity, seams, save/reopen, and
  deterministic generation. Human visual acceptance of game-art transitions
  remains a manual review step.
