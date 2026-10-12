# Cross-map recovery: diagnosis and sequential integration handoff

Date: 2026-10-12. Inspection base: main `8f82ce3d341dbfe755ef77d7de34429a6a5a2102`, VERSION152; installed151. Shared files already have unrelated work in progress. Ownership for this task: `source/app/neighbor_performance_check.cpp` and this report only. No executable build, UI/OpenGL run, installation, live data writes or commit performed here. Private engine skills are unavailable; findings use ordinary project code and the authorized PNG/data locations only.

**Status: visual cross-map transition OPEN.** VERSION151's equality assertions establish boundary agreement, not the appearance of a mixed transition. The new harness has been statically reviewed, not compiled or executed. Its structural PASS must not be reported as visual acceptance.

## What the old PNGs actually show

Both report PNGs were opened visually. They are 1024x512, representing two 128x128 atlas tiles enlarged 4x. The after image has a straight apparent join at display X512 and repeated narrow strips at its external edges. Before/after differ at 518688/524288 pixels (exact ARGB comparison). They are not byte/pixel-equivalent; visual similarity is not a valid change metric.

The old `seam-before`/`seam-after` exporter always draws active `a` left and neighbor `b` right. With zone4 active, `a` is zone4 cell X0 and `b` is zone6 cell X127. Actual world placement is zone6 origin (704,64), right edge X960, then zone4 origin (960,190), left edge X960. Thus the comparison correctly tests `a.pixel(0,row)` against `b.pixel(last,row)`, but the PNG joins **a's opposite edge** to **b's opposite edge**. The two edges the test compared are at the exterior of the PNG. The apparent centre line is not the real world seam. With zone6 active the ordering is different, another reason to fix the exporter explicitly by world coordinates.

The abandoned repair capture is closer to correct order for zone4 active, but hardcodes tile size32, draws native images without sizing/rotation into 32-pixel steps, assumes the neighbor is left, and silently ignores failures. Native tiles128 overlap one another and clip at the 128x96 canvas. It cannot provide reliable visual evidence.

On the existing after PNG, native columns0/1,0/2,0/3,0/4 match in all128 native rows; native columns255/254,255/253,255/252 also match in all128 rows. These comparisons step across 4x blocks, not within one enlarged pixel. By contrast native columns127/126 match14/128,127/125 match0/128,128/129 match5/128 and128/130 match0/128. Therefore the external smearing is present in the atlas raster, not introduced by nearest-neighbor enlargement. OpenGL sampling is not implicated by this raster evidence.

## First incorrect decisions in the shared pipeline

1. `view_world_terrain.cpp::paintWorldTerrainAt`: stage both material fields, plan common boundary vertices, import common uniform/stock sources, then call `CTerrainMixedEdge::resolve`. This builds B2 physical mixed tiles at world-cell coordinates. A later loop calls `shared.worldBoundaryTile(...)` and `importWorldBoundaryTile(...)` for those same seam cells, replacing the entire B2 tile. See current calls near1499.
2. `resolveWorldMaterialBlends` similarly calls mixed resolve, then always runs the canonical seam replacement for Land and Overlay, near1054. The last replacement is what gets saved, not the earlier B2 output.
3. `terrain_virtual_atlas.cpp::worldBoundaryTile`, near2882: starts from `reviewGeneratedBlend(signature,0)` cached only by signature. It has no input for the already-resolved body image. Consequently it necessarily discards selected B2 ownership/body/variation. This is a proven control-flow consequence, not proof that every resulting canonical transition is visually bad.
4. The same function defines `band=min(12,size/4)`, `guard=min(4,band)`. For each pixel in the band it constructs `world` exclusively from the side's two edge segments and the along-edge parameter `t`. There is no normal-distance term. `sourcePixel(a,world)` and `sourcePixel(b,world)` therefore do not vary across the band. For `distance<guard`, fade=1, and the first fade endpoint at distance4 is also1. Several full columns/rows are overwritten with the same samples: a proven data-level cause of the external stripes seen above.
5. A semantically uniform seam tile can have identical opposite-border pixels and still provide no mixed transition at all. Even correct shared corner IDs plus a single matching texel column cannot establish the shape/width of the visible transition or continuity into the next interior tile. DXT1 border equality only extends that narrow property through save/reopen.

## Proposed shared correction; ownership required

Do not patch shared files concurrently with their current owner. Request sequential ownership of `source/documents/terrain_virtual_atlas.{h,cpp}` and the two call sites in `source/render/view_world_terrain.cpp`, or ask the shared owner to implement:

- Extend `worldBoundaryTile` with an optional resolved physical body image. For Land with modernMixedEdge enabled, pass the active image of `before` after `CTerrainMixedEdge::resolve`, transformed by its packed rotation into the API's unrotated orientation. Start the output from this body; only alter the seam anchor band. Keep canonical generation as the fallback for the non-mixed path/Overlay. Validate dimensions rather than silently resizing. Include the body identity in any final-output cache; current signature-only canonical cache can remain solely a fallback-generation cache.
- Compute the source sample position in both surface axes. For grid `(u,v)=(x/(size-1),y/(size-1))`, map `(2u,2v)` into the cell's 3x3 SEC vertices; interpolate the same two triangles per quarter-cell as the terrain mesh. Along the actual border this must reproduce the existing common-world edge point; inward pixels must have their own world positions. Use those positions for periodic source sampling, not the projected border point. Do not repair/flatten geometry or height discrepancies.
- Keep equal world samples on the outer ring, but remove the fully repeated four-texel guard. Fade the constraint toward the preserved B2 body. A new feather profile requires visual review of the full context; choosing a smaller band alone does not fix the missing normal coordinate.
- Check joins to the interior neighbor cells as well as the cross-map border. Preserve per-map namespaces, SEC classes, Overlay presence, slot protections and batch Undo/Redo/save behavior.

The API extension and world-coordinate correction are proposed patches. Their appearance and regression remain untested. No threshold of image similarity has been chosen to manufacture a PASS.

## Harness changes in the assigned file

The unfinished QPainter include/error detail are retained; broken repair exporter is replaced. `inspectSeam` exports a6x7 tile neighbourhood in world order: three zone6 columns then three zone4 columns, aligned Y using the63-cell origin difference. It derives native tile dimensions, applies packed UV turns following `tile.cpp::getNewIndex`, and mirrors atlas Y into terrain grid Y. Grid +Y runs down the output, explicitly recorded in its receipt. This is an atlas surface raster, not an OpenGL viewport or a full terrain deformation projection.

Exports are native PNG, nearest-neighbor4x PNG and TXT for `stroke-before`, `stroke-after`, `stroke-undo`, `stroke-redo`, `stroke-reopened`; repair exports before/after/undo/redo/reopened when those states exist. Reopened images decode the published MMP from the copied RES, rather than reusing the working atlas. Export/read/decode/signature failures cause FAIL. The fixture explicitly requires zone4/zone6 in either active order.

Each receipt records world rectangle, map/cell, packed slot/rotation, four semantic corners, mixed seam cell counts for each side and exact edge mismatch count. The stroke now requires non-uniform semantic seam cells on **both** maps after painting, Redo and disk reopen. This rejects the vacuous case of two uniform fields with matching border pixels. Repair normally only reports mixed counts: an originally uniform fixture is not arbitrarily required to become mixed. `EI_WORLD_SEAM_MIXED_WITNESS=1` additionally requires both mixed sides after repair/Redo/reopen when run on the prepared mixed stroke fixture. Neither assertion claims the pixels show a visually acceptable blend. The existing complete state hashes cover transaction restoration; the wider images supply the missing visual context.

## Reproducible private fixture and sequential command

The source paths below were verified to exist. Copy only authorized zone4/zone6 MPR/MOB and the terrain RES; no live mutation. Fresh fixture avoids inheriting earlier stroke/repair changes from `build-tests-151-crossmap-real/world-brush-seam`. Run this only after the lead integrates the harness/shared changes and builds one executable sequentially. Use normal Windows QPA; do not run simultaneously with another executable/UI/OpenGL build or test.

```powershell
$repo = 'C:\GOG Games\maped\ei-mapper-ataman'
$sourceMod = 'C:\GOG Games\Дежаваю\PZ\MODS\Dejavu'
$fixture = Join-Path $repo 'build-tests-crossmap-recovery-20261012'
if (Test-Path -LiteralPath $fixture) { throw 'Use a new fixture path; do not overwrite prior evidence.' }
New-Item -ItemType Directory -Path (Join-Path $fixture 'Maps'),(Join-Path $fixture 'res') | Out-Null
foreach ($file in 'zone4.mpr','zone6.mpr','zone4.mob','zone6.mob') {
    Copy-Item -LiteralPath (Join-Path $sourceMod "Maps\$file") -Destination (Join-Path $fixture 'Maps')
}
Copy-Item -LiteralPath (Join-Path $sourceMod 'Res\HDPack_Terrain.res') -Destination (Join-Path $fixture 'res')
Get-ChildItem -LiteralPath (Join-Path $fixture 'Maps'),(Join-Path $fixture 'res') -File |
    Get-FileHash -Algorithm SHA256 | Export-Csv (Join-Path $fixture 'input-sha256.csv') -NoTypeInformation
# Point this to the lead's single freshly built candidate, with matching Qt DLLs.
$exe = Join-Path $repo 'build-staging-crossmap-recovery\EIMapperEndurance.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Set $exe to the integrated candidate.' }
$env:EI_NEIGHBOR_PERF_ROOT = $fixture
$env:EI_NEIGHBOR_PERF_RES = Join-Path $fixture 'res\HDPack_Terrain.res'
$env:EI_NEIGHBOR_PERF_ZONE = 'zone4'
$env:EI_WORLD_BRUSH_NEIGHBOR = 'zone6'
$env:EI_WORLD_BRUSH_CHECK = '1'
$env:EI_WORLD_MIXED_EDGE_BRUSH_CHECK = '1'
$env:EI_WORLD_SEAM_PAIR_CHECK = '1'
$env:EI_WORLD_SAVE_WORLD_TERRAIN_CHECK = '1'
$env:EI_WORLD_REPAIR_SEAM_CAPTURE_DIR = Join-Path $fixture 'stroke-captures'
foreach ($flag in 'EI_WORLD_MATERIAL_CHECK','EI_WORLD_LAYOUT_MATERIAL_CHECK','EI_WORLD_SEAM_MIXED_WITNESS') {
    Remove-Item -LiteralPath "Env:$flag" -ErrorAction SilentlyContinue
}
& $exe --scenario-layer-check > (Join-Path $fixture 'stroke.stdout.log') 2> (Join-Path $fixture 'stroke.stderr.log')
if ($LASTEXITCODE -ne 0) { throw 'Stroke failed; inspect evidence before proceeding.' }
$env:EI_WORLD_MATERIAL_CHECK = '1'
$env:EI_WORLD_SEAM_MIXED_WITNESS = '1'
$env:EI_WORLD_REPAIR_SEAM_CAPTURE_DIR = Join-Path $fixture 'repair-captures'
& $exe --scenario-layer-check > (Join-Path $fixture 'repair.stdout.log') 2> (Join-Path $fixture 'repair.stderr.log')
if ($LASTEXITCODE -ne 0) { throw 'Repair failed; do not mark complete.' }
```

Use a fresh PowerShell session for these flags; close it afterwards. Record candidate full source SHA, EXE SHA256, input/final hashes, exit codes and elapsed times. The command is prepared, **not executed** here. MOB and originals must remain byte-identical. Repair is a second process on the saved mixed fixture, establishing fresh-load behavior; its before snapshot is intentionally the previous stroke result. A separate fresh fixture with zone6 active exercises the reverse ordering; do not reuse a modified fixture to claim the same baseline.

## Exact snapshot/acceptance requirements

The planned stroke centre is world(960,206), radius1, Land, Modern mixed-edge enabled, family absent from the original5x5 seam vertex footprint. The exported rectangle is world X[954,966), Y[200,214), with seam at the middle between columns3/4 (zero-based2/3). Receipt reports the chosen family. No SEC-deformed known exclusion occurs at this centre; do not use excluded rows to justify a local transition failure.

Review native and4x images at the same coordinates for before, after, Undo, Redo and saved reopen; retain the adjoining interior tiles. Both banks must show the intended newly painted material with a transition to retained material, and the transition must continue across the actual centre seam. A whole tile replacement or an exact border match is insufficient. Confirm the body is not replaced with repeated strips, and that joins to the interior cells are also natural. If a mixed signature is present but pixels remain straight/unmixed, report structural PASS/visual FAIL with the exact cell and slot. If the fixture does not expose the claimed defect, record that limitation and select a documented material/coordinate; do not relax the assertion or declare visual PASS.

An additional hidden viewport capture after integration must use the same world region/camera, show both selected maps, and capture after frame completion without reinitializing shaders. Record viewport rectangle, world bounds, active map, enabled neighbors, camera pose, terrain texture resolution, layers and source hashes. Compare atlas raster against the viewport: strips present in both implicate data; strips appearing only in viewport implicate UV/padding/filter/capture. The harness raster alone cannot validate SEC deformation, lighting or the viewport capture path. User/game acceptance remains separate.

Static verification performed: current diff readback and `git diff --check` (no whitespace errors); actual PNG dimensions, changed-pixel count and native-column stripe checks; both PNGs visually inspected; source control flow traced. Full compile and integration execution remain for the lead's sequential slot. VERSION/ROADMAP/shared files were not changed by this task.

Handoff delivery: the first attempt was rejected by automatic approval review for missing explicit cross-chat messaging authorization. After the owner explicitly authorized delivery, the findings, report path, sequential scenario and shared-ownership request were successfully sent to source chat `01a12f3b-2308-72b2-b775-2d03122c9422`. Shared ownership has been requested, not yet granted. Concurrent main advanced to `2bbe19ca5eaa5340136e4be99ab10a73dc2c8424` by final readback of the diagnosis; this task did not commit. Reconcile this harness with the accepted main before the sequential build.

## Sequential shared implementation after ownership transfer

The paragraph above records the initial delivery state. The lead subsequently confirmed the three shared files were clean, notified their previous owner, and explicitly transferred sequential ownership of `source/documents/terrain_virtual_atlas.h`, `source/documents/terrain_virtual_atlas.cpp`, the two seam call sites in `source/render/view_world_terrain.cpp`, and `tests/terrain_virtual_atlas_tests.cpp`. The transfer authorized only this CPU unit-target in a separate build directory; full executable/UI/OpenGL remain with the first executor. No git add/commit, VERSION/ROADMAP or installation changes were made here. Concurrent main at final implementation readback: `5b5bee56cb2df8c654f9a5dca2a886c97df39eee`; the implementation is still uncommitted work for integration.

Implemented diff against accepted main: atlas CPP56 added/18 removed, header6/1, two call sites21/2, CPU tests197/0. The prior harness and this report remain this task's files. HD-owned files were not edited.

- `worldBoundaryTile` accepts the resolved physical body and its packed rotation. It mirrors atlas Y, applies the renderer's UV index transform with an exact quarter-turn, and returns artwork normalized to rotation0. Only the seam band is composed; the body, grain and B2 variant outside it remain pixel-exact. Both production call sites pass the just-resolved B2 image/rotation and import the result with rotation0 and the original reviewed runtime class/signature.
- Source sample XY uses the pixel's own2D surface point. Each tile is four SEC quarter-quads split into actual render triangles `(00,10,11)` and `(00,11,01)`, as in `tile.cpp`/`sector.cpp` indices. Triangle weights transport signed254-scale SEC offsets. Height is not flattened or changed: material sampling uses world XY; non-flat height fixtures and differing neighbor heights remain byte-identical. This is not a bilinear quad approximation.
- Only distance0 has full edge constraint. Inward weight is `smoothUnit(1-distance/band)`, with no repeated guard plateau, and each inward sample has its own world point. Shared endpoints still set the along-edge material ownership. Opposite interior-facing edges and all pixels outside selected collars remain pixel-exact; transverse joins through another collar still require actual map inspection.
- Resolved-body output is never cached by signature. The cache stores only canonical fallback bodies, keyed by native size, source pixel hashes, semantic signature, material styles, mask content/labels, opacity and seed. Geometry-dependent anchoring is calculated per call. Body dimension mismatch, missing source, invalid origin/cell/side mask or rotation are rejected without silent resize.
- Important caller distinction: B2 resolve regenerates non-uniform cells, but deliberately retains matching uniform artwork. Only non-uniform mixed-edge Land cells pass a body. Uniform/Overlay/non-mixed cells retain deterministic canonical fallback, preventing repeated repair from feathering its own earlier uniform result. The compositor is a one-pass constraint over a fresh resolved body, not an idempotent transformation of arbitrary already-constrained input. Production repeat repair/save-reopen remains a required integration check.

### CPU regression evidence

Build directory: `build-tests-crossmap-recovery-cpu`. Toolchain: existing Qt5.12.12 MinGW73 and matching GCC7.3 from the verified Endurance tooling path, used read-only; no toolchain installation/modification. Only `tests/terrain_virtual_atlas_tests.pro` was built, with `mingw32-make -j2`. Sandbox launch failures (PowerShell redirection adapter, then system DLL relocation) were not PASS; the authorized CPU build/run succeeded with approved host execution and hidden processes.

1. **Before implementation:** `worldBoundarySamplesSurfaceTriangles` failed on the old source at the assertion that moving the interior SEC vertex must change an inward sample. Exit1,2 lifecycle PASS/1 FAIL/0 SKIP,11ms. Log: `build-tests-crossmap-recovery-cpu/before.stdout.log`. This test was compiled and run before changing the shared algorithm.
2. **After implementation:** the whole existing CPU unit-target passed174/0/0 in5330ms, exit0. Log: `build-tests-crossmap-recovery-cpu/after.stdout.log`. This includes existing material ownership, artwork variants, persistence and atlas tests; no other suite or executable was run.
3. **Final additions:** after including mask labels in fallback cache identity and adding the normalized packed import assertion, only the affected four test functions were rerun:26/0/0 in640ms, exit0. Log: `build-tests-crossmap-recovery-cpu/focused-final.stdout.log`. Final unit EXE SHA256 `C42CF79E52AEB3BAEA7B81B69B9185A32A5DF8E357C6FA910E5821D81DC2435F`.

Focused functions: `worldBoundarySamplesSurfaceTriangles` checks both triangle formulas against independent explicit coordinate/color oracles, changed inward samples and unchanged border/SEC bytes; `worldBoundaryPreservesResolvedBody` tests a real polished B2 bake, four packed rotations across each cardinal side and the four-side mask (20 rows), exact body/interior edges, body cache independence and physical import/signature/runtime class at rotation0; `worldBoundaryJoinsDeformedMaps` checks every texel on east/west and south/north joins with shared signed SEC offsets and non-flat/differing heights; `worldBoundaryCacheAndDimensions` checks same-signature/different-source fallback cache separation and rejected dimension/rotation/missing-material inputs. `git diff --check` passed. Compile warnings shown in logs are pre-existing misleading-indentation warnings outside the changed algorithm.

### Next sequential integration gate

**Visual status remains OPEN.** The lead must reconcile the uncommitted diff with current main, integrate/version/commit it with the rest of the assigned result, and let the first executor perform the single full application build. Run the private zone4/zone6 command above against that candidate, inspect all coordinate-matched native/4x images after stroke/Undo/Redo/reopen and repair, and check production repeat repair idempotence and unchanged SEC/runtime classes. The two CView call sites have been reviewed statically, not compiled in this CPU-only target.

An additional material-rendering boundary matters: `CTile::generateDrawVertexData` samples inside the atlas tile with a `4/512` page UV inset (a native tile fraction1/16). The raw texture's outer texel ring is therefore not identical to the live rendered edge. The wider atlas raster and CPU outer-ring equality do not validate that inset, bilinear/mip filtering, deformed mesh presentation or shader capture. Record the viewport comparison required above; if the real centre seam still breaks, trace the actual inset UV sample and its adjacent pixel footprint before altering the collar/renderer. Do not close the issue by increasing a matching-pixel metric or by presenting the new CPU PASS as visual acceptance. Shared ownership was used only for this implementation; no additional renderer ownership was assumed.

The completed shared diff, exact CPU results, current main, report path and the required sequential integration/viewport gate were successfully delivered to the lead in source chat `01a12f3b-2308-72b2-b775-2d03122c9422` under the owner's explicit messaging authorization. No cross-chat delivery remains blocked.
