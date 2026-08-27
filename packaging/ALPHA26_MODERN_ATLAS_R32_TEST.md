# Modern terrain atlas R32 tester changelist

R32 integrates the reviewed blend-mask feedback and preserves Evil Islands MP
terrain classes through generated atlas slots, live preview, finalization, and
AI graph preview regeneration.

## Changed

- Stock-derived masks keep their native 256×256 detail; the previous blur pass
  is removed. Substantial authored fragments survive cleanup, while pixel-sized
  grain, large detached square blocks, and near-solid masks are rejected.
- Sources rejected in the mask questionnaire are excluded by material style and
  blend scenario, including the reviewed paving/stone cut sources.
- Every generated physical slot now receives an explicit runtime terrain type
  (0..15). Corner-majority wins; a 2/2 class tie uses the material painted by
  that stroke. Rotation cannot change the result.
- Pixel deduplication includes runtime terrain type, so identical artwork with
  different ambience/traversal behavior remains in separate physical slots.
- Live working MPR packages and finalized MPRs carry the same complete MP type
  vector. Unknown types refuse instead of silently becoming Grass.
- Visual variations whose gameplay type conflicts with their grouped brush are
  excluded with a warning, preventing random strokes from changing traversal.
- Terrain edits automatically refresh an already-generated AI graph preview;
  an authored loaded graph remains a static user-selected override.

## Included visual collections

- **Final material stripes**: 67 reviewed brushes, 441 required slots, 261
  generated exact tiles, 84 stock blends reused, and zero unresolved cells.
- **Every reviewed material family**: one isolated family stamp per brush, 412
  required slots, 261 generated exact tiles, and zero unresolved cells.
- Zone 1, 2, 5_1, 11, 16, 17, and 18 material-stamp collections using the same
  real-map review set as semantic-atlas work.

These are stock JMV maps/textures with isolated generated MPR/RES pairs. The
separate EI-HD reference set is not included.

## Focused manual checks

1. Open **Final material stripes** and compare stone/paving, sand, rock, grass,
   snow/ice, and water edges. Use **Mark bad tiles on map** for any wrong mask,
   family, or rotation.
2. Open **Every reviewed material family** and verify every brush paints only
   its displayed uniform Family / Subfamily identity.
3. On a normal JMV map, paint radius-3 circles of several materials over one
   uniform background. The interior and every outer/inside corner must retain
   the brush identity without sharp quadrant cuts.
4. Regenerate the AI graph preview, paint a material with a different runtime
   class, and confirm the generated preview refreshes. Switching to the loaded
   graph override must keep the authored graph static.
5. Finalize, reopen the generated MPR, and confirm the palette remains assigned,
   the map is textured, and staging does not animate or drift atlas slots.

## Known limitation

Automated checks prove exact semantic corners, seam continuity, class metadata,
reopen validity, and zero unresolved cells. Blend artistry and reviewed family
membership still require the human checks above.
