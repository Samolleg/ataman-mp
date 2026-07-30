# Modern terrain atlas R33 tester changelist

R33 incorporates the latest reviewed semantic family catalog and the first
direct pass over the R32 blend-mask review and paint-refusal reports.

## Changed

- Stroke release now preserves the selected paint material through command
  composition. Equal-coverage generated transitions use the painted material's
  runtime terrain class instead of refusing the stroke.
- Grass subfamilies share the reviewed grass mask pool. Exact authored tiles
  are still reused first, but an absent transition no longer privileges a bad
  mask merely because it came from the same subfamily pair.
- One-corner masks that leak onto the opposite edge or run almost to the end of
  an adjacent edge are rejected. This targets the reported L-shaped cuts and
  incorrect inside corners without introducing synthetic mask art.
- Paving and slab generation prefers substantial broken rock/gravel
  silhouettes over straight course and radial-cut masks. Sand prefers smoother,
  lower-complexity stock silhouettes.
- The material catalog is synchronized to the latest reviewed sibling data:
  502 map-local families, 62 transition-support families, and 70 global land
  brushes after grouping uniform variations by family/subfamily.
- Uniform authored variants remain selected once per stroke. A new default-on
  option also varies their quarter-turn rotation between strokes; transition
  tile rotations are never randomized.
- The selected palette card now has a bright lime outline. Right-click and hold
  on terrain to inspect the physical slot, rotation, uniform brush or blended
  corner identities, authored/generated provenance, and runtime terrain type.
- Generated slot terrain types remain explicit in working and finalized MPRs:
  corner-majority wins, a tie uses the material being painted, and unknown
  classes refuse rather than silently becoming Grass.

## Included visual collections

- **Final material stripes** regenerated from the installed Zone 2 JMV atlas.
- **Every material radius-3 circles** regenerated from the same reviewed brush
  set and real JMV texture pool.
- The established Zone 1, 2, 5_1, 11, 16, 17, and 18 real-map collections are
  retained for comparison. EI-HD reference assets are not included.

## Focused manual checks

1. Paint several overlapping strokes on a default JMV map, including a 2/2
   terrain-class tie. No stroke should report `terrain.generated_type_tie`.
2. Review Grass Winter/Sparse/Dark/Faded, Ice, Stone Black/Grayish/Mossy,
   Slabs, Rock Snow, Rubble Orange/Red, Sand Common/Dark, and Paving in both
   included visual collections. Report any wrong family or remaining cut edge.
3. Toggle **Vary uniform texture rotation between strokes**. Each stroke should
   use one coherent uniform variation/rotation; its edge blends must keep their
   corner-required rotation.
4. Right-click and hold an interior tile and an edge tile. Confirm the inspector
   identifies the brush/runtime class and lists all blended corner identities.
5. Finalize and reopen the MPR, then stage it manually. Confirm the palette is
   assigned and terrain slots remain stable and textured.

## Known limitation

Automated checks reject known mask pathologies and prove exact semantic corners,
runtime type propagation, reopen validity, and zero unresolved generated tuples.
The artistic quality of the revised stock-derived mask choices still requires
the visual review above.
