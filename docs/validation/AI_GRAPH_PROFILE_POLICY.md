# AI graph profile-policy validation

Use only disposable copied maps and synthetic fixtures. Regeneration is an
editor diagnostic and does not silently replace an embedded or external graph.

## Implemented generation inputs

- all eight unit-profile layers are regenerated independently;
- terrain is sampled on the 0.5 m AI grid with the SEC three-corner sampling
  rule rather than generic bilinear filtering;
- active overlays contribute water depth only when their referenced MPR
  material is explicitly water; land-family overlays remain non-water;
- prone movement rejects any positive water depth, ordinary profiles compare
  depth with their own clearance, and the levitating profile follows the water
  surface;
- active-profile `tileDesc.reg` speed and cost values define terrain quality;
- small, medium, and very-large footprint masks expand impassable terrain;
- ordinary and levitating slope limits remain distinct;
- graph-node placement prefers higher-quality terrain, and link weights include
  terrain quality, diagonal distance, and elevation cost;
- borders, diagonal corner cutting, fresh graph serialization, consistency,
  and connected-component recoloring remain validated.

## Deliberate diagnostic boundary

The editor does not yet decode the authored navigation volumes attached to
figures. Regeneration therefore rasterizes current rendered bounds as
conservative hard obstacles. It cannot yet distinguish hard, soft, easy,
danger, or bridge volumes, and cannot raise the navigation surface onto an
authored bridge. Whenever such proxy bounds are present, the workflow log
states that object collision is approximate. The loaded game graph remains the
authoritative comparison for those cases.

Exact object parity requires an independently validated reader for authored
collision volumes and passability classes. Until then, generated previews must
not be described as source-exact around bridges, doors, overhangs, or complex
multi-level objects.

## Focused automated checks

- shallow water blocks prone while preserving levitating and taller-profile
  links;
- deep water blocks all seven ordinary layers while preserving levitation;
- configured impassable terrain types block every layer;
- small, medium, and very-large profiles retain independent clearance;
- fences, overhead clearance, walk slopes, and levitating slopes retain their
  expected topology;
- synthetic terrain traversal registries are parsed without shipping private
  profile resources.

## Manual comparison

1. Open a disposable Zone 1 copy containing the known river and show the loaded
   graph. Capture the prone, crawling, human-sized, and levitating layers.
2. Regenerate. Prone must have no river-crossing link. Compare shallow-water
   behavior for the taller ordinary layers and water-surface behavior for the
   levitating layer.
3. Compare the mountain test location. Ordinary layers must not connect a
   summit directly down an inaccessible cliff; levitation may retain only links
   within its separate slope policy.
4. Compare fence 568. A conservative regenerated block is expected. Treat any
   bridge, doorway, or complex-volume mismatch as the documented object-metadata
   boundary, not as evidence that water or terrain-profile policy is wrong.
