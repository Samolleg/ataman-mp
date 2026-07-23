# Environment rendering validation

This feature remains isolated on `feature/render-pipeline` until its viewport
quality is accepted. It must not be merged into `main` based on build success
alone.

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
- Geometry casts directional shadows only when its existing `Is shadow` value
  permits it. Static and animated casters use separate cached maps.
- Terrain receives directional shadows but is never submitted as a caster.
- Stock shadows are per-caster projected silhouettes over selected receivers.
  The editor approximates this with a broad cached static projection used by
  terrain and a tight, pivot-centred animated projection. Ordinary static
  meshes do not sample the broad map on themselves, avoiding non-stock roof
  and fence self-shadow acne.
- The upper-left viewport widget selects authored-texture **Unlit**, legacy
  **Simple shading**, or resource-driven **Full environment** preview. Moving
  its clock enables the existing transient time override and never edits MOB
  world settings.
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
   terrain; the ridge itself must not cast into the directional shadow map.
7. Inspect a map light from outside and inside its range. Illumination must
   attenuate continuously and disappear when the light is hidden or map-light
   rendering is disabled.
8. Toggle fog, sky, map lights, and shadows independently, then repeat unit
   selection and picking checks.
9. Switch Unlit, Simple shading, and Full environment in the viewport widget.
   Collapse/restore it and scrub through midnight, morning, noon, and evening.
   Entrance labels and selection overlays must remain authored-colour UI.
10. At a map edge, confirm full fog begins no farther than one 32-unit tile
    inward and the lower sky/cloud field blends into exactly the same colour.
11. Inspect an idle humanoid and held weapon at 04:45, noon, and 16:00. Feet
    and the weapon must meet their shadow without the former caster-base gap.
12. Toggle **Texture mipmapping** while viewing distant terrain. Enabled must
    reduce shimmer/grain without changing character faces, armor, or weapons;
    disabled must restore nearest level-zero terrain sampling immediately.
13. Load `gz5g`, `gz10g`, `bz7g`, or `gz20g` through a profile whose
    `map.txt` marks it `#sky cave`. Confirm there is no outdoor sky, the map
    fades into a black boundary, and moving the preview time does not rotate or
    recolor shadows. Confirm an ordinary `#sky normal` map remains outdoor.

## Known evidence boundary

The MOB schema proves omnidirectional map-light inputs but does not yet prove
the runtime meaning of `LIGHT_SHADOW`. Per-light shadow maps remain pending
executable/corpus evidence; the checkbox is preserved and not silently assigned
a guessed meaning.
