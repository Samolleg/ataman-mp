# Alpha 26 terrain and sun-shadow fidelity test

## What changed

- Added **Settings > Render > Terrain casts directional shadows**. When
  enabled, hills and cliffs cast sun shadows onto terrain and scene objects.
- Terrain and ordinary object blockers share the existing near/far directional
  depth atlases, producing one strongest-blocker result without a second
  shadow mask or another full-scene object traversal.
- Terrain edits and the toggle invalidate the cached static sun atlases so the
  result updates without requiring camera movement.
- Corrected the outdoor sun ray by half a turn. At the same authored time,
  projected shadows now follow the game's map-space direction while retaining
  the established elevation curve and shadow-length ratio.
- Point-light shadow atlases, torch semantics, animated-shadow cadence, the
  NVIDIA overlay guard, minimap capture, and ANGLE fallback are unchanged.

## Focused manual test

1. Start the desktop OpenGL launcher, load an outdoor MOB/MPR, choose **Full
   environment**, and use a nonzero shadow quality.
2. Open **Settings > Render**. With a fixed camera, toggle **Terrain casts
   directional shadows** off and on. Ridge/cliff silhouettes should disappear
   and return; ordinary object shadows should remain in both states.
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

- Terrain shadows use the editor's existing finite near/far directional
  coverage. Very distant terrain outside those cascades does not cast into the
  viewport.
- The terrain depth pass uses a conservative slope offset. Please report any
  visible self-shadow speckling or detached ridge contact so it can be tuned
  against the affected map and camera angle.
