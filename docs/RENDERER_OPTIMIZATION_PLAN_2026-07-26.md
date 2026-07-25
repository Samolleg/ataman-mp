# Renderer optimization plan: measured 2026-07-26

## Result and feasibility

The current merged renderer does not meet the 200+ FPS 4K target. In the
water-heavy `zone18` full-environment case with unit animation enabled, the
largest viewport available to the validation desktop was 2418 by 1875 rather
than 3840 by 2160. A warmed stationary sample measured 18.8 FPS, 52.43 ms
average CPU frame time (67.27 ms p95), and 25.24 ms average GPU time (42.72 ms
p95). The scene submitted about 7,152 draws, 5,350 batches, 5.216 million
triangles, and 395 texture binds per frame.

This is not a 4K acceptance result. It is sufficient to reject incremental
micro-optimization as a route to 200 FPS: the target requires both CPU and GPU
averages at or below 5 ms, while a true 4K workload will increase raster cost.
The target remains technically possible on the named Ryzen 7 9800X3D and RTX
5080 only through architectural submission, visibility, animation, and shadow
changes. The stock engine is not an optimization ceiling.

The capture used desktop OpenGL 4.6, VSync off, instancing and VAOs enabled,
full environment and shadows enabled, logic/help overlays disabled, frustum
culling enabled, and the `zone18`/`gz18h` content fingerprint
`05338e0b34e4b68777de03773cdb50da2c085b3ac1eb67c5b3132ccee58cbcb9`.
The JSON/log artifacts accompanying the test package retain the complete
fingerprint and camera/options record.

## Ranked work

| Rank | Proposed work | Evidence and expected gain | Implementation risk | Visual/correctness risk | Dependencies |
| ---: | --- | --- | --- | --- | --- |
| 1 | GPU-driven visibility and submission: persistent draw records, indirect/multi-draw submission, and compatible geometry/material consolidation | Queue draw/submit is about 23.35 ms; 7,152 draws and 5,350 batches dominate despite only 395 texture binds. A several-fold CPU reduction is required. | High | Medium-high: ordering, transparency, selection IDs, and compatibility fallbacks must remain exact. | Stable render-item keys; desktop capability path; ANGLE/legacy individual-draw fallback; selection parity tests. |
| 2 | Persistent scene/animation preparation with dirty-state updates, visibility-first animation evaluation, and bounded animation LOD/cache | Queue collection is about 22.60 ms; the capture reports roughly 2,700 animation components and 98 evaluations per frame. Avoid reconstructing unchanged records and transforms. | High | High for animated pose, equipment, morph, and selected/offscreen objects. | Explicit dirty generations; deterministic pose cache; selection and editor overrides. |
| 3 | Split static/dynamic shadows, cache static casters, throttle/fence dynamic rebuilds, and add shadow-frustum/LOD filtering | Roughly 12,956 candidates and 1,604 submitted shadow items cost about 5.78 ms CPU each frame, in addition to their GPU raster cost. | Medium-high | High: missing or stale shadows are conspicuous. | Stable caster hashes; light/camera invalidation; conservative selected/moving-object handling. |
| 4 | Reduce GPU raster and overdraw: validate back-face culling by asset family, tighten shadow/alpha cutout paths, then consider depth prepass or hierarchical occlusion | GPU average is about 25.24 ms below 4K while processing 5.216 million triangles; back-face culling remains globally disabled. A 5 ms 4K target needs a major raster reduction. | Medium-high | High for mirrored/winding-inconsistent FIGs, foliage, translucent effects, and two-sided materials. | Winding/material classification report; per-family fallback; fixed-camera image comparisons. |
| 5 | Expand instancing and mesh sharing across more immutable animated/static combinations | About 7,987 instances are present but only about 1,075 instanced draws; 6,880 shared meshes still result in thousands of batches. | Medium | Medium for complexion, equipment, morph, highlight, and transparent ordering. | Work from ranks 1-2; exact material/pose keys. |
| 6 | Terrain and overlay multi-draw after scene submission is fixed | Terrain contributes only about 358 draws in this capture. Combining visible sector draws is useful but cannot close the current gap alone. | Low-medium | Low-medium: picking and edit overlays must match sector ownership. | Desktop multi-draw path; ANGLE fallback; sector visibility list. |
| 7 | Texture/sampler state cleanup and small uniform-call reductions | Only about 395 texture binds were measured. This is useful hygiene after larger work, not a leading bottleneck. | Low | Low | Shader-location/state caches already present. |

## Delivery sequence and gates

1. Freeze fixed-camera desktop and ANGLE baselines at an actual 3840 by 2160
   viewport on the target machine. Record average/p95 CPU and GPU time plus
   collect, sort, draw, shadow, draw-call, batch, and triangle counters.
2. Implement rank 1 behind a desktop capability gate. Require pixel-equivalent
   opaque/cutout output, transparent ordering checks, and selection/editing
   equivalence before making it default. Keep the existing ANGLE path.
3. Implement rank 2 with explicit dirty generations. Validate animated units,
   equipment, morphs, map switching, hidden objects, and first-frame wake-up.
4. Implement rank 3 with conservative invalidation. Compare static and moving
   camera/light/unit cases before accepting cached shadows.
5. Use GPU captures at true 4K to choose among the rank-4 raster changes rather
   than enabling culling or a depth prepass globally.
6. Re-profile after every rank. Stop optimizing a stage once its average and
   p95 contribution no longer threatens the 5/6 ms frame contracts.

The 200 FPS target should not be claimed until a true 4K uncapped run has both
CPU and GPU averages no greater than 5 ms and p95 values no greater than 6 ms,
with full environment, unit animation, shadows, selection, landscape editing,
and compatibility validation complete.
