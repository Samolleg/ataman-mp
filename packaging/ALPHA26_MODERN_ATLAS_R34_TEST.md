# Modern terrain atlas R34 tester changelist

R34 applies the latest reviewed Zone 2 semantic catalog and tightens the
stock-derived mask selection behind generated exact transitions.

## Changed

- Zone 2 Faded and Burnt anchors/corners now use the latest reviewed sibling
  data, including the corrected Burnt preview and the reviewed Brown/Faded
  transition corners.
- A mask rejected for the material being painted can no longer re-enter through
  a different source material style. This specifically removes previously
  rejected Ice transfers from generated Ice transitions.
- One-corner masks are rejected when even a soft visible strand reaches the
  opposite edge, or when the material runs along more than three quarters of an
  adjacent edge. This targets the reported Grass Winter overreach and harsh
  cross-tile strands without inventing synthetic artwork.
- Paint-issue reports now include the physical tile pixel hash. The included
  map manifests record every physical slot's source/generated provenance,
  semantic corners, pixel hash, runtime terrain type, and the packed cell grid,
  so a finalized/repacked slot is no longer mistaken for an original Zone 2
  identity.
- The retained Qt 5.12.12 MinGW build now compiles the resolve-after-stroke
  queue using C++11-compatible construction.

## Included visual collections

- **Final material stripes** regenerated from installed JMV stock assets: 452
  active slots and zero unresolved cells.
- **Every material radius-3 circles** regenerated from the same reviewed brush
  set: 579 active slots and zero unresolved cells.
- The established real-map Zone set remains included for comparison. No EI-HD
  assets are shipped in the editor or test maps.

## Focused manual checks

1. Open the stripe and circle collections and review Grass Winter/Faded/Burnt,
   Ice/Snow, Sand, Paving/Slabs, Rock, Rubble, and Stone edges. Report a tile by
   marking the smallest visibly wrong region; R34 reports carry exact physical
   pixel identity after repacking.
2. On a default JMV map, paint overlapping radius-3 circles and inward strokes.
   Requested vertices must remain the selected brush, with no refused stroke
   and no one-corner mask extending through the opposite corner.
3. Finalize, reopen, and stage a painted MPR. Confirm the material palette is
   assigned, textures remain stable, and generated terrain types agree with the
   working preview.

## Known review boundary

The provenance audit found all eleven reported uniform source anchors to be
genuine four-corner uniforms. Zone10 slot 398 (Ground / Dry) shares corner
pixels with Slabs / Square transitions, but whole-tile evidence still supports
Ground / Dry; it has not been reassigned without human confirmation. Artistic
mask quality still requires the visual checks above.
