# Ataman 12: neighboring static scenes (2026-09-17)

Task EI-NEIGHBOR-012, branch `codex/neighbor-scenes-v12`, based on
`1f10c4b9f14fd54c295ed10e464e0dc6ada0f5a4`. No game resources modified.

The old renderer loaded only MPR synchronously, applied background tint and
disabled terrain/object shadows. No neighboring MOB geometry was loaded.
The screenshot's black gap is not diagnosed as a lighting defect.

Implementation: two private CPU workers parse/validate immutable MPR and MOB
snapshots. Generation checks discard results after map changes. Ordinary B001
world-object visuals preserve position, complexion, quaternion, parts, textures
and shadow flag. Units and gameplay-only nodes are excluded. Terrain projection
and world translation use the neighboring landscape. Neighbor objects stay out
of editable MOB lists, selection, document serialization and minimap capture.
Terrain shadow UVs subtract each landscape's world origin; active terrain and
water restore their own shadow state. Visible objects join the existing GPU
queue; nearby off-screen shadow casters use its shadow-only collection.

GPU resources still belong to the GUI render context. Object upload is bounded
to 16 objects / approximately 4 ms per callback (one asset may exceed that
budget). Sector construction and texture assembly still occur on that context.
This is not independent GPU-context rendering or a measured FPS improvement.
Neighbor point lights, gameplay, particles and separate local weather are not
loaded; neighboring scenes share the active scene's sun/time settings.

## Focused evidence

- `build-tests-12-neighbor/results.txt`: 5 PASS, 0 FAIL, 0 SKIP.
  Synthetic visual preservation/exclusion and malformed/dimension mismatch
  rejection; real read-only zone1/zone9/zone4: 1527/1396/1339 static objects.
  MPR bytes unchanged. No live map edits or copies.
- `build-tests-10-widgets/shader12.txt`: 3 PASS, 0 FAIL, 0 SKIP.
  Actual land vertex/fragment shaders compile/link in hidden Windows OpenGL
  context; world-origin uniform exists. This does not prove rendered appearance.
- `build-neighbors12.log`: release build PASS in fresh version-12 shadow.

## Remaining acceptance

Open the original zone9 view with neighbors enabled. Check buildings, trees,
rocks and lighting while changing time of day. Move a neighbor and verify
objects/shadows move together; switch maps rapidly and toggle neighbors to check
stale results and cleanup. Verify static-object visibility and shared sculpt
projection. Compare frame/load times before making performance claims.
Actual asynchronous scene rendering/cancellation has not been exercised by the
focused document/shader tests. Parent task remains open pending these checks
and evaluation of the requested separate GPU work.
