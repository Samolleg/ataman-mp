# Alpha 26 point-light shadows revision 3 test

## What changed

- A map light with **Cast shadows** enabled now contributes its color,
  attenuation, and direction at each shadow caster instead of at the camera
  pivot.
- Turning **Cast shadows** off leaves that light's illumination visible but
  removes its influence from the shadow direction.
- Torch-generated point lights and other authored shadow-producing objects now
  participate in the caster-local projected shadow by default.
- Shadow-producing lights remain eligible when the camera rotates away from
  the light itself; color-light frustum culling no longer changes shadow
  ownership.
- A point light influences only casters inside its radius. Moving the camera or
  rotating around the scene no longer changes that range decision or snaps the
  shadow back to the sun direction.
- Directional/sun shadows remain in the existing near/far cascades. Up to 16
  nearest visible point-influenced casters receive independent local atlas
  projections, so multiple lights can produce different combined directions
  at different objects in the same frame.

The existing sun cascades, static/dynamic cache cadence, NVIDIA overlay guard,
ANGLE compatibility fallback, and performance counters were not replaced. The
desktop path adds one shadow atlas and reuses the existing render queue; it does
not add another full-scene traversal.

## Focused manual test

1. Start the desktop OpenGL launcher and load a MOB/MPR scene containing a
   point light, a visible shadow-casting object, and terrain beneath it.
2. Select **Full environment**, enable **Scene point lights**, and use a nonzero
   shadow quality.
3. Move the point light beside the caster and confirm its illumination remains
   visible and the projected silhouette points away from the light. Move it
   outside the radius and confirm that caster returns to its sun-only shadow.
4. Toggle the light's **Cast shadows** property off. Confirm illumination
   remains but the point light no longer steers the silhouette.
5. Toggle it on again, move the light from the radius edge toward the caster,
   then to the opposite side. Confirm influence is strongest nearby, does not
   disappear close to the caster, and changes direction without flickering.
6. In Zone 11, inspect ordinary torches beside visible casters and confirm
   their projected shadows respond without adding a **Cast shadows** property
   to the torch record.
7. With light 121 influencing nearby shadows, orbit the camera through a full
   rotation without moving the pivot. Confirm the shadow direction does not
   snap back to the sun when the light leaves the viewport.
8. Check an ordinary outdoor scene to confirm sun shadows still render, then
   start the ANGLE compatibility launcher and confirm Unlit/Simple shading
   remain usable without shadow-atlas warnings or a white viewport.

## Known limitations

- The local atlas is capped at the 16 nearest visible shadow casters. Remaining
  casters keep their normal sun shadow until they enter the local set.
- Point-shadow candidates currently come from the existing visible/sun-swept
  render queue. A fully off-screen point-influenced caster cannot project into
  view unless that existing collection already includes it.
- ANGLE compatibility mode retains its established reduced lighting path and
  does not allocate the desktop shadow atlases.
