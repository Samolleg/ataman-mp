# Nav64 offline data contract (X64NAV01 r13)

RecastNavigation v1.6.0 (zlib), unchanged sources; pin/archive digest:
third_party/recastnavigation.lock.json. Recast and Detour use DT_POLYREF64.
Algorithm 8, container 3, library 0x010600, SaveSchema 4.

The game ONLY reads Maps/Nav64 in the active mod. No navmesh generation on
zone entry, no Cache64/Nav reads/writes, no fallback to the legacy graph.
Missing, stale or incompatible data rejects the zone before publication and
names its MPR/MOB/profile with a request to re-bake in the mapper.
Old Cache64/Nav files are unused; this task does not remove installed files.

## Production baker / mapper interface

EIRebuild64NavBake.exe is a console target in EI_Rebuild.sln. Its build first
builds EI_Rebuild, then links the SAME engine objects with NavBakeMain.cpp.
No second MPR/MOB/BASE extractor, body measurement or profile implementation.
No renderer/audio/window/server startup. MOB scripts are neither compiled nor
executed. ResourceScope resolves the effective mod archives without entering a
mod simulation session; only package lookup tables are loaded.

Example (paths containing spaces require quotes):

    Engine64/EIRebuild64NavBake.exe --game-root "C:/GOG Games/PZ" --mod MODS/Dejavu --all --output-root "D:/nav-release/MODS/Dejavu"

One map:

    Engine64/EIRebuild64NavBake.exe --game-root "C:/GOG Games/PZ" --mod MODS/Dejavu --map zone1 --mob zone1 --output-root "D:/nav-release/MODS/Dejavu"

--game-root, --mod and --output-root are required. --mod may be relative to the
game root. Output goes to <output-root>/Maps/Nav64. --map and --mob may include
extensions; --mob defaults to the --map stem. Use either --all or --map.
--all reads the effective texts.res/map.txt using CGlobalMap::LoadZones, includes
the menu and both zone6/zone6_2 terrain states selected by WorldInterface,
deduplicates MPR/MOB pairs, and continues after a map error.

Exit codes: 0 = all requested outputs written; 2 = invalid arguments;
3 = package/database/registration initialization failure; 4 = one or more map
imports, builds or output writes failed, or campaign size exceeds 400 MiB;
5 = some registered MPR/MOB sources are absent, all other maps were processed.
Code 4 takes precedence over 5 if both occur. No map-size limit applies.
Missing sources are resolved through the SAME effective mod/Base map lookup
as the importer, before importing. MAP_MISSING identifies MPR/MOB, code=5,
mpr_present/mob_present and reason=no_sources. It does not change registrations
or replace a missing map. Other data/import errors remain MAP_ERROR code=4.
stdout is flushed after MAP_BEGIN, PROFILE, MAP_DONE and BAKE_DONE records;
stderr contains MAP_MISSING/MAP_ERROR/BAKE_ERROR. PROFILE gives type, radius, tiles, wall
ms, raster/polygon CPU ms, bytes and fingerprint. MAP_DONE gives import and
total wall ms and the sum of eight profile file sizes. BAKE_DONE gives
counts (completed, missing, errors), total wall ms and total bytes. Every
requested pair belongs to exactly one of those three counts. BOUNDS_NORMALIZED records
MPR/MOB/NID/model/part for every reversed finite solid BV, once per map part.
Fields are space-separated key=value; names are resource names without spaces,
reason is the remaining text after reason=. Times are integer milliseconds,
bytes are decimal byte counts. The mapper must check exit code and BAKE_DONE,
not infer completion from a PROFILE record alone.
A failed map is not reported as complete. Each nav64 file is written through
a flushed temporary file and MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH).

Filename: <mpr-stem>--<mob-stem>.<profile-name>.nav64.
Names are lowercase ASCII resource basenames, with .mpr/.mob extensions in
the header. Each file embeds BOTH source names and the profile ID; the mapper
matches that triple against its open MPR/MOB pair and requested profile,
then checks the fingerprint. Do not infer a map solely from a filename/hash.
Renaming a file cannot change its embedded identity. The game checks the same
triple before adding tiles. Older containers/algorithms must be re-baked.
The container embeds a SHA-256 of effective MPR header/sectors/material
registry, raw primary MPR/MOB, original object model/BON payloads (including
objects absent from a saved world), authoring .navsrc bytes (explicitly empty
when absent), object constitution quantization setting, profile,
coordinate conversion, algorithm/library/container/ABI settings. Script-only
secondary MOBs do not add geometry. A primary MOB edit requires re-baking.
No file dates, installation paths, transient IDs or EXE hashes enter the key.
.navsrc authoring semantics remain NAV03; this slice fingerprints the sidecar.
Geometry is assembled in the same stable object order in both executables.

## Fixed profiles and body/pose selection

| Type | Radius m | Clearance tiers m | Cell m |
|---|---:|---|---:|
| tiny | 0 | 0.2 / 0.4 / 1 / 1.6 | 0.1 |
| small | 0.1 | 0.2 / 0.4 / 1 / 1.6 | 0.1 |
| human | 0.2 | 0.4 / 1 / 1.6 / 2.2 | 0.1 |
| large | 0.5 | 0.4 / 1.6 / 2.2 / 3 | 0.1 |
| giant | 1 | 0.4 / 1.6 / 3 / 6 | 0.1 |
| levitate_tiny | 0 | 0.2 / 0.4 / 1 / 2 | 0.1 |
| levitate_small | 0.2 | 0.2 / 0.4 / 1 / 2 | 0.1 |
| levitate | 0.5 | 0.2 / 0.4 / 1 / 2 | 0.1 |

Radius uses half the FIG torso/shoulder width in the current constitution,
not GetCollisionDistance or database dimensions. Distal arms, weapons and
armor do not enlarge width. Models without standard torso names use remaining
body vertices. Missing idle/pose clips use that model's bind transforms.
Choose the largest fixed radius <= the body. A zero-radius minimum avoids
enlarging creatures narrower than 0.1 m. A body lower than the lowest clearance
receives that tier; no exception. Other tiers round down, capped by the old AI
class permission. Missing model/FIG/BON remains an explicit resource error;
corrupt/nonfinite resources are not treated as valid geometry.
Body measurement never modifies live animation, collision separation or attacks.

All eight profiles are baked, including future summons/poses. They build
concurrently. Raw rasterization is shared per (cell size, ground/levitation)
tile using the largest group border; water/slope policies require separate
ground and levitation rasters. A bounded 32-tile domain store limits memory.
Each profile copies raw spans, applies its original ledge/height filters and
builds its own compact field, radius erosion, clearance contours and detail.
All radii are exact cell multiples; erosion never rounds a radius above the
measured half-width. Contour simplification error is one cell instead of
zero; region adjacency and area/pose boundaries still constrain contours.
Detail height samples are RETAINED: Detour projection/step/restore use them,
including BASE over terrain. Removing them would flatten interior hills and
change floor selection. The detail grid is 1.2 m (previously 0.6 m), with
sample height error at most 0.1 m (previously 0.05 m). Walkable contours,
clearance masks and polygon adjacency are unchanged by detail settings.
All profiles use 0.1 m cells, including giant (previously 0.2 m).
Detour tiles enter each mesh in fixed z/x order. Disk copies omit runtime
links (zeroed link storage, firstLink=DT_NULL_LINK), which addTile reconstructs
from the retained polygon neighbours and tile portals. Each tile array is
then byte-shuffled before lossless LZMS compression (see below).
There is no per-map size cap. Campaign target is 300 MiB, strict cap 400 MiB.

Game (x,y,z) maps to Recast (x,z,-y). Units are metres. Ground is the actual
MPR triangles with sector offsets/water. BASE/IsBridge parts use effective FIG
triangles, BON/constitution, orientation and origin; CROWN/EMPTY keep their
semantics. Other solid BVs become prisms, never walkable tops. Climb is 0.35 m;
ground slope 40 degrees, supported levitation slope 60 degrees. Levitation is
not free 3D flight. Four clearance permissions share each profile mesh.
The decoder is the embedded owned renderer's production decoder. Finite BV
endpoints are normalized on each local axis before transforming all eight
corners. CFigure::RecalcBoundingBoxWithRotation (Figure.cpp) likewise computes
Min/Max over all eight endpoints; the old aiMap object path also enumerates
both endpoints rather than requiring their order. Negative constitution is
not a corrupt volume. NaN/infinite geometry still reports its source.

## Container 3

Little endian fields, no raw host pointers/structs except pinned Detour tile
payloads: NAV664 magic (u32 0x3656414e, 0x00003436), container=3, algorithm=9,
library=0x010600, endian=0x12345678, polyrefBytes=8; four length-prefixed UTF-8
strings: fingerprint, MPR basename, MOB basename, profile ID. String lengths
are u32 byte counts without NUL. Then six f32 profile fields (radius, height,
slope, climb, cellSize, cellHeight), u32 tileCells; three f32 origin values,
two f32 tile dimensions, u32 maxTiles/maxPolys; u32 sectionCount=3.
Each section is u32 type, u32 payloadBytes, payload:

- 1: u32 codec=5 (Windows Compression API LZMS, buffer mode, no RAW),
  u32 expandedBytes, u32 compressedBytes, compressed buffer. The expanded
  buffer is u32 tileCount followed by (u32 tileBytes, filtered Detour tile).
  Expansion is bounded to 512 MiB and must have exactly the declared length.
- 2: u32 domainCount=1, u32 domainId=1 (imported terrain/BASE).
- 3: u32 layerCount=0 (reserved until NAV05).

Filtered tiles keep the 4-aligned dtMeshHeader unchanged. Following arrays,
in Detour order, transpose bytes within each fixed-size record array:
vertices (12 bytes), dtPoly (32), dtLink (16), dtPolyDetail (12), detail
vertices (12), detail triangles (4), dtBVNode (16). For N records of width W,
stored[lane*N+i] = native[i*W+lane]. Counts come from the unchanged header;
each section remains 4-aligned. No off-mesh connections are present. After
LZMS decompression, the reader bounds the header/counts/array lengths, reverses
the shuffle and performs the existing complete tile checks before addTile.
This preserves every geometry float bit, index, material and clearance flag;
only runtime links are regenerated by Detour, as for an ordinary new tile.

Compression is lossless and uses the OS cabinet.dll, linked via cabinet.lib;
no dependency DLL is added to the package. API contract:
[Compress](https://learn.microsoft.com/en-us/windows/win32/api/compressapi/nf-compressapi-compress),
[Decompress](https://learn.microsoft.com/en-us/windows/win32/api/compressapi/nf-compressapi-decompress).
The production reader validates
versions, SHA, profile, exact lengths, counts, indices and tile ABI before
addTile. Limits: 4096 tiles, 32768 polygons/tile, 16 MiB/tile, 512 MiB/file.
Zero walkable tiles is valid. Stale data is rejected, never regenerated.

## Runtime / remaining packages

Prepare reads every fixed profile before publishing NewZone/staged load and
resolves restored unit addresses. Query entry points still route exclusively
through NavigationWorld. Preview, exact linear corridor, constrained Detour
step, bounded source ingress, pose masks and SaveSchema 4 remain as in r8.
Old x64 saves are rejected without migration; transient polygon refs are not
serialized. Partial paths and bounded-search exhaustion are failures.
Moving units wait rather than push. Full crowd/queue/terrain costs and robust
independent floors remain later NAV packages.

Runtime object changes retain the published baked snapshot until NAV05.
No runtime builder or old graph is used. Actual add/remove/update contributions
are journalled with before/after solid/triangle counts; empty-to-empty events
do not affect generation or the snapshot. Dynamic tile updates remain NAV05. Exact campaign gameplay and the zone1
passages (1.3 m opening/ramp, 1.5 m courtyards, 0.8 m door, 6 m giant gates)
require owner acceptance. samvlrmp01 measured width remains 1.30 m.

## Detail height sampling and release validation (r13)

The tile border was already max(domain radius / cell size) + 3 cells:
13 cells for ground and 8 for levitation. Zero-radius profiles have the same
border as their domain. No border, erosion or passage contour is changed.
After polygon construction only, maxEdgeError supplied to the detail builder
is max(compact-field width, height), bounding the search by the WHOLE local
height patch. Recast v1.6.0 uses ceil(maxEdgeError) as getHeight's search
radius. A contour sample can fall on an unset cell; the previous one-cell
search could return RC_UNSET_HEIGHT, later converted into a 3273 m vertex.
Even two cells were insufficient on zone19/tiny and zone8/levitate_tiny.
Every clamped sample and every filled height-patch cell is within this bound
because a polygon's height patch is no larger than the compact field.
getHeight still stops at the FIRST filled ring of the SAME height patch/layer;
ordinary samples do not scan the whole field. No polygon, erosion, radius or
passage contour is changed, and resulting heights are never clamped.

Before writing any profile of a map, the baker checks every polygon and
stored detail vertex against that tile's vertical bounds, allowing 2 cell
heights (0.1 m, including the library's +cellHeight detail offset). Nonfinite
or out-of-bounds heights cause MAP_ERROR code=4; reason includes MPR, MOB,
profile, tile x/y/layer, vertex kind/index, height, bounds and tolerance.
PROFILE includes height_outliers=0 only after this check succeeds for the map.
This is release-data validation, not an alternate reader or test executable.
Container 3, compression, filenames, command line and exit codes are unchanged;
algorithm 8 fingerprints require re-baking all profiles.
## Immutable source fingerprint and runtime changes (r14)

Algorithm 9 fingerprints only original file inputs. At Scene entry the shared
owned MOB reader enumerates the original model references without creating
objects or executing scripts/RNG. Packed MOD/BON, or LNK plus its selected
per-node FIG/BON, are fingerprinted in stable key order. Missing optional
resources are explicit empty entries. This dependency set never comes from
objects restored from a save or spawned/removed by gameplay. The derived live
geometry no longer enters the file key. The normal and staged-save loaders
use the same source catalog; the baker uses that same Scene path.

Prepare validates and loads all eight profiles once per zone. Party deployment,
repeat Prepare and queries retain them and their generation after a live
object change. The frozen ingress-support geometry also remains unchanged
within that loaded zone. The live contribution inventory is still compared
and journalled for NAV05; it cannot clear meshes or change the source key.
A zero contribution before AND after a change is ignored. Actual stale source
files still fail at zone/save entry. Container 3, CLI and SaveSchema 4 remain;
algorithm 8 files require a complete re-bake with this executable.

query_failed adds profile, clearance height, max_length and route_length to
the existing bounded journal event. A short straight-line displacement can
still exceed max_length when its traversable corridor detours around a wall.
No path-length/radius/clearance policies or map data are changed in r14.
