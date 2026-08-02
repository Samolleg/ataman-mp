# Alpha 26 terrain and sun-shadow fidelity test

## What changed

- Added **Settings > Render > Terrain casts directional shadows**. When
  enabled, hills and cliffs apply the game's heightfield-based sun visibility
  to terrain lighting.
- Terrain and object blockers now resolve through one strongest-shadow mask and
  one stock 80/255-opacity overlay. Their overlap has the same color and opacity
  as either silhouette alone.
- Terrain self-shadowing is cached independently of the existing object and
  point-light shadow atlases. It does not add a scene-object traversal. Its
  immutable heightfield calculation runs in the background; slider changes are
  coalesced and only the newest complete result is uploaded on the render thread.
- Terrain edits and sun-direction changes rebuild the cached visibility field.
  Disabling the option uploads full terrain visibility immediately after Apply
  or Save/Close, without requiring camera movement.
- Restored the native outdoor clock angle after the first terrain-shadow pass
  had reversed it by half a turn. Terrain self-shadows use the accepted proxy's
  three-times reach while object projections retain the native elevation curve.
- Cave and dungeon maps now use the game's fixed normalized diagonal sun ray.
  Their directional shadows no longer inherit the MOB time or the outdoor clock
  track.
- Weather colors now select their table from the map's authored allod ID rather
  than its unrelated zone-type field, so Ingos maps such as Zone 11 no longer
  fall back to Gipat colors.
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
3. Put an object shadow across a terrain self-shadow. Confirm both merge into
   one silhouette with no darker or differently colored overlap.
4. Drag the time slider quickly. The viewport should remain responsive; the
   last complete terrain field may remain briefly, then settle to the final
   slider position after release.
5. Sculpt or preview a terrain height change near a visible ridge. Confirm its
   terrain shadow refreshes without moving or rotating the camera.
6. Hold the camera fixed and scrub to 06:00, 12:00, and 18:00. Shadows should
   follow the game screenshots' map-space direction, with noon shadows shorter
   than morning/evening shadows. Orbit the camera and confirm direction is stable.
7. In Zone 11 near 04:35, compare shaded snow and the sky to the game reference.
   They should use the authored Ingos ambient/sky tables rather than silently
   falling back to Gipat; direct sunrise light remains intentionally warm.
8. In Zone 11, confirm campfire/torch point-light shadows still respond to
   distance and radius, independently of the terrain-shadow toggle.
9. Load a map whose catalog sky type is `cave` (for example Zone 12 Dungeon 2).
   Confirm the directional shadows run diagonally along the fixed cave vector,
   remain stable while changing the preview time, and do not inherit the outdoor
   sun angle.
10. Start the ANGLE compatibility launcher and confirm Unlit/Simple shading
   remain usable without shadow-atlas warnings or a white viewport.

## Known limitations

- Terrain self-shadow visibility is sampled at the one-world-unit heightfield
  grid and interpolated by the terrain mesh. Object receivers continue to use
  the existing directional shadow atlases.
- The lighting-table selection bug is fixed, but this pass does not claim full
  pixel color parity: the supplied game/editor cameras and output pipelines are
  not pixel-aligned enough to retune independently derived hourly RGB curves.
