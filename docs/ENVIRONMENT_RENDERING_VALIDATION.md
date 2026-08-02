# Environment rendering validation

This renderer is integrated into `main` and forms part of the `0.3.0` closed-test
baseline. Build success alone is not the acceptance criterion: the focused
viewport/runtime checks below remain part of the `0.3.0` release matrix, and
profile-specific behavior remains bounded by the recorded resource and
fingerprinted executable evidence.

## Data contract

- Sky geometry is the selected profile's `nask0sky.fig`; its outdoor material
  is `sky00.mmp`. The selected profile's `map.txt` `#sky cave` value suppresses
  this pass and selects black boundary fog.
- The active MOB `WORLD_SET` supplies `WS_TIME`, `WS_AMBIENT`, and
  `WS_SUN_LIGHT`. Preview override is transient and never changes the MOB.
- MOB `LIGHT` records expose position, RGB color, range, and `LIGHT_SHADOW`.
  The format contains no direction or cone fields, so these records are
  rendered as omnidirectional map lights. Directional spotlight semantics must
  not be invented without additional corpus or executable evidence.
- Global environment illumination and local map lights use channel-wise maxima,
  matching the fingerprinted native terrain path rather than summing the eight
  nearest camera lights. Endurance retains up to 64 eligible lights, which
  covers the complete audited JMV MOB corpus without camera-relative
  truncation. Point-light attenuation follows the native distance/range curve.
- Geometry casts directional shadows only when its existing `Is shadow` value
  permits it. Static and animated casters use separate cached maps.
- Map lights with `LIGHT_SHADOW` enabled contribute their attenuated luminance
  and caster-relative direction to the projected-shadow direction. Disabled
  map lights still illuminate but do not steer the shadow projection.
- Terrain always receives directional shadows. When **Terrain casts directional
  shadows** is enabled, visible hills and cliffs are also submitted to the
  existing near/far static sun atlases. Terrain and object blockers therefore
  share one nearest-depth result; the option does not create a second shadow
  mask or affect point-light atlases.
- Stock shadows are per-caster projected silhouettes over selected receivers.
  The editor approximates this with a broad cached static projection used by
  terrain and a tight, pivot-centred animated projection. Ordinary static
  meshes do not sample the broad map on themselves, avoiding non-stock roof
  and fence self-shadow acne.
- The upper-left **Time of day** widget is independent from the **Lighting
  mode** widget beneath it. Its clock remains available in **Unlit**, **Simple
  shading**, and **Full environment** modes. Moving the clock enables the
  transient time override and never edits MOB world settings.
- A selected time override is delivered to JMV Test Current Map and every hot
  reload through the hash-owned autorun-MOB bootstrap as
  `RunWorldTime(hours)`. This sets the requested starting hour and lets the
  game clock continue normally. The staged primary MOB and its script remain
  exact.
- Map-edge fog reaches full density over four world units: two authored
  two-unit terrain tiles. The
  authored sky cylinder is lowered by one of its own vertical rings and its
  lower two rings blend to the same fog colour, hiding the open-cylinder seam.
- Terrain atlases upload their authored DXT mip chain. The render-setting
  toggle changes terrain sampling only; humanoid/equipment atlases retain the
  established compatibility path.
- Shadow depth is packed across the RGBA8 target rather than truncated to one
  eight-bit channel. This preserves contact silhouettes without increasing the
  4096 static and 2048 animated map allocations.

## Focused runtime checks

1. Load the same Base and JMV map with environment rendering off and verify the
   compatibility view is unchanged.
2. Enable the profile sky and inspect the full horizon and dome. Confirm the
   `nask0sky` UV seam remains stable while rotating and map-boundary fog
   blends the authored open lower ring into the atmospheric colour. Missing
   geometry or `sky00` must produce a diagnostic and disable the whole pass.
3. Enable time override and scrub 00:00, 05:30, 06:00, 12:00, 18:00, and 23:59.
   Sky tint, skylight, directional light, fog, and shadows must change together
   without discontinuities. The override must not dirty the MOB.
4. Move the camera across a 32-unit sector boundary while watching long static
   shadows. Static caches may refresh, but shadows must not disappear at the
   viewport edge.
5. Check an animated unit beside static scenery. The unit and held weapon must
   produce a legible silhouette near the viewport pivot while the static shadow
   cache remains reusable.
6. Put a tall static object beside a terrain ridge. The object must cast onto
   terrain. Toggle **Terrain casts directional shadows**: the ridge silhouette
   must disappear/reappear while object shadows remain. Terrain edits must
   invalidate the cached static sun atlases without rotating the camera.
7. Inspect a map light from outside and inside its range. Illumination must
   attenuate continuously and disappear when the light is hidden or map-light
   rendering is disabled. Beside a visible caster, toggle **Cast shadows**:
   illumination must remain, while the projected silhouette stops/starts
   following the point light.
8. Toggle fog, sky, map lights, and shadows independently, then repeat unit
   selection and picking checks.
9. Switch Unlit, Simple shading, and Full environment in the Lighting Mode
   widget. Collapse/restore it and confirm the separate Time of Day control
   remains enabled while scrubbing midnight, morning, noon, and evening.
   Entrance labels and selection overlays must remain authored-colour UI.
10. With a JMV test running, set a distinctive editor time, invoke Test Current
    Map, then change the time and hot reload. Confirm each generation starts at
    the selected value, the clock continues normally, and the source MOB hash
    is unchanged.
11. At a map edge, confirm full fog begins no farther than one 32-unit tile
    inward and the lower sky/cloud field blends into exactly the same colour.
12. Inspect an idle humanoid and held weapon at 04:45, noon, and 16:00. Feet
    and the weapon must meet their shadow without the former caster-base gap.
13. Toggle **Texture mipmapping** while viewing distant terrain. Enabled must
    reduce shimmer/grain without changing character faces, armor, or weapons;
    disabled must restore nearest level-zero terrain sampling immediately.
14. Load `gz5g`, `gz10g`, `bz7g`, or `gz20g` through a profile whose
    `map.txt` marks it `#sky cave`. Confirm there is no outdoor sky, the map
    fades into a black boundary, and moving the preview time does not rotate or
   recolor shadows. Confirm an ordinary `#sky normal` map remains outdoor.
15. On an outdoor map, hold the camera fixed and inspect 06:00, 12:00, and
    18:00. The sun-shadow ray must point approximately -Y, +X, and +Y in map
    space respectively; noon shadows must be shorter than morning/evening
    shadows. Orbiting the camera must not rotate those world-space directions.
16. Repeat startup through the ANGLE compatibility launcher. Full environment
    remains unavailable and no directional or terrain shadow atlas may be
    allocated; Unlit and Simple shading must remain usable.

## Known evidence boundary

`LIGHT_SHADOW` controls participation in the combined projected silhouette; it
does not introduce a cone or a separate per-light shadow map. Non-Tendri
particle families and the special native local-light flag remain explicitly
outside the confirmed model; see
[particle reconstruction coverage](PARTICLE_RENDERING_STATUS.md) and the
[runtime evidence ledger](investigations/2026-07-23-particle-runtime.md).
