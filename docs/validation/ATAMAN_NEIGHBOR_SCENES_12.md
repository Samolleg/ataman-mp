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

## Build and installation

Source `4b41818983ebdc919ecd8d711cf7d59d20fa8f53` rebuilt successfully
(`build-neighbors12-final.log`) after tooltip translation refresh. An interim
link failed because the earlier UI check still held the EXE; retry after that
test exited passed. No user process was stopped.

Final executable: version, database-creation, resource-navigation checks PASS;
scenario integration 47/47 PASS (`final-checks.json`). Initial sandboxed scenario
check failed creating an isolated recovery snapshot and timed out; rerun with
test-folder access passed. General UI check timed out at 120 seconds as in
prior builds; its own test process was stopped. Not PASS.

Fresh owned `build-staging-12-neighbors`: deployment, isolated version smoke,
finalizer PASS. Independently checked exactly one current changelist, all
100 manifest digests, full coverage, and fresh/staged/installed EXE equality.
Permanent `artifacts/EI-Mapper-Ataman-2-first-test` was closed and updated:
6 managed files replaced, 0 removed, 1486 unlisted user files unchanged by hash.
EXE SHA-256: `18C0493CE9CB8880EAAFFA001F05D4EEAEB9278CA1CF89EDE6BCD4207E3E6F5E`.
Runtime components also match the fresh build. Retained rollback:
`build-rollback-neighbors12` (previous version 11); older owned
`build-rollback-layout11` (version 10) hash-verified before removal.

## Remaining acceptance

Open the original zone9 view with neighbors enabled. Check buildings, trees,
rocks and lighting while changing time of day. Move a neighbor and verify
objects/shadows move together; switch maps rapidly and toggle neighbors to check
stale results and cleanup. Verify static-object visibility and shared sculpt
projection. Compare frame/load times before making performance claims.
Actual asynchronous scene rendering/cancellation has not been exercised by the
focused document/shader tests. Parent task remains open pending these checks
and evaluation of the requested separate GPU work.
