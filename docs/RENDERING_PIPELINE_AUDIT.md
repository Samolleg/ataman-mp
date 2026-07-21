# Rendering pipeline audit and performance contract

## Outcome

Endurance currently renders scene geometry with OpenGL on the GPU, but prepares
and submits that geometry through an immediate, object-by-object CPU path. The
main performance risk in a large scene is therefore not the polygon count of an
Evil Islands asset. It is the number of objects and parts visited, draw calls,
texture and buffer state changes, and duplicated GPU resources.

The audit increment added rolling CPU/GPU counters and removed two provably
redundant costs: sequential FIG triangles no longer allocate and bind identity
index buffers, and adjacent parts of an object no longer rebind the same
texture. The visibility increment now removes the fixed 15 ms scheduler cap,
culls terrain sectors, and submits MOB objects through a deterministic 32-unit
spatial grid. The queue increment now shares equivalent immutable FIG VBOs,
classifies decoded texture alpha, and submits sorted opaque, cutout, transparent,
and highlighted render items. The first modern path instances matching static
and hierarchy-animated opaque/cutout parts; morph and ordered transparency stay
on the individual correctness path. This remains an intermediate renderer
stage, not a claim that the final performance target has been reached.

## Current frame

| Stage | Current implementation | Existing optimization | Remaining cost |
| --- | --- | --- | --- |
| Scheduling | A precise request loop supports an immediate 0-1000 FPS cap | Normal mode uses the persisted VSync choice; `--render-benchmark` forces VSync off and no cap | Integer-millisecond Qt timers make non-divisor caps approximate |
| Scene traversal | Each MOB has a deterministic 32-unit spatial grid | Visible cells and object bounds are frustum tested; selected objects stay conservative | Dense views still submit thousands of visible parts |
| Terrain | Land and water sectors are frustum rejected | Terrain is already split into 32-unit sectors | Visible sectors still issue one draw each |
| Mesh storage | Immutable FIG geometry shares profile/resource-scoped VBOs | Morph-deformed parts detach to private updatable buffers; desktop OpenGL 3.3+ caches VAO layouts per shared mesh, shader, and pass | CPU vertex arrays remain per part and thousands of unique meshes still limit instancing |
| Object submission | Visible parts produce explicit render items | Matching immutable opaque/cutout parts instance on desktop OpenGL 3.3+; genuine transparency sorts back-to-front | Unique meshes and ordered transparency still produce individual draws |
| Rasterization | OpenGL shaders, VBOs, textures, depth test | Verified opaque work disables blend/alpha test; cutouts retain alpha test | Back-face culling remains disabled and conservative materials retain legacy blending |
| Selection/helpers | Separate selection and logic-helper passes | Picking reuses spatial visibility; selected/moving nodes remain conservative | Enabled logic mode intentionally retains full helper traversal |
| Resources | Figure, texture, archive-index, and miss caches exist | Unit visual preload avoids many first-view loads | CPU decode and GUI-thread GL upload are not yet bounded asynchronous phases |

## Instrumentation

Enable **Settings > Render > Log render performance counters**. Every two
seconds the log reports:

- observed FPS;
- average, 95th-percentile, and maximum CPU frame submission time;
- asynchronous GPU elapsed time when timer queries are supported;
- viewport, camera pose, profile/map identity, and source-content hash;
- visited, culled, submitted, spatial-cell, candidate, and overflow counts;
- logic traversal calls;
- visible/culled/tested terrain sectors;
- draw calls, triangles, texture binds, queue/instance/shared-mesh counters,
  and separate queue collection, sorting, and OpenGL draw-phase milliseconds.

`--render-benchmark` enables counters automatically, requests VSync off before
the first OpenGL widget is created, and writes `logs/render-benchmark.json`.
That JSON is a reproducible benchmark definition: profile, map, combined input
hash, viewport, camera position/pivot/rotation, render toggles, timings, and
submission averages. It embeds no game resources.

GPU timing is polled without blocking the render thread. Unsupported timer
queries produce a warning and leave the CPU counters usable. Measurements must
be taken on the same map, camera, window resolution, profile, and render options
before and after an optimization.

## Acceptance contract

The high-end target is at least 200 FPS after warm-up at 3840 x 2160 on the
Ryzen 7 9800X3D + RTX 5080 control machine in the densest useful `gz18h` view,
with unit animation enabled. CPU and GPU averages must be at most 5 ms and
95th percentiles at most 6 ms. This is measured in uncapped benchmark mode;
normal editing is display-synchronized (currently 143 Hz).

The low-spec target is at least 60 FPS at 1920 x 1080 on a named modern
low-spec machine. It cannot be claimed until such a device is tested.

Acceptance runs cover the largest known scenes with a static camera and while
moving the camera, with units animated and with representative editor helpers.
Cold map/resource load is recorded separately from steady-state rendering.
Both CPU submission and GPU timing must be reported: FPS alone cannot identify
which side is limiting the frame.

## Optimization sequence

1. **Visibility (implemented; partially runtime validated).** Use continuous
   VSync/benchmark scheduling, reproducible counters, terrain-sector culling,
   the MOB spatial grid, conservative selection handling, and helper gating.
2. **Build explicit render queues and shared meshes (implemented; runtime validation pending).** Separate opaque, cutout, transparent,
   helper, and selection work. Sort opaque/cutout draws by pipeline, material,
   and shared mesh; sort genuine transparency back-to-front. Cache one GPU
   mesh/VAO for equivalent immutable FIG geometry and stop enabling blending
   for verified opaque work.
3. **Batch and instance static objects (initial modern path implemented).** Issue instanced draws for matching
   mesh/material entries inside visible spatial cells. Keep a non-instanced
   compatibility path and a separate dynamic queue for animated units.
4. **Reduce remaining submission overhead.** Cache immutable vertex input
   state, combine compatible geometry where instancing cannot reduce unique
   draws, and submit visible terrain sectors through a multi-draw path.
5. **Bound resource work.** Perform archive lookup and CPU decode off the GUI
   thread; upload to OpenGL in bounded batches during map load. Retain explicit
   placeholders and diagnostics for failures.

Each phase must retain selection-pass equivalence, diagnostics, profile
isolation, and visual regression cases before the next phase begins.

## Visibility-build runtime validation

Use the packaged launchers for normal VSync operation and the uncapped
benchmark launcher. Do not modify any map or resource merely to run a test.

1. Open JMV `gz18h` at 3840 x 2160, enable unit animation and performance
   counters, disable logic/help overlays, and move to the established densest
   useful camera. Let resources warm up, then hold the camera stationary for at
   least ten seconds and orbit/move for at least ten seconds.
2. In normal mode confirm the log says `presentation:display-vsync`, reports
   `swap-interval-request:1`, and frame pacing follows the display. In benchmark
   mode confirm `presentation:uncapped-benchmark`, `swap-interval-request:0`,
   and `logs/render-benchmark.json` updates with the correct resolution,
   camera, profile/map, content hash, and toggles.
3. Compare land and water at the same fixed camera with frustum culling on and
   off. Visible output must match; tested/visible/culled sector counts must
   change coherently.
4. Select objects near every screen edge, rectangle-select a mixed group, then
   move, rotate, and scale them. Exercise copy/paste, delete, hide/unhide,
   undo/redo, body-part/texture edits, and MOB switching. A selected or actively
   moved object must never disappear because the grid has stale bounds.
5. Toggle logic helpers and logic edit mode. Ordinary rendering must report
   zero helper submissions while helpers are off; enabled helpers and logic
   picking must remain complete.
6. Check Base and JMV static objects, humanoids, animated monsters, morph wings,
   equipment, layered atlases, transparent objects, water, and highlighted
   selection. Close normally after several map switches and confirm no crash.

Only after these cases pass should the queue/shared-mesh stage begin.

The 2026-07-21 VSync capture ended cleanly and verified spatial rejection and
helper gating in `zone18` and `zone3obr`. It was captured at 2430 x 1960 with
VSync, so it is behavior evidence rather than the required uncapped 4K result.
Dense `zone3obr` still produced about 8,900 draws and 2,842 texture binds at
roughly 250 ms CPU submission; this is the direct baseline for queue validation.

## Queue-build runtime validation

Repeat the visibility cases with this build, then inspect the queue counters at
the same fixed cameras. Opaque/cutout/transparent counts must be non-zero where
the scene contains those materials; shared-mesh and GPU-memory estimates must
stabilize after warm-up. Texture binds should fall substantially while draw
count remains comparable until instancing is added. Specifically verify water,
foliage/cutouts, translucent effects, highlighted selections, humanoid layered
atlases, morph wings, rectangle picking, and several map switches. Attach both
`workflow.log` and `render-performance.log`; use the benchmark launcher for the
separate uncapped 4K measurement.

The 2026-07-21 pacing/instancing capture proves the remaining dense-view limit
is CPU submission, not the FPS cap. At 2430 x 1960 with VSync off, the heaviest
`zone18` view averaged about 25 FPS, 39 ms CPU submission, 7.7 ms GPU time,
4,082 draws, and 3,935 batches. The comparable `zone3obr` camp view averaged
about 34 FPS and 29 ms CPU submission. The following state-reduction increment
removes per-object uniforms that were still executed while merely collecting
queues, avoids per-batch matrix-vector allocations, and records collect/sort/
draw phase timings for the next fixed-camera comparison.

The subsequent submission capture localizes that cost. A stationary heavy
`zone18` view at 2430 x 1960 averaged about 34.5 FPS and 28.1 ms CPU submission,
while queue collection plus sorting consumed only about 1.6 ms and the queue
draw phase consumed about 23.3 ms. It submitted 3,984 draws in 3,839 batches;
GPU average time was 6-7 ms. The workflow contained no crash/fatal record and
reported desktop OpenGL 4.6 with instancing enabled. This evidence selects
vertex-input and uniform submission as the next measured target. The following
increment caches shader locations and separate ordinary, instanced, selection,
and instanced-selection VAO layouts per immutable mesh. The legacy/ANGLE path
continues to configure vertex attributes explicitly.

## Lighting, shadows, sky, fog, and particles

These features must consume the optimized render queues rather than introduce
another unconditional full-scene traversal.

- Reverse engineer Base/JMV environment, light, particle, and runtime sun data
  before assigning semantics. Conflicting or absent data remains diagnostic.
- Implement skybox, fog, ambient/skylight, directional sun, and the time-of-day
  slider as an environment state shared by all relevant passes.
- Cache static shadow casters. Rebuild directional shadow data only when the
  sun/time, map geometry, or a caster changes. Range/frustum-cull spot lights
  and honor the object's `is shadow` value.
- Pool particle emitters and render particles as instanced camera-facing quads
  with fixed simulation steps, distance culling, and bounded counts.
- Provide explicit quality controls: shadows Off/Low/High, particles
  Off/Reduced/Full, and an unshadowed lighting fallback. Disabling an effect
  must avoid its simulation, traversal, allocation, and draw costs—not merely
  make its output invisible.

The renderer must never silently lower correctness settings to disguise a
missed frame target. Compatibility choices are user-visible and diagnostic.
