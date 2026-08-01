# Alpha 26 point-light shadows revision 2 test

## What changed

- A map light with **Cast shadows** enabled now contributes its color,
  attenuation, and direction to the projected scene shadow.
- Turning **Cast shadows** off leaves that light's illumination visible but
  removes its influence from the shadow direction.
- Torch-generated point lights now participate in the combined projected
  shadow by default.
- Shadow-producing lights remain eligible when the camera rotates away from
  the light itself; color-light frustum culling no longer changes shadow
  ownership.
- Off-screen caster collection follows the same combined light direction.

The existing sun cascades, static/dynamic cache cadence, NVIDIA overlay guard,
ANGLE compatibility fallback, and performance counters were not replaced.
This change adds no framebuffer, texture sampler, shadow pass, or full-scene
traversal.

## Focused manual test

1. Start the desktop OpenGL launcher and load a MOB/MPR scene containing a
   point light, a visible shadow-casting object, and terrain beneath it.
2. Select **Full environment**, enable **Scene point lights**, and use a nonzero
   shadow quality.
3. Move the point light beside the caster and confirm its illumination remains
   visible and the projected silhouette points away from the light.
4. Toggle the light's **Cast shadows** property off. Confirm illumination
   remains but the point light no longer steers the silhouette.
5. Toggle it on again, move the light to the opposite side, and confirm the
   silhouette changes direction without disappearing or flickering.
6. In Zone 11, inspect ordinary torches beside visible casters and confirm
   their projected shadows respond without adding a **Cast shadows** property
   to the torch record.
7. With light 121 influencing nearby shadows, orbit the camera through a full
   rotation without moving the pivot. Confirm the shadow direction does not
   snap back to the sun when the light leaves the viewport.
8. Check an ordinary outdoor scene to confirm sun shadows still render, then
   start the ANGLE compatibility launcher and confirm Unlit/Simple shading
   remain usable without shadow-atlas warnings or a white viewport.

## Known limitation

Endurance retains its existing view-centred cascade approximation. The stock
game computes the combined light direction per caster, while the editor uses
the viewport shadow focus for the shared cascade direction.
