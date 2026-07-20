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
spatial grid. This remains the first stage of the renderer replacement, not a
claim that the final performance target has been reached.

## Current frame

| Stage | Current implementation | Existing optimization | Remaining cost |
| --- | --- | --- | --- |
| Scheduling | A precise zero-interval request loop renders continuously | Normal mode requests display VSync; `--render-benchmark` requests swap interval 0 | Driver presentation behavior must be confirmed in runtime logs |
| Scene traversal | Each MOB has a deterministic 32-unit spatial grid | Visible cells and object bounds are frustum tested; selected objects stay conservative | Render-item queues and batching are not implemented yet |
| Terrain | Land and water sectors are frustum rejected | Terrain is already split into 32-unit sectors | Visible sectors still issue one draw each |
| Mesh storage | Each object owns copied `CPart` vertex buffers | Parsed figures and textures are cached | Identical static instances do not share immutable GPU meshes |
| Object submission | Each visible part sets attributes and draws | Repeated textures within one object are now coalesced | No VAO reuse, render queue, material sorting, batching, or instancing |
| Rasterization | OpenGL shaders, VBOs, textures, depth test | GPU executes vertex/fragment work | Blending and alpha testing are globally enabled; back-face culling is disabled |
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
- draw calls, triangles, texture binds, and reserved queue/instance/shared-mesh
  counters used by later stages.

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

1. **Visibility (implemented; runtime validation pending).** Use continuous
   VSync/benchmark scheduling, reproducible counters, terrain-sector culling,
   the MOB spatial grid, conservative selection handling, and helper gating.
2. **Build explicit render queues and shared meshes.** Separate opaque, cutout, transparent,
   helper, and selection work. Sort opaque/cutout draws by pipeline, material,
   and shared mesh; sort genuine transparency back-to-front. Cache one GPU
   mesh/VAO for equivalent immutable FIG geometry and stop enabling blending
   for verified opaque work.
3. **Batch and instance static objects.** Issue instanced draws for matching
   mesh/material entries inside visible spatial cells. Keep a non-instanced
   compatibility path and a separate dynamic queue for animated units.
4. **Bound resource work.** Perform archive lookup and CPU decode off the GUI
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
