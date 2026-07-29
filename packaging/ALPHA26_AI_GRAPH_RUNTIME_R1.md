# 0.3.0-alpha.26 AI graph runtime-policy R1

## Changelist

- Regenerated navigation now uses the game's quantized terrain heights,
  per-profile footprints, slope limits, water clearance, terrain classes,
  bridge/object clearance, node selection, and route-weight scale.
- Flying terrain policy is separate from the seven ground profiles.
- Layered-sector overlay depth and active liquid terrain classes now feed graph
  regeneration consistently.
- Test Current Map keeps using the existing temporary graph-staging path; this
  build changes the generated graph content, not MPR save serialization.

## Focused manual check

1. Open a disposable copy of `zone1thunderbluff.mpr`, regenerate the graph, and
   compare the human-sized layer with the terrain you can actually traverse.
2. Use **Test Current Map** with the regenerated preview. Check long-distance
   movement across ordinary terrain, then confirm cliffs remain disconnected.
3. On a normal Zone 1 collection, compare loaded versus regenerated prone,
   crawling, human-sized, and flying layers at the river, a fence, and a bridge.
4. Toggle **Loaded graph override** on and off. Confirm the preview changes
   source without forcing an unnecessary regeneration when the generated graph
   is still current.

## Known limitation

Generated graph delivery remains temporary for Test Current Map. Persistent
external `.grp` export and explicit embedding into a chosen MOB are pending.
