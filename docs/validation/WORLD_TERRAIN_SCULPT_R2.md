# EI-WORLD-SCULPT-001-GAPS — terrain preview context ownership

User recording `20260912-1215-58.5560116.mp4` shows black openings while
smoothing with world terrain enabled; the modified-map list includes zone1 and
zone2. Their shared boundary has 161 coincident XY vertices. A read-only check
of those installed boundary coordinates found no differing floating-point keys;
independent misaligned-grid behavior is not established as this recording's cause.

## First incorrect state and regression

`sculptTerrainTowardLatestPoint` defers active-map GPU uploads until
`endTerrainPreviewBatch`. Inside each world stamp, `refreshWorldTerrainNeighbors`
previously called `makeCurrent`, uploaded neighbors, then unconditionally called
`doneCurrent`. The subsequent active upload used `QOpenGLBuffer` without acquiring
a context. CPU geometry could advance while the active GPU buffer stayed stale.

The regression reproduces this ownership sequence using a hidden native legacy
Qt viewport and two actual GPU buffers. Expected: updating the neighbor preserves
the enclosing context; both buffers read back the new height after the active
upload. Before correction, the first context assertion failed (2 passed / 1
failed). With the ownership guard, all 3 test phases pass, no skips. It also checks
that a context acquired from a timer-like context-free entry is released on exit.

Neighbor refresh now preserves its caller's context. Active immediate uploads,
deferred batch uploads and retained-map restoration acquire the viewport context
for their own work and restore the previous state. Height formulas, map placement,
save payloads and the approved independent-grid policy are unchanged.

## Validation and limits

- Native Windows GPU regression: 3 passed, 0 failed, 0 skipped.
- Full offscreen Landscape Tools suite: 16 passed, 0 failed, 2 context skips.
  The new GPU test ran separately on the Windows platform, rather than accepting
  its offscreen skip as validation.
- World terrain core: 9 passed, 0 failed.
- Release editor and x86 components built successfully.
- Full `--ui-widget-check`: existing `0xC0000005` remains unresolved.
- The exact recorded terrain scene has not been replayed interactively. This
  fixes a reproduced rendering defect; disappearance of every recorded gap,
  save/reopen and full parent acceptance still require the user's scene check.
- No installed map files or user settings were modified by this investigation.

R2 is separate to preserve the user's R1 scene/settings for A/B comparison.
See the current tester instructions in `packaging/WORLD_TERRAIN_SCULPT_R2.md`.
