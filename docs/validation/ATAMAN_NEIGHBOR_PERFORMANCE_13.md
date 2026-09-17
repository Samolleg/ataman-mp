# Ataman 13 — neighboring static scene performance

2026-09-17, EI-PERF-013, branch `codex/neighbor-performance-v13`.
Base: `47303bbc43f59cbd3fbe99c67a72ba497239a7e6` (delivered version 12).
Owner requested proposed points 2, 3, 4 and 5 together. No live game writes,
main integration, remote operations or interactive editor launch.

## Implementation

- 32-unit XY groups have conservative bounds encompassing entire objects,
  including large objects crossing cell boundaries and negative coordinates.
  Reject a group only outside the color frustum AND neighboring shadow reach.
- Cache world matrices, transformed part bounds, mesh/material lookup and draw
  commands. Stationary frames only select candidates and update camera distances.
  New uploads append records. Origin changes rebuild them; terrain reprojection
  explicitly invalidates records. Generation replacement destroys old caches.
- Worker-local archive indexes, figure parser, constitution interpolation,
  triangle expansion, texture decoding and material classification. Prepared
  values contain no GL handles. Geometry/texture variants are shared within a
  worker result. Existing compressed DXT mip chains remain compressed on desktop
  upload; the worker also prepares the GLES image. GL upload retains the bounded
  main-context callback. Worker pool is joined before shutdown changes CPart's
  shared GPU cleanup policy.
- Vertex clustering preserves normal/UV discontinuities and extreme positions;
  it leaves small meshes and negligible reductions unchanged. Distant meshes
  use shared GPU buffers. Enter at distance >140 and projected radius <40px;
  leave at <=110 or >=56px. Color objects below 0.5px radius are omitted.
  Neighbor shadow reach is min(user shadow distance,128). Offscreen casters
  within that reach remain eligible. Active-map quality remains unchanged.
- Specialized humanoid/inventory appearance paths keep their previous loader
  and full geometry because they also compose appearance/UV data. Ordinary
  scenery assets are prepared entirely on CPU workers before GPU upload.
  Terrain-sector assembly/atlas upload still belongs to the render context.
  No separate GPU context or FPS improvement is claimed.

## Evidence

- `build-tests-13-performance/results.txt`: 6 PASS, 0 FAIL. Conservative group
  boundaries, negative cells, shadow reach, LOD hysteresis and source-preserving
  mesh simplification. Synthetic 10,000 objects: 192 candidates in 169 groups
  for the specified viewport/shadow setup; tessellated plane 8192 -> 1152
  triangles with surface area and winding preserved. These are algorithm
  cases, not an estimate of real FPS. Initial plane test caught extreme-edge
  shrinkage; preserving extreme representatives fixed it.
- `build-tests-13-queue/results.txt`: 14 PASS, 0 FAIL.
- Hidden integration through `--scenario-layer-check` with opt-in
  `EI_NEIGHBOR_PERF_ROOT`, `EI_NEIGHBOR_PERF_RES`, `EI_NEIGHBOR_PERF_ZONE`.
  Its settings are in scratch, source maps are read-only and verified by hashes.
  Tests worker execution without GL context, GPU upload, real full-detail
  geometry against 24 legacy visual variants, stationary cache reuse, origin
  and height invalidation, retained shadow bounds, stale-result rejection,
  current async generation completion and hidden rendering.
  Logs: `build-release-13-performance/zone*-perf.stdout.txt` and JSON exit codes.

| Map | Prepared objects / textures | Full triangles | Distant triangles |
| --- | ---: | ---: | ---: |
| zone9 | 1396 / 1396 | 726740 | 697817 |
| zone1 | 1527 / 1527 | 950267 | 900551 |
| zone4 | 1339 / 1339 | 1117847 | 1052312 |

Counts sum all object instances, not just visible geometry or unique GPU meshes.
The real-map geometry reduction is about 4–6%; it is not a claimed FPS gain.
Initial zone1 comparison counted stst8's empty helper as a rendered part;
the corrected oracle applies the same nonempty-part rule as the legacy queue.
An initial screenshot harness used renderPixmap twice, reinitialized shaders,
produced a black image and crashed on teardown. The harness now uses one
WA_DontShowOnScreen backing surface and reads its framebuffer, with clean exit.
Hidden captures prove scene submission, not full user-window visual acceptance.

## Remaining acceptance

Open the user's original view. Pan/orbit and approach/distance neighbors;
check near geometry, distant transitions and reduced shadow reach. Move a
neighbor and sculpt/undo its terrain to verify object placement. Switch maps
and profiles during loading. Compare steady-frame and load-time measurements
with version 12 under identical camera, resolution, light and quality settings.
The parent scene/visual acceptance task remains open pending that comparison.
