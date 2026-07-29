# Modern terrain atlas R28 tester changelist

This build focuses on finalized-atlas reopen correctness, fast cached strokes,
and reviewable real-JMV material tests.

## Fixed

- Finalized semantic profiles are now bound to the actual post-DXT physical MMP
  pages, including page padding. Reopening a finalized MPR no longer rejects its
  profile as a slot-count/hash mismatch and reports an empty palette.
- The bundled material-stripe and material-circle collections now include their
  adjacent semantic-atlas profile sidecars.
- Repeated strokes whose exact tiles are already available skip the map-wide
  usage scan and atlas upload. Missing signatures are generated only once per
  unique corner tuple during a stroke.
- Finalization shows a blocking progress dialog while it audits references,
  resolves transitions, evicts unused slots, and rebuilds physical pages.
- Stock-derived transition masks retain their broad tonal ownership ramps and
  use the full shared edge, avoiding binary inside corners and narrow sharp
  transition bands.
- The latest committed reviewed terrain-family corrections from the semantic
  atlas audit are included.
- Finalization keeps only safe remapped authored animation ranges; generated,
  imported, partially retained, or non-contiguous ranges are removed so remapped
  terrain slots cannot accidentally animate through unrelated tiles.

## Included visual tests

- `Modern Atlas - Material Stripes`: 438 assigned tiles in 448 physical slots.
- `Modern Atlas - Material Circles`: 551 assigned tiles in 576 physical slots.
- Both maps are generated from installed Jamais Vu terrain resources and report
  zero unresolved exact blends.

## Focused manual checks

1. Open both included collections. Confirm the palette is populated and the
   assigned/physical counts match the values above.
2. Repeat a previously resolved stroke. The log should report
   `uniqueMissing:0 usageScanMs:0`, and release should be near immediate.
3. Paint radius-3/4 circles for Fresh/Irregular grass, Rock/Greenish, Stone over
   dark rock, and Ice/Common. Check outside and inside corners for broad blends,
   correct material identity, and no one-pixel grain or cyan artifacts.
4. Finalize a painted map, reopen the produced MPR, and confirm its assigned
   count and brush palette remain populated.
5. Stage that reopened map in game and check that no dark-grass tiles cycle
   through unrelated textures.

## Remaining runtime proof

The editor and automated tests do not launch the game. The animation-range and
physical-profile regressions pass headlessly, but the drifting-texture fix still
needs the staged visual check in step 5. Some material-family identities remain
under active catalog review; report any brush whose uniform center or edge uses
an unrelated family with the brush name and screenshot.
