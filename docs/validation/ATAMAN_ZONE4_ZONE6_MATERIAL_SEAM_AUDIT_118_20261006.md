# Ataman118: why zone4/zone6 material repair leaves a visible seam

Task: `EI-WORLD-MATERIAL-SEAM-AUDIT-118-20261006`.
Date: 2026-10-06. Branch: `main`.
Inspected source: `2dbf45229deab12e211973b507509675e04a47d9`, VERSION118.
Related: `EI-NEIGHBOR-TILE-SNAP-118`, `EI-WORLD-MATERIAL-SEAMS`,
`EI-UNIFIED-TERRAIN`. Their visual and save/reopen acceptance remains open.

The owner reports that "Исправить переходы…" leaves neighboring textures cut
at the zone6/zone4 border. The screenshot shows zone4 active in the Dejavu
profile. This task inspects ordinary mapper code, logs and installed data;
it does not implement a repair or modify maps, resources or the installation.

## Confirmed cause of the repair no-op

The neighboring map is included. The workflow log contains the exact command
at 11:47:23–11:47:25 on 2026-10-06, action6, zone4:

```text
Terrain resolve audit begin action:6 trigger:button
Restored exact shared-corner terrain semantics ... zone6_fe645a375f96 ...
World material repair maps:0 cells:0 boundaryVertices:0 conflicts:101
Terrain resolve audit end action:6 ... outcome:world-completed
```

Evidence: permanent installation `logs/workflow.log`, lines8494–8498;
`diagnostics/terrain-resolve/zone4_fc5a0ba98c52-resolve-sessions.json`, action6.
The101 conflicts are per-map boundary-vertex observations across the whole
enabled cohort and both layers, not101 distinct zone4/zone6 tiles.

The button calls `CView::resolveMissingTerrainBlends` in
`source/render/view.cpp`. With the world brush enabled it enters
`CView::resolveWorldMaterialBlends` in `source/render/view_world_terrain.cpp`.
Active and selected adjacent maps are staged together; a committed change
would preview every changed map and refresh neighboring terrain.

The first decisive refusal is in `planWorldMaterialRepair`,
`source/documents/world_material_boundary.h`, near line99:
boundary evidence must contain exactly one positive material. Pure physical
tiles take precedence; otherwise the resolver collects shared endpoint
materials. If the evidence is empty or has multiple materials, the planner
records a conflict and retains the original field. It does not create a new
joint transition band to resolve this choice. Thus a completed operation can
leave every tile unchanged, as action6 did.

The physical resolver subsequently preserves tiles whose four material
corners already match their own authoritative field:
`CTerrainEditModel::missingBlendRepairSignature`,
`source/documents/terrain_edit_model.cpp`. It does not compare pixel masks
along paired tile edges. Matching categorical corners are consequently not
a guarantee of continuous texture detail on a seam between two atlases.
This existing limitation is also documented in
`ATAMAN_WORLD_MATERIAL_SEAMS_68.md`, "Acceptance boundaries".

## Read-only verification of this pair

An owned probe, `build-audit-118-material-seam-20261006/read_pair.py`, reads
the installed layout, both MPR indexes, bound semantic fields and profiles
from `HDPack_Terrain.res`. It validates resource/member lengths, MPR SHA256,
profile SHA256, atlas identity, semantic dimensions and decompressed field
hashes. Files are never copied for editing or written. Aggregates are in
the same owned directory, `pair-observations.json`.

The probe explicitly selects profile `deja-vu`. The JSON also contains
`jamais-vu`; its older fractional coordinates are not used for this case.

| Map | Origin | Extent in world units | Land tile dimensions |
| --- | --- | --- | --- |
| zone4 | (960,190) | 512×320 | 256×160 |
| zone6 | (704,64) | 256×384 | 128×192 |

The shared nominal edge is X960, Y190..448:129 two-unit segments,
130 endpoint vertices. Relative Y offset126 is63 complete tiles. There is
no nominal origin/grid-phase mismatch on this pair in the saved layout.

Results for **saved land**, not unsaved editor state:

- 10 of130 paired semantic endpoint vertices have different materials.
- 20 of129 paired physical tile-edge signatures differ, after decoding
 packed rotation and the profile-to-grid reflection with the same
 convention as `STerrainAtlasProfile::usableAnalysis`.
- For example, world endpoint(960,242): zone4 is `Grass / Common`, zone6 is
 `Grass / Dark`. Endpoint(960,246): `Grass / Irregular` versus
 `Grass / Sparse light`. Physical mismatches are present on both incident
 tile segments; they are not just a different slot number in two atlases.
- No incident pair in this saved sample consists of two different completely
 uniform tiles. The actual pair must not be described as a uniform-versus-
 uniform example; its conflicts also involve mixed transitions.
- 127 of130 paired actual geometry vertices coincide. zone6 boundary
 vertices(128,180),(128,181),(128,182) have signed Y offset−63, giving
 an along-edge displacement−63/254≈−0.248 world units relative to zone4.
 This is local MPR deformation, not layout-origin snapping. All three
 retain X960; it is not evidence of a gap normal to the map border.

Installed source hashes, checked equal before/after the probe:

| Source | SHA256 |
| --- | --- |
| world-map-layouts.json | E243FE7CFC2A0F7A8ACD25141706A3F1073C30013CEEC896E6C1B21A0E0B1DC9 |
| zone4.mpr | 0A3030A0C4EB571A6BB2D9B1FDEDB67D1E35E6AAC834F49C3E733673C4B7A2AE |
| zone6.mpr | 33D3092AB2F817092EB2F63DB0A44225860D4427D338B1ED00167D0F232EBD8A |

The resource archive's full before/after hash and all field/atlas hashes
are retained in the owned aggregate JSON. Its fingerprint is not inferred
from the older log byte size; the owner has continued updating resources.

The user's later log reads both atlas sets completely: zone4 13/13 parts,
zone6 16/16, each from `HDPack_Terrain.res` (11:49:56–11:49:58).
Older "deferred binding unavailable" warnings refer to other atlas
fingerprints. No missing current zone4/zone6 page is demonstrated here.

## Existing behavior check and related accounting defect

Ran the two existing focused component cases
`materialRepairUsesPhysicalUniformEvidence` and
`materialRepairUsesDeformedTripleGeometry` from the retained
`build-tests-115-terrain-mob-height/world_terrain_sculpt_tests` executable:
4 QtTest passes including setup/cleanup,0 failures,0 skips. Log:
`build-audit-118-material-seam-20261006/existing-policy.txt`.
This is an existing-binary component check. The planner/header and these
test cases predate115 and remain unchanged in118. It confirms the
conservative conflict policy, not a new repair, full118 build, UI acceptance
or screenshot pixel continuity. No interactive mapper was launched.

Separate confirmed defect: the generic audit does not receive world
transaction deltas and only counts
`terrain.virtual_map_ambiguous_skipped`, not
`terrain.world.material_conflict`. Action5 on zone6 reports actual world
changes maps3/cells12/vertices22 in `workflow.log`, but the generic journal
still says changedCells0/skippedCells0. Therefore generic audit zeros are
not reliable world-change counts. Action6 is a genuine no-op because the
world-specific summary itself says0; its conclusion does not rely only on
the defective generic counter.

## Required next implementation and completion criteria

Repairing the reported symptom requires an explicit joint material-seam
operation. Simply refreshing the renderer or snapping origins again cannot
remove the demonstrated material disagreements.

1. Plan both incident border rows as a single world-space boundary, with
   an explicit, reviewable policy for different materials. Preserve the
   interior of each map. Do not silently choose whichever map is active.
2. Prepare transitions in both working atlases together. Shared masks and
   sampling along the border must agree; comparing only corner IDs is not
   enough. Check actual deformed edge segments and report unsupported
   geometry instead of assuming every vertex is nominally on the grid.
3. Preview and commit both maps as one reversible world transaction, retain
   runtime tile types, and fail atomically if a required transition cannot
   be represented. The existing save/rollback contract still applies.
4. Report actual changed/skipped counts and conflicts per map/pair/layer,
   including world edits in the generic resolve audit.
5. Verify this saved zone4/zone6 case, mixed and uniform material pairs,
   compatible/rejected neighbors, reverse active-map choice, T-junctions,
   pixel continuity, Undo/Redo, save/reopen and eventual owner visual
   acceptance. Aggregate checks alone cannot close the screenshot defect.

Status: diagnosis and implementation requirements recorded. No repair,
version increase, release build, distribution or installation performed.
The material seam and parent manual acceptance remain open. Missing optional
private-engine/modding skills do not affect this ordinary public mapper-code
inspection; no private engine sources were read.
