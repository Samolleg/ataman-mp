# EI Mapper Endurance 0.3.0-alpha.24 test notes

## Changes

- Successful character-appearance preparation is cached by profile, database,
  unit appearance inputs, and editor version. Reopening unchanged maps or
  restarting the editor can reuse the cached CPU-side result.
- Longer uncached background work reports `Resolving character data...` in the
  existing non-blocking progress area. Fast cache hits do not flash the UI.
- Keyboard camera movement now runs on a fixed 120 Hz motion step and is
  interpolated for rendering, reducing uneven lateral strafe jitter.
- The progress indicator remains visible across gaps between worker batches,
  and held camera input prevents foreground cold character resolution even on
  an interpolated frame with no positional delta.

## Focused checks

1. Open `gz18h`, wait for character resolution to finish, close and reopen it,
   and compare the first and warm-load progress duration.
2. Restart Endurance, reopen the same unchanged map, and confirm cached
   character appearances resolve substantially faster.
3. Change the active profile/database or an individual unit appearance and
   confirm that changed content is resolved again rather than remaining stale.
4. Hold `A` or `D` while panning across a detailed area and check that lateral
   motion remains smooth at both high and temporarily reduced frame rates.

## Known boundary

The persistent cache stores deterministic CPU-side appearance resolution. GPU
figures and textures remain normal process-lifetime resources and are recreated
after an editor restart.
