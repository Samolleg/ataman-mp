# Terrain paint modes R154: Safe Tile and conservative map repair

This revision fixes the Zone2 palette, Tile-target Safe mode, shape preview,
and Resolve Missing Blends regressions reported against R153.

## Changes

- Classic Auto now exposes exact authored one-on-three contextual stamps when
  a family has no uniform tile. Zone2 Grass / Irregular is available in Vertex
  target mode with its required support family, while Tile target hides stamps
  that cannot represent a complete tile interior.
- Tile target always uses the square/tile footprint. The circle shape cannot
  remain active or render a circular outline while Tile is selected.
- Classic Auto Safe in Tile target no longer paints a uniform core when any
  required boundary cell lacks an exact authored blend. Unsupported cores stay
  unchanged; independent exact regions may still paint.
- Resolve Missing Blends no longer majority-votes disputed shared vertices.
  It repairs only cells whose four shared-corner materials are unanimous, keeps
  ambiguous borders byte-exact, and remains one undoable action.
- Context-free map repair now reuses reviewed authored-derived masks for exact
  two-family signatures. It no longer drops the reviewed Grass/Soil style just
  because no brush is currently active.
- Mask review tooling can emit only live unapproved applications. The retained
  all-zone audit found one outstanding production mask topology:
  `stone-fracture / edge-2of4-v2`.

## Focused manual checks

1. Open Zone2 and select Classic Auto / Vertex. Confirm Grass / Irregular
   appears as a contextual stamp and paints only against its named support.
   Switch to Tile and confirm that stamp is hidden.
2. Select Tile target after choosing the circle shape. Confirm the viewport and
   shape controls show a square one-tile footprint.
3. With Classic Auto Safe and Tile target, paint where an exact boundary blend
   exists, then where one is missing. The exact case should paint; the missing
   case should stay unchanged without inserting a uniform hard edge.
4. On a copy of the reported Zone2 scenario, run Resolve Missing Blends. Only
   unambiguous missing blends should change; established borders and tile
   positions should remain fixed. Undo once and confirm the whole repair reverts.
5. Export a paint diagnostic if a generated two-family Grass or Soil blend is
   still reported. Its provenance should use an approved mask route.

## Known review item

`stone-fracture / edge-2of4-v2` remains mathematical pending the focused R154
questionnaire. No other live mask family/topology in the ten retained all-zone
review indexes lacks an approved provenance suffix.
