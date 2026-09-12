# EI-WORLD-SCULPT-001: baseline and geometry decision

Status: owner approved the geometry policy; implementation and focused tests are
recorded in [r1 validation](../validation/WORLD_TERRAIN_SCULPT_R1.md).
Baseline: `ffdc28ea9f749896ddffdd583e2a6a5e44d5201a`, fetched origin/main,
branch `contrib/Samolleg/world-terrain-sculpt`, initially clean.

## Original acceptance reproduction, recorded before implementation

Use synthetic 1x1-sector MPRs A and B, 33x33 vertices, zero XY offsets,
same maxZ, flat equal initial heights. Place A at (0,0), B at (32,0).
Open A with B visible. Enable editing B (this control is absent at baseline).
Raise at world (32,16), radius 3, positive strength, release once.
Expected: A's east and B's west vertices change to the same final heights;
both interiors within the radius change, and a single undo/redo restores both.
Repeat Lower, Flatten and Smooth; Smooth samples an immutable combined input
and produces the same result with document enumeration reversed. With B
protected, no B bytes/state change. Switching active maps must preserve edits.
Saving must review both paths, validate and reopen both outputs, retain backups,
and report/recover injected failures instead of silently saving half a seam.

Baseline result is established by source inspection, not an interactive GUI run:
there is no opt-in neighboring-document editing operation to execute. The full
synthetic integration test still needs implementation/execution after the decision.
Do not report this scenario as a passing automated or manual end-to-end test.

## Pipeline and first blocking ownership state

- `source/render/view.cpp:loadNeighborLandscapes`: validates a local temporary
  CMprDocument, then stores only placement and a read-only CLandscape. The
  temporary document goes out of scope. There is no retained neighbor edit model.
- `setNeighborMapContext` and `clearNeighborLandscapes` discard loaded neighbors;
  an editable multi-map session must survive these presentation changes.
- `sculptTerrainAt` uses local position.x/y and exclusively m_terrainEdit.
- `source/documents/terrain_edit_model.cpp:makeSmooth` clips its 3x3 samples to
  the active model's bounds. It cannot read the other side of an MPR boundary.
- `finishSculptStroke` pushes one CTerrainHeightPreviewCommand tied to CView's
  active terrain model, not a stable document cohort.
- Landscape unload resets m_terrainEdit and m_mprDocument. Existing background
  lifetime is unsuitable for dirty neighboring documents and cross-map undo.
- `saveLandscapeDocument` and `buildValidatedLandscapeCandidate` operate on the
  current MPR/model. Existing per-file safety is not a multi-file recovery protocol.

## Read-only installed-layout evidence

Ran `scripts/audit_world_sculpt_geometry.ps1` with explicit local layout and maps
paths. No extraction, copying, mutation, private source inspection or GUI launch.
The script reports each input MPR hash and pair geometry; paths are supplied at
run time and are intentionally not embedded in the script/report.
Layout SHA256: `5027B3597CCB4C0E682B5D60F10995F30EBFD308B5E7652A9FA88F9C599389E5`.
Audited saved jamais-vu/gipat/gz1g component (eight MPRs).

Vertex interpretation follows public project code: sector stride 32, 33 vertices,
eight bytes per vertex, signed byte XY offsets divided by 254, unsigned 16-bit
height multiplied by that MPR's maxZ/65535 (`source/render/sector.cpp`).
Matching uses exact decimal XY including offsets, not nearest-vertex guessing.
Height mismatch reporting uses 1e-6 world units solely as a diagnostic threshold;
this is not an approved authoring/persistence tolerance.

| Pair | Exact boundary XY matches | Unequal heights | Maximum world Z difference |
| --- | ---: | ---: | ---: |
| zone1 / zone2 | 161 | 3 | 0.5420083607 |
| zone1 / zone3obr | 0 | n/a | n/a |
| zone3obr / zone6 | 321 | 2 | 0.1170742047 |
| zone3obr / zone7 | 353 | 20 | 0.2346057306 |
| zone6 / zone7 | 161 | 0 | 0 |
| zone8 / zone9 | 353 | 0 | 0 |

zone1 origin (0,0), dimensions 352x288; zone2 origin (192,288), 160x192.
zone3obr origin (352,63.5): its nominal edge touches zone1, but vertex XY does
not coincide. zone6/zone9 nominal rectangles overlap by 0.25 in X over 125.75
in Y; there are zero exact boundary XY matches. This rectangle diagnostic is
not a triangle-intersection or full surface-overlap test.

maxZ differs between maps (e.g. zone1 21.363665, zone2 46.130543). Equal raw
height deltas are not equal world displacements. Exact common arbitrary world
heights are generally not representable in two independently scaled uint16
grids; persistence policy must account for this rather than promise exact
multi-file seam equality after naive quantization.

## Decision history before implementation

The request explicitly required asking when installed geometry has gaps,
overlaps, misaligned grids or initially unequal heights. Implementation paused
at this gate until the owner explicitly confirmed the policy below.

Approved: independently sculpt misaligned grids/overlaps; average initially
unequal coincident vertices inside the stroke; accept format-bounded saved-height
rounding. Do not move maps or stitch outside the stroke. Normalizing maxZ across
maps would alter heights outside the brush and is not performed.

Next: finish the PR/tester handoff and native interactive acceptance described
in the validation report. The full approved scope remains the parent task.

## Handoff uncertainty audit

No remaining owner-policy ambiguity. Only the located saved component was audited,
not every possible layout/profile. Core and focused widget evidence now exist;
native viewport acceptance remains open. The full UI check reproduced 0xC0000005.
Named EI skills were absent from the supplied catalog and local skill-file search;
no private-source access was needed. No guessed geometry association was chosen.
