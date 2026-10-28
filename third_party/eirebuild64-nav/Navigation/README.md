# Nav64 X64NAVCORE candidate

The offline baker and game use the same Recast/Detour v1.6.0 source.
No fingerprint, content hash, stale-input check, legacy-graph fallback or
runtime configuration toggle participates in navigation. Refresh on mapper
save, release with --all and model replacement with --model NAME.

    EIRebuild64NavBake --game-root DIR --mod DIR --map zone1 --mob zone1 --output-root DIR --report-root PRIVATE_DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --all --output-root DIR --report-root PRIVATE_DIR
    EIRebuild64NavBake --game-root DIR --mod DIR --model NAME --output-root DIR --report-root PRIVATE_DIR

Modes are exclusive; --mob requires --map. Output goes to Maps/Nav64 below
output-root. Report-root is mandatory, outside that directory: all CSVs go
there, never into runtime data or the handoff. The optional offline-only
--tile-cells 32|64|96|128 compares bake layouts; normal release and the game
use 96 cells (9.6 m). Other layouts require a matching profile build and are
rejected by the game loader. Exit codes: 0 complete, 2 arguments, 3 setup,
4 map/build/data failure, 5 missing input. --all continues after map errors.
Consume BODIES, QUALITY, HERO_START, REBUILD, PROFILE, SOURCES, MAP_DONE,
MAP_ERROR/MAP_MISSING and BAKE_DONE. Compilation alone is not a passed pilot.

## Geometry and world changes

Static trees/stones/buildings use actual solid FIG triangles; BASE and
authored parts are walkable. Items, traps, stuff, torches and ordinary levers
are dynamic. Literal RemoveUnitFromServer/SetCP script NIDs are dynamic;
variable references can be marked in navsrc. Newly spawned objects not in
the baked catalogue are dynamic. Their real solid parts become oriented
boxes rasterized as non-walkable prisms; they never supply walkable BV tops.

Recast uses 0.1 m cells, 0.05 m vertical cells, climb 0.35 m, slope 40 degrees,
flight slope 60 degrees. Eight profiles retain four clearance tiers each.
MPR material/water class areas only restrict traversal. The 40/60 degree
slope filter applies to terrain and model surfaces for every profile. All profiles bake the losslessly decoded
imported source geometry, exactly as runtime support and tile rebuilds do. Ready files
are decoded by the runtime loader before baker quality queries.

No TileCache layers or DetourTileCache dependency remain. Regular Recast
compact fields, monotone regions, contours and polygon meshes produce ready
Detour tiles including multiple elevations. No detail-mesh samples are
stored: Detour triangulates the polygon fan, but no support or floor query
uses its height. Before contouring, compact spans are attributed to original
walkable triangles and regions are split by terrain/BASE identity. Polygons
retain that identity, with only directly connected source neighbours admitted
at voxel boundaries. Height selects the closest original floor after rejecting
lower skins with insufficient requested body clearance beneath an admitted
upper support. The same rule applies across touching polygon references;
query selection uses the requested pose height, not the profile minimum.
Original triangle interpolation supplies Z; intact terrain hides buried BASE.
Catalogue storage is shared by all profiles. Solid geometry never supplies a
floor ID. Raster area merging cannot promote a nearby non-walkable top.
Movement uses Detour's connected surface traversal (512 visited-polygon
bound), accepts at most 1 mm of endpoint roundoff, then checks original Z
against climb + tan(slope) * horizontal distance. A clipped move is rejected.
Layered tiles keep exact contours: simplifying terrain and BASE boundaries
independently can create overlapping polygons and a funnel shortcut outside
the mesh. Before source serialization, facing boundary edges within one
walkable BASE part close gaps up to one 0.1 m cell. Only upward <=40 degree
source faces participate. Edges must face outwards, be nearly antiparallel,
overlap along their lengths and differ by at most 0.35 m at the seam.
Each half of the gap retains its own exact source plane; a step remains a
step. No convex hull, solid top, terrain, other object or steep riser becomes
a walkable floor. Seam triangles retain the owning part/object for removal.
The declared BASE may interpolate its walkable source plane over at most
0.25 cell (2.5 cm); its source catalogue includes that same margin. Terrain
retains its strict 1 mm footprint. Release support, delta-Z, terrain and
client checks keep their original thresholds and also cover the seam planes.
The local client resolves interpolated XY against the same server-owned mesh;
it never replaces the selected floor with the highest expanded bridge BV.
Contour error is selected from the compact region topology before contouring:
0.25 cell on tiles with holes (Euler characteristic), otherwise one cell.
A one-cell error was observed collapsing a hole and misclassifying its outline.
If this produces multiple outlines for a region, the tile selects monotone
partitioning followed by connected monotone row strips and exact contours
(zero simplification error). Bad/collapsed outlines select the same exact
construction. Surface border vertices are retained to preserve provenance. Recast merges interior regions even with zero
merge area; separate row runs cannot form holes and preserve walkable spans. MONOTONE_TILE lists each selected tile
and profile in the baker log. The same selection runs during worker rebuilds.
Any remaining multiple outline or other Recast error rejects the tile.
No library sources are modified.

The game reads ready tiles and one shared navgeom, indexes source triangles
by tile and static object, and queues affected old/new footprints. A single
worker rebuilds only those tile/profile pairs from retained triangles,
excluding removed static NIDs and rasterizing current oriented dynamic boxes.
Snapshot jobs whose tile was dirtied again are discarded. Worker Recast
state, temporary mesh/query and allocations are destroyed on the worker;
the simulation thread only swaps a preallocated tile, refreshes local door
flags and invalidates references by generation. At most one completed tile
is applied per frame. Actual worst-case frame timing needs gameplay evidence.
On zone load, current dynamic obstacles and absent static objects are applied
before placement. Mesh state and process-local references are not saved.
Loaded positions use the same placement resolver: at most 1 m horizontally,
nearest saved world height, with surface identity used only to break floor ties.
Re-resolve polygon references and actual terrain/BASE height; do not compare
the exact game height with the approximate Detour height as a save-load gate.
Unavailable placement stops the derived move and is journaled, without rejecting
the saved world. The existing serialized address fields remain advisory.

Door material 15 changes flags without rebuilding. Door updates visit only
aperture tiles. Replacements restore current door states and clearance flags.
The baker logs actual worker/apply timing at the zone1 start tile; this does
not establish the worst case for every campaign tile or 100 creatures.

## Authoring

Optional Maps/<mpr>.navsrc is UTF-8 JSON version 1:

    {"version":1,"dynamic":[10000001],"static":[],"walkable":[{"id":123,"part":"base"}],"doors":[{"id":456,"min":[10,20,0],"max":[11,21,3],"open_state":1}]}

Static/dynamic IDs cannot overlap. Part names are lower case. Explicit
static policy overrides literal script classification. Door apertures are
world-space boxes, open_state is the actual lever state. Authored apertures
override automatic MOB isDoor detection. Doors lacking a solid panel in
their authored state need an explicit aperture. Mapper UI integration is
separate from this baker contract.

## Wire format

- <mpr>--<mob>.<profile>.nav64: container 8, algorithm 21, Recast 0x010600,
  DT_POLYREF64, names/profile, ready shuffled/LZMS tiles and surface schema.
  In the expanded tile stream, each tile blob is followed by a u32 polygon
  support count (must equal polyCount). Each polygon has u32 source-count and
  that many u64 source IDs (low/high u32), primary first, connected neighbours
  afterwards. These records belong to that tile's polygon order and move with
  tile replacement. Readers of container 7 must reject container 8.
  No layer section exists. Loader validates exact profile, bounds and ABI.
- <mpr>--<mob>.navworld: JSON version 3, baked static NIDs and initial dynamic obstacle boxes.
- <mpr>--<mob>.navgeom: shared NSRC version 5 (four parallel LZMS blocks), inner section 4,
  original bounds and indexed common float32 XYZ vertices, losslessly stored;
  per-triangle indices, float32 water depths, byte material/class metadata,
  static object and surface IDs. Centimetre rounding is not used: it changes
  thin BASE planes and slope classification. NSRC 4 is rejected;
  door apertures and reserved off-mesh endpoint/surface/radius/flag records.
  Includes precomputed triangle/surface/object tile indices. One copy serves
  all profiles. No source import or spatial binning occurs during gameplay.
- Surface 1 is terrain. BASE IDs encode (unsigned NID << 32) | (part + 2).
  Placement/step/restore and the local client resolve original terrain/BASE
  triangles through polygon provenance. Gameplay acceptance remains separate.
  Off-mesh records remain reserved, not linked.
- bodies.navbody: BODY version 1, algorithm 21, resource-derived models and
  full morph grids, including non-un* models actually used by MOB creatures.
  Runtime never measures bodies. Save field sizes and movement speeds remain.

## Acceptance

HERO_START must advance by 0.02 m in shared Project/Find/Constrain code; the
baker fails if it does not. QUALITY preserves total gained_water_m2 and
separately reports gained_forbidden_water_m2 under the original depth rule.
CSV reports include losses, water reasons, BASE connections, placements and
private object/vegetation identity. Zone1 release validation walks complete
paths in 5 cm moves and 17 cm ticks across corners, using the runtime surface
traversal. The original 56 profile/map pairs plus journal-derived ruin-wall,
mountain and NPC-height routes check each accepted step/tick against the
climb/slope height budget, unrounded source support, terrain and client floor.
The six previously rejected support samples and both ramp seam samples are
also checked at their exact XY with the 0.35 m placement height budget.
HEIGHT_CONTINUITY reports counts/errors; a discontinuity prints coordinates
and the independently resolved client floor.
A failed step or discontinuity on a complete route rejects the map. Unreachable
goals remain explicitly reported as no_path. Shared loader time is measured
including reconciliation with the imported live objects, without a game tick.
Runtime performance, save/load, queues/doors, full random-goal coverage,
common-radius class policy and in-game client floor acceptance remain
separate acceptance items. See the private REPORT for measured release data.

The baker accepts --replay-input PRIVATE_JSON for journal-derived release inputs.
Records are an array: scene [mpr,mob], kind (step_rejected/query_failed/walk),
unit, source/goal [x,y,z], optional profile/height/max_length. Required walk
records additionally set name, required=true and optionally neighbour=true.
The private report records Place/Find/Step outcomes and source-floor evidence.
Walk records run CAIMap::NewPath/CalcSpeeds/CreatePathSegment, CRouteWalker and
NavigationWorld::Move shared with L1ExecMove, then the client support resolver.
One iteration is one game tick. Neighbour release records exercise waiting and
bounded avoidance; a route that never encounters the neighbour is a failure.
No scripts, animations, renderer or game session are started.

All emitted polygons must be clockwise and convex in Detour XZ. A tile whose
simplified contour yields an inverted face is partitioned into exact compact
row runs before publication. Invalid output is rejected. Step traversal clips
the actual segment against linked convex polygons and exact cross-tile portal
edges; it retains the 1 mm numerical allowance and original height limits.
The server publishes frozen route clocks during queue waiting so the client
does not keep advancing the old route while the server stands still.

Standing-unit queue queries wait 27 game ticks (1.485 s at the default tick),
then search once around standing bodies using query-local circumscribed rings.
Each visibility edge is a normal Find corridor, checked against every supplied
body, with material costs and max length preserved. No mesh flags are changed.
At most four obstructing rings (50 vertices) bound the secondary query; an
unavailable/limited detour waits without repeated queries. Moving/resized bodies,
profile/pose changes and navigation generation changes allow another query.
Release walk records may set neighbour_distance, release_after_wait (0 keeps
the neighbour standing) and require_detour. Their results include detour_max_ms.
