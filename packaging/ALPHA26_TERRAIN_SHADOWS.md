# Alpha 26 terrain and sun-shadow fidelity test

## What changed

- Added **Settings > Render > Terrain casts directional shadows**. When
  enabled, hills and cliffs apply the game's heightfield-based sun visibility
  to terrain lighting.
- Terrain self-shadowing is cached independently of the existing object and
  point-light shadow atlases. It does not add a scene-object traversal.
- Terrain edits and sun-direction changes rebuild the cached visibility field.
  Disabling the option uploads full terrain visibility immediately after Apply
  or Save/Close, without requiring camera movement.
- Corrected the outdoor sun ray by half a turn. At the same authored time,
  projected shadows now follow the game's map-space direction while retaining
  the established elevation curve and shadow-length ratio.
- Removed terrain's packed-depth self-comparison, eliminating the
  triangle-aligned bias stripes on steep slopes while preserving the existing
  object contact-shadow path.
- Point-light shadow atlases, torch semantics, animated-shadow cadence, the
  NVIDIA overlay guard, minimap capture, and ANGLE fallback are unchanged.

## Focused manual test

1. Start the desktop OpenGL launcher, load an outdoor MOB/MPR, choose **Full
   environment**.
2. Open **Settings > Render**. With a fixed camera, disable **Terrain casts
   directional shadows**, press **Apply**, and confirm ridge/cliff self-shadow
   disappears immediately while object shadows remain. Re-enable it and use
   **Save/Close**; the terrain self-shadow should return without camera motion.
3. Sculpt or preview a terrain height change near a visible ridge. Confirm its
   terrain shadow refreshes without moving or rotating the camera.
4. Hold the camera fixed and scrub to 06:00, 12:00, and 18:00. Shadows should
   follow the sun continuously, with noon shadows shorter than morning/evening
   shadows. Orbit the camera and confirm the world-space direction is stable.
5. In Zone 11, confirm campfire/torch point-light shadows still respond to
   distance and radius, independently of the terrain-shadow toggle.
6. Start the ANGLE compatibility launcher and confirm Unlit/Simple shading
   remain usable without shadow-atlas warnings or a white viewport.

## Known limitations

- Terrain self-shadow visibility is sampled at the one-world-unit heightfield
  grid and interpolated by the terrain mesh. Object receivers continue to use
  the existing directional shadow atlases.
