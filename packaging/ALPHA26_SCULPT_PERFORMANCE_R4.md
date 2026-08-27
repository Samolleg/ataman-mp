# Landscape and overlay sculpt responsiveness r4

## What changed

- Terrain sculpt stamps now decode and rebuild only land tiles touched by
  changed height or normal bytes. They no longer rebuild the complete sector
  and its untouched overlay layer every 33 milliseconds.
- Overlay sculpt stamps again use the existing incremental overlay delta path.
  Its vertex-buffer upload retains the safe orphaning behavior, avoiding both
  complete land-sector decoding and writes into an in-flight GPU allocation.
- Terrain dirty-state comparisons are limited to sectors actually touched by
  the command rather than scanning every sector after every stamp.
- Main-window dirty-title/layout publication is deferred until stroke release.
  The canonical MPR dirty bit still updates immediately.
- Brush cadence, stationary-hold strength, seams, normals, undo/redo, and save
  transactions are unchanged.

## Focused checks

1. On the same map and camera used for r3, hold a terrain Raise/Lower/Noise
   stroke stationary and compare viewport FPS and input responsiveness.
2. Drag a terrain stroke across a sector boundary and verify the surface and
   lighting normals remain continuous.
3. Repeat both checks while sculpting an overlay.
4. Undo and redo each stroke and confirm the exact geometry is restored.
5. Save/reopen only if convenient; the serialized transaction path was not
   changed by this performance pass.

## Expected diagnostics

The workflow log continues to emit one aggregate overlay timing record when a
stroke ends. It does not log per-stamp messages on the hot path.
