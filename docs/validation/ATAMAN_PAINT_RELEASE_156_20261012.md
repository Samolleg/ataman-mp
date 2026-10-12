# EI-PAINT-RELEASE-156 — 2026-10-12

## Cause and scope

The owner confirmed that strokes appeared while dragging and disappeared on mouse release in installed155. Its workflow log records Ground/Common 1999753121, applied stamps and changed cells followed by refused=1 on every stroke.

`paintWorldTerrainAt` deliberately delegates a single-map Mixed-edge stroke back to the local path. `resolveMixedEdgeTerrain` then incorrectly refused that stroke whenever any neighbor was selected, even when the brush never touched that neighbor. Production automatically selects neighbors; the previous155 isolated-view tests omitted that context.

156 removes this obsolete global selection guard. Multi-map routing remains in the existing world painting implementation. Actual resolution errors retain rollback and now include the detailed report in workflow.log. No HD/seam candidate changes are integrated.

## Evidence

Private copied zone1 MPR and HDPack_Terrain.res; source game resources read only. Hidden native Windows Qt/OpenGL context, production neighbor setter, native mouse press/move/release, GPU capture, exact sector Undo/Redo, in-memory clipboard and isolated backups.

- `build-tests-155-matrix/fixture-release156/neighbor-before`: baseline fails at mouse release with `terrain.mixed_edge.single_map`; preview is rolled back. The diagnostic fixture initially flattened its single XY array; this reproduced at the map corner. XY input validation was subsequently added.
- `neighbor-after`: actual XY points55,40 and80,65, shared brush and distant selected neighbor;34 PASS/0 FAIL, drag retained after release, GPU changed, other layer preserved, exact Undo/Redo.
- `all-modes-neighbors`: all material/surface/sculpt modes, held C, animation preservation and save/reopen. Initial diagnostic run used the local stamp counter for world strokes; that counter is not incremented by world painting. The regression now compares terrain snapshots at successive native mouse events instead.

Clean Release156, final matrix and guarded installation are pending. No interactive editor or game acceptance is claimed.
