# Rendering pipeline audit and 60 FPS contract

## Outcome

Endurance currently renders scene geometry with OpenGL on the GPU, but prepares
and submits that geometry through an immediate, object-by-object CPU path. The
main performance risk in a large scene is therefore not the polygon count of an
Evil Islands asset. It is the number of objects and parts visited, draw calls,
texture and buffer state changes, and duplicated GPU resources.

The first audit increment adds opt-in rolling CPU/GPU counters and removes two
provably redundant costs: sequential FIG triangles no longer allocate and bind
identity index buffers, and adjacent parts of an object no longer rebind the
same texture. This is measurement and low-risk cleanup, not a claim that the
60 FPS target has already been reached.

## Current frame

| Stage | Current implementation | Existing optimization | Remaining cost |
| --- | --- | --- | --- |
| Scheduling | A viewport timer requests a frame every 15 ms | Unit poses have distance and visibility tiers | The viewport redraws even when a static scene has not changed |
| Scene traversal | Every MOB node is visited on the CPU | Object bounding spheres can be frustum culled | No spatial index; off-screen objects still incur traversal and bounds work |
| Terrain | Every loaded sector is submitted | Terrain is split into sectors | No sector-frustum rejection and one draw per land/water sector |
| Mesh storage | Each object owns copied `CPart` vertex buffers | Parsed figures and textures are cached | Identical static instances do not share immutable GPU meshes |
| Object submission | Each visible part sets attributes and draws | Repeated textures within one object are now coalesced | No VAO reuse, render queue, material sorting, batching, or instancing |
| Rasterization | OpenGL shaders, VBOs, textures, depth test | GPU executes vertex/fragment work | Blending and alpha testing are globally enabled; back-face culling is disabled |
| Selection/helpers | Separate selection and logic-helper passes | Object geometry is culled in normal visual passes | Logic helpers are traversed independently and state changes are immediate |
| Resources | Figure, texture, archive-index, and miss caches exist | Unit visual preload avoids many first-view loads | CPU decode and GUI-thread GL upload are not yet bounded asynchronous phases |

## Instrumentation

Enable **Settings > Render > Log render performance counters**. Every two
seconds the log reports:

- observed FPS;
- average and maximum CPU frame submission time;
- asynchronous GPU elapsed time when timer queries are supported;
- visited, culled, and submitted nodes;
- logic traversal calls;
- draw calls, terrain draws, triangles, and texture binds.

GPU timing is polled without blocking the render thread. Unsupported timer
queries produce a warning and leave the CPU counters usable. Measurements must
be taken on the same map, camera, window resolution, profile, and render options
before and after an optimization.

## Acceptance contract

The target is at least 60 FPS after warm-up on a modern supported machine in
every shipped Base and JMV scene. A frame has 16.67 ms at 60 Hz; implementation
work should target at most 12 ms typical frame time and at most 16.67 ms at the
95th percentile so editor UI and driver variance have headroom.

Acceptance runs cover the largest known scenes with a static camera and while
moving the camera, with units animated and with representative editor helpers.
Cold map/resource load is recorded separately from steady-state rendering.
Both CPU submission and GPU timing must be reported: FPS alone cannot identify
which side is limiting the frame.

## Optimization sequence

1. **Measure and remove redundant work.** Keep the counters, remove identity
   index buffers, coalesce safe texture/state changes, and add terrain-sector
   visibility counts. Establish reproducible heavy-map baselines.
2. **Share immutable meshes.** Cache one GPU mesh/VAO for equivalent FIG
   geometry and let object instances carry only transforms, appearance choices,
   and animation pose data. Do not duplicate VBOs per static instance.
3. **Index the scene spatially.** Build a map-owned grid or BVH so the renderer
   visits visible cells instead of every MOB node. Cull terrain sectors using
   the same camera frustum.
4. **Build explicit render queues.** Separate opaque, cutout, transparent,
   helper, and selection work. Sort opaque/cutout draws by pipeline, material,
   and shared mesh; sort genuine transparency back-to-front. Stop enabling
   blending for opaque work.
5. **Batch and instance static objects.** Issue instanced draws for matching
   mesh/material entries inside visible spatial cells. Keep a non-instanced
   compatibility path and a separate dynamic queue for animated units.
6. **Bound resource work.** Perform archive lookup and CPU decode off the GUI
   thread; upload to OpenGL in bounded batches during map load. Retain explicit
   placeholders and diagnostics for failures.
7. **Redraw on demand where possible.** Static, inactive views need not render
   continuously. Animated or interactive views continue at the target cadence.

Each phase must retain selection-pass equivalence, diagnostics, profile
isolation, and visual regression cases before the next phase begins.

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
