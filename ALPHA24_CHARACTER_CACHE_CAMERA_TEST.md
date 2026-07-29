# EI Mapper Endurance 0.3.0-alpha.24 test notes

## Changes

- Successful character-appearance preparation is cached by profile, database,
  unit appearance inputs, and editor version. Reopening unchanged maps or
  restarting the editor can reuse the cached CPU-side result.
- Character preparation now remains active until the complete equipment,
  figure, layered texture, GPU-resource, and initial idle-pose work is ready.
  It reports `Resolving full character visuals...` in the existing
  non-blocking progress area instead of declaring completion after the much
  smaller database-plan phase.
- CPU-safe database resolution remains on the worker. OpenGL-bound completion
  is performed in bounded, camera-prioritized batches so characters are warmed
  before traversal reaches them rather than remaining in their initial pose.
- Keyboard camera movement uses the actual bounded frame delta. It is no longer
  quantized to a 120 Hz simulation/interpolation phase, which could make
  VSync-limited lateral movement visibly uneven despite a stable frame rate.
- The progress indicator remains visible across gaps between worker batches,
  and held camera input prevents foreground cold character resolution even on
  an interpolated frame with no positional delta.
- Every MOB Family Validation column can now be resized manually; the initial
  widths remain readable when the panel first opens.

## Focused checks

1. Open `gz18h`, wait for character resolution to finish, close and reopen it,
   and compare the first and warm-load progress duration.
2. Restart Endurance, reopen the same unchanged map, and confirm cached
   character appearances resolve substantially faster.
3. Change the active profile/database or an individual unit appearance and
   confirm that changed content is resolved again rather than remaining stale.
4. Open `gz11k`, leave VSync enabled, hold `A` or `D`, and check that lateral
   motion remains smooth while FPS is pinned to the display refresh rate.
5. In `gz18h`, wait for `Resolving full character visuals...` to complete,
   then pan across the map and confirm newly encountered humanoids already use
   their resolved equipment/idle pose instead of initial placeholders.
6. Open MOB Family Validation and drag each column separator in its header.

## Known boundary

The persistent cache stores deterministic CPU-side appearance resolution. GPU
figures and textures remain normal process-lifetime resources and are recreated
after an editor restart.
