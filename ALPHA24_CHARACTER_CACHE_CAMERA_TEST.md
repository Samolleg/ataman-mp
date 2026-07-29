# EI Mapper Endurance 0.3.0-alpha.24 test notes

## Changes

- Successful character-appearance preparation is cached by profile, database,
  unit appearance inputs, and editor version. Reopening unchanged maps or
  restarting the editor can reuse the cached CPU-side result. The persistent
  cache is portable and lives beside the editor under
  `cache/unit-appearances`, not in AppData. Its schema is recorded internally;
  incompatible or malformed cache data is discarded and rebuilt automatically.
- Character preparation now remains active until the complete equipment,
  figure, layered texture, GPU-resource, and initial idle-pose work is ready.
  It reports `Resolving full character visuals...` in the existing
  non-blocking progress area instead of declaring completion after the much
  smaller database-plan phase.
- Fixed a worker ownership race where a completed future could be replaced
  before its queued result handler ran, leaving approximately one batch of
  units permanently marked in-flight (the observed zone18 terminal remainder).
- Cache-hit counts and full-visual progress are now logged per accepted batch
  and every 32 completed units, so persisted-plan reuse is distinguishable
  from the unavoidable per-process GPU reconstruction phase.
- CPU-safe database resolution remains on the worker. An explicit 16 ms idle
  pump now keeps OpenGL-bound completion moving even when the viewport has no
  input or other repaint source; the reported zone18 387/402 stall is covered
  directly by a regression check.
- Background figure/texture finalization no longer inserts deliberate upload
  work into camera-movement frames. It advances one character per stationary
  pump tick; a character that genuinely enters view retains the immediate
  first-use path.
- The progress indicator remains visible across gaps between worker batches,
  and held camera input prevents foreground cold character resolution even on
  an interpolated frame with no positional delta.
- Every MOB Family Validation column can now be resized manually; the initial
  widths remain readable when the panel first opens.

## Focused checks

1. Open `gz18h`, wait for character resolution to finish, close and reopen it,
   and compare the first and warm-load progress duration.
2. Confirm the progress reaches 402/402 while leaving the camera completely
   idle. Restart Endurance, reopen the same unchanged map, and confirm the log
   reports appearance cache hits and that files exist in the editor-local
   `cache/unit-appearances` folder.
3. Change the active profile/database or an individual unit appearance and
   confirm that changed content is resolved again rather than remaining stale.
4. Open `gz11k`, leave VSync enabled, hold `A` or `D`, and report whether the
   remaining scene/frame-pacing jitter changes after character preparation is
   complete. The rejected camera-delta change is not claimed as its fix.
5. In `gz18h`, wait for `Resolving full character visuals...` to complete,
   then pan across the map and confirm newly encountered humanoids already use
   their resolved equipment/idle pose instead of initial placeholders.
6. Open MOB Family Validation and drag each column separator in its header.

## Known boundary

The persistent cache stores deterministic CPU-side appearance resolution. GPU
figures and textures remain normal process-lifetime resources and are recreated
after an editor restart.
