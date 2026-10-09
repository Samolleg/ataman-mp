# Modern terrain editor MVP — current contract at Ataman 128

Synchronized 2026-10-10, task EI-PAINT-DOCS-SYNC-20261010. This document describes
the current implementation; original acceptance/reference notes are retained
below as history. Source anchors and verification limits are in
[the synchronization report](validation/ATAMAN_PAINT_DOCS_SYNC_20261010.md).

Terrain authoring separates semantic vertex material identities from physical
atlas slots. A brush card is a material identity; transition-support components
remain available for atlas review without becoming independent paint tools.

## Painting modes and stroke contract

- **Modern (Z)** reuses exact authored junctions, including stock-library
  artwork, and generates missing exact blends from uniform material sources.
  With automatic transitions enabled (default), release resolves the accumulated
  stroke. With that option disabled, missing blends can remain pending until
  **Resolve Missing Blends**; publication refuses unresolved changed tuples.
- **Classic / Classic Auto (X)** uses existing exact atlas tiles and never
  expands the atlas. **Safe** is enabled by default and leaves unsupported
  boundaries unchanged. With Safe disabled (the less restrictive behavior often
  called Smart), unresolved boundaries may use a local uniform hard edge.
  Smart is not a separate enum or a separate current UI mode.
- **Manual (C)** writes a selected physical atlas slot and rotation directly.

Ground and Overlay are selectable layers within the painting workflow.
Underwater materials appear in the Ground palette. A family may allow Ground,
Overlay, or both; tile corners must share a compatible layer.

During a semantic drag the editor accumulates touched vertices and displays a
temporary unblended fill. Release resolves the accumulated footprint. One drag
is one Undo transaction. The semantic field and final physical references must
remain coherent; failures are reported rather than silently changing a requested
Modern material identity.

## Exact generation and packing

The resolver tries the current authored catalog and stock authored library
before generation. Generation requires uniform material sources, selects a
blend style/topology and deterministic variation, and records provenance.
Reviewed ownership masks are preferred. The implementation also contains
explicit canonical mathematical mask paths and a corner-bilinear partition for
zero-weight gaps in multi-family composition. The old blanket statement
“No procedural fallback” is therefore obsolete; a missing source or unusable
mask still produces a diagnostic.

Packing preserves the exact orientation of retained authored artwork.
**R151's removal of rotated-equivalent authored-slot merging must not be
confused with generated-signature canonicalization:** current generation still
canonicalizes signatures across quarter turns. Identical authored images can
merge only with agreeing semantic corners, runtime terrain class and route
owner; animation phases are kept distinct. Generated cache identity includes
pixels, terrain class, signature and route owner. Rotated artwork is not made
identical merely because its semantic corners are rotation-equivalent.

Runtime MP terrain classes (0..15) remain separate from editor material IDs.
Generated classes use corner coverage; ties require the preferred/painted
material's runtime class. Unknown classes are refused. Dense packing keeps live
cells, pinned references from the other layer and live animation blocks, remaps
physical references, and produces 64-slot MMP pages. DXT1 page encoding preserves
binary alpha; it does not authorize flattening graded alpha.

The editor finalizer accepts up to **16,384 slots / 256 pages**, the packed
format ceiling. It warns above **4,096 slots** instead of prohibiting export.
The corrected retained JMV runtime campaign passed 1,024, 2,048 and 4,096 slots;
8,192 failed during load and the ordered corrected campaign stopped before
16,384. These are historical workload-specific results, not a guarantee for
every map or a new runtime check of Ataman 128. See
[expanded runtime evidence](validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md).
The old 1,024-slot finalization ceiling is obsolete. A legacy 1,024 validation
constant/comment remains in the header, but the editor passes the packed limit.

## Saving, publication and semantic persistence

Ground + Overlay can be published together in one session. Sparse inactive
Overlay regions are not a requirement for separate sessions: publication uses
a recoverable Ground snapshot and pins/remaps the live opposite-layer tiles.
Incomplete required Ground decoding or unresolved changed tuples still refuse
publication.

**Save MPR** may update the loaded MPR in place; **Save MPR As** chooses another
destination. Publication does not require a renamed MPR or a fresh output
directory. New artwork/working-extension references or an explicit identity
publication request route saving through atlas publication. A semantic field
alone is not new artwork: ordinary saves, including height-only changes, use
the MPR transaction without republishing the shared RES.

Since Ataman 52 the configured **HDPack_Terrain.res** is the publication target
for terrain pages/profile data. The MPR and RES candidates are staged/reopened,
existing resources are preserved, replacements are guarded/backed up, and
publication failure attempts guarded rollback. Reusing a resource identity can
affect other maps using it; the UI asks about replacement. Publication is not
unconditional isolation from a shared archive.

Since Ataman 122 semantic vertex state is embedded in the MPR member
**ei_mapper_terrain_semantics.json**. It stores Ground/Overlay fields and binds
them to namespace, terrain-tile state, atlas pixels and profile. Loading prefers
the MPR member and falls back to legacy RES semantics when it is absent.
Height-only edits do not invalidate the tile binding. Portable profile copies
and publication review metadata are separate editor data.

The explicit modern-draft action remains available for editable working atlas
data; it is distinct from the ordinary save route and from runtime acceptance.

## Historical MVP acceptance workload

The headless matrix covers every ordered pair of four independent uniform
materials with compact-square, horizontal, vertical, and both diagonal paint
shapes, plus three- and four-family junctions. Its oracle compares every saved
cell's exact four semantic corners with the authoritative intended vertex
field, checks shared-edge continuity and mandatory touched vertices, repeats
finalization for stable hashes, and loads every generated MPR again. Dedicated
runtime-class regressions cover majority resolution, painted-material ties,
rotation independence, typed pixel deduplication, unknown-class refusal, and
working-preview propagation. This historical workload description is not a
fresh test result for version 128. Current terrain edits mark the AI graph preview stale; regeneration requires
an explicit request. A loaded authored graph remains a user-selected override.

The distributable visual workload is a real-JMV Zone2 stripe collection. It
stamps every reviewed uniform family, forces missing multi-family junctions,
writes the exact generated MPR/RES pair, and emits a native-resolution contact
sheet plus per-tile stock provenance. Synthetic unit fixtures remain structural
tests only and are not presented as human visual evidence.

## Historical reference evidence (not revalidated in this synchronization)

Read-only static inspection of the installed `EvilIslandsMapEditor.exe` shows a
similar high-level separation among a material field/catalog, tile recipes,
composition, and atlas baking. Its installed HD tileset descriptions also
confirm layered base/mask recipes and explicit rotations. Those observations
support the architectural split only; no private or third-party implementation
is copied, and the installed editor is not presented as an Endurance identity
source.
