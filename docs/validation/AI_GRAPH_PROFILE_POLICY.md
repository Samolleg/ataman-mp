# AI graph profile-policy validation

Use only disposable copied maps and synthetic fixtures. Regeneration is an
editor diagnostic and does not silently replace an embedded or external graph.

## Implemented generation inputs

- all eight unit-profile layers are regenerated independently;
- terrain is sampled on the 0.5 m AI grid with the SEC three-corner sampling
  rule rather than generic bilinear filtering;
- layered sectors contribute their stored overlay-plane depth even where an
  overlay tile is inactive; an active liquid tile additionally supplies its
  terrain class, while land-family overlays do not masquerade as water;
- prone movement rejects any positive water depth, ordinary profiles compare
  depth with their own clearance, and the levitating profile follows the water
  surface;
- active-profile `tileDesc.reg` speed and cost values define terrain quality
  for the seven ground profiles; levitation deliberately ignores terrain-type
  traversal restrictions;
- visible FIG parts contribute their authored, complexion-adjusted navigation
  bounds after current part pose, object scale, rotation, and position;
- oriented authored footprints are rasterized directly instead of being widened
  to world-axis mesh boxes;
- `BASE*` parts provide a single bridge-deck navigation surface, while
  `CROWN*` and `EMPTY*` parts retain their non-blocking classifications;
- small, medium, and very-large footprint masks expand only impassability;
  they do not smear a slow terrain class into neighboring passable cells;
- ordinary and levitating slope limits remain distinct;
- graph-node placement uses the legal central fallback and prefers a local
  passable component that crosses the graph cell before terrain quality;
- link weights use the runtime terrain-class cost table, cardinal/diagonal
  distance factors, elevation multiplier, and serialization scale;
- a diagonal destination is legal when that destination is passable; blocked
  orthogonal neighbors do not add a separate corner-cut prohibition;
- borders, inclusive neighboring-cell search bounds, fresh graph serialization,
  consistency, and connected-component recoloring remain validated.

## Deliberate diagnostic boundary

If a visible FIG part lacks authored bounds, regeneration still uses its
rendered-volume bounds as a conservative fallback and reports that fallback in
the workflow log. The graph format is 2.5D: one XY cell can carry one selected
surface, so an elevated bridge deck and a simultaneous traversable underpass
cannot both be represented. The higher overlapping bridge deck wins, matching
the single-surface contract.

The currently selected body parts and current transient part poses are used.
Changes that alter which collision assembly or door pose is active therefore
require regeneration. The loaded game graph remains the authoritative
comparison for unusual multi-level assemblies and any profile-specific object
metadata that is not present in the loaded FIG assembly.

## Focused automated checks

- shallow water blocks prone while preserving levitating and taller-profile
  links;
- deep water blocks all seven ordinary layers while preserving levitation;
- configured impassable terrain types block all seven ground layers while the
  levitating layer remains terrain-type independent;
- a flat eight-AI-tile cardinal/diagonal crossing serializes with the expected
  64/90 route weights, preventing the former invented cost scale from returning;
- small, medium, and very-large profiles retain independent clearance;
- fences, authored overhead clearance, bridge decks over deep water, easy and
  ignored parts, walk slopes, and levitating slopes retain their expected
  topology;
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
4. Compare fence 568 and a known bridge. The fence must block the same profile
   layers as the loaded graph; the bridge must replace water/terrain height only
   across its authored footprint. Treat a multi-level underpass mismatch as the
   documented single-surface boundary, not as evidence that terrain policy is
   wrong.
