# Alpha.26 AI Graph Parity Test — Revision 4

Revision 4 fixes the Zone 9 precision regression reported against Revision 3.

## What changed

- Restored the runtime floating-point operation order used to quantize terrain
  and object heights. Zone 9 now has zero topology, route-weight, and packed
  node-position differences on every movement layer.
- Added a regression test for the Zone 9 sample that previously changed 62
  directional weights and one packed position.
- Automated corpus captures now explicitly force regeneration instead of
  potentially preserving the graph loaded from the map.
- Retains Revision 3's aligned controls, explicit Loaded/Generated selector,
  loaded-versus-regenerated diff report, graph baking/export, and runtime
  staging behavior.

No changes are made to `addon.dll`, `game.exe`, or installed map files by graph
preview regeneration.

## Focused test

1. Open `zone9.mpr` and `zone9.mob`; wait for the MOB load to complete.
2. Confirm the preview initially selects the embedded graph.
3. Click **Regenerate preview** and wait for **Preview graph: Generated**.
4. Click **Diff loaded / regenerated**. Every layer should report zero missing
   links, extra links, changed weights, and changed positions.
5. Toggle between Embedded and Generated while the graph is visible and inspect
   the previously affected area near world coordinates `(274, 34)`.
6. Use **Test Current Map** with Generated selected and exercise ordinary and
   large-unit routes where practical.

## Broader smoke test

Repeat regeneration and diff on two or three previously exact maps, then test a
bridge, shoreline, fence, or large gate. Report the map, layer, coordinates,
route direction, and screenshot for any discrepancy.

## Component-label note

The diff compares the route-defining planes: directional connectivity/weights
and packed node positions. Connected-component identifiers are a separate
reachability-cache plane. Pure ID renumbering is irrelevant; some native graphs
also reuse an ID across small disconnected border fragments. Generated graphs
use internally consistent component partitions, so complete `.grp` files need
not be byte-identical even when the reported navigation difference is zero.

Autozones remain excluded from editor generation because the game updates them
when they load. `zonemichel` remains outside the audit as a debug location.
