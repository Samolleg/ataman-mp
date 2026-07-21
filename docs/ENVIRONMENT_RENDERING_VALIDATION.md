# Environment rendering validation

This feature remains isolated on `feature/render-pipeline` until its viewport
quality is accepted. It must not be merged into `main` based on build success
alone.

## Data contract

- Sky faces are the selected profile's `sky00` through `sky05` MMP resources.
- The active MOB `WORLD_SET` supplies `WS_TIME`, `WS_AMBIENT`, and
  `WS_SUN_LIGHT`. Preview override is transient and never changes the MOB.
- MOB `LIGHT` records expose position, RGB color, range, and `LIGHT_SHADOW`.
  The format contains no direction or cone fields, so these records are
  rendered as omnidirectional map lights. Directional spotlight semantics must
  not be invented without additional corpus or executable evidence.
- Geometry casts directional shadows only when its existing `Is shadow` value
  permits it. Static and animated casters use separate cached maps.

## Focused runtime checks

1. Load the same Base and JMV map with environment rendering off and verify the
   compatibility view is unchanged.
2. Enable the profile skybox and inspect all six directions plus straight up
   and down. Missing faces must produce a diagnostic and disable the whole pass.
3. Enable time override and scrub 00:00, 05:30, 06:00, 12:00, 18:00, and 23:59.
   Sky tint, skylight, directional light, fog, and shadows must change together
   without discontinuities. The override must not dirty the MOB.
4. Move the camera across a 32-unit sector boundary while watching long static
   shadows. Static caches may refresh, but shadows must not disappear at the
   viewport edge.
5. Check an animated unit beside static scenery. The unit shadow should refresh
   while the static shadow cache remains reusable.
6. Inspect a map light from outside and inside its range. Illumination must
   attenuate continuously and disappear when the light is hidden or map-light
   rendering is disabled.
7. Toggle fog, skybox, map lights, and shadows independently, then repeat unit
   selection and picking checks.

## Known evidence boundary

The MOB schema proves omnidirectional map-light inputs but does not yet prove
the runtime meaning of `LIGHT_SHADOW`. Per-light shadow maps remain pending
executable/corpus evidence; the checkbox is preserved and not silently assigned
a guessed meaning.
