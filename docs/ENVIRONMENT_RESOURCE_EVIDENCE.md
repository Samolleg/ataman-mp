# Environment resource evidence

Read-only inspection of Base `Res/textures.res` establishes the shipped
`sky00.mmp` through `sky05.mmp` members. Every member is a 256 by 256 DXT1 MMP
with seven mip levels. Their presence does not mean they are six cube faces.

The decisive runtime evidence is:

- `game.exe` constructs one sky object and assigns `Sky00` or `Sky01` to that
  object's single material slot. The choice is coupled to the nearby `Cave`
  state, not to six simultaneous faces.
- Captured resource transitions show `Sky00.mmp` at the stock sky draw call and
  `Sky01.mmp` when the alternate state is entered.
- The stock `nask0sky.fig` is a pre-signature six-morph FIG: 380 source
  vertices, 1,944 triangle indices, seven height rings, about 54 angular
  samples per ring, radius about 40 and height about 38.6. It is an open,
  rounded cylindrical dome with deliberately repeating UVs, not a cube and not
  a capped cylinder.

Endurance reads `nask0sky.fig` through the selected profile's immutable
resource catalog and uploads its actual triangles and UVs. The outdoor preview
uses `sky00`; automatic `sky01` cave-state selection is deferred until the map
loader exposes that state explicitly. Missing or malformed geometry/texture
disables the pass with a diagnostic rather than substituting unrelated sky
members. Rotation follows the same clock phase as the directional light.

The fingerprinted JMV lighting investigations also establish that the light
vector remains slanted and rotates around the map. It does not cross a horizon
or reach a vertical zenith. Weather RGB changes continuously, while terrain and
object publication cadences caused the visible stepping investigated in the
runtime patch. Endurance therefore keeps MOB ambient/sun intensity fields
authoritative, rotates a constant-elevation direction, and applies a bounded
cyclic hue approximation until exact provider weather-RGB curves are exposed.

Terrain is deliberately excluded from the shadow-caster pass. The game builds
terrain as a projected-shadow receiver and relights its vertex colours through
the separate weather path; treating it as a caster was an editor-only error.

No game archive was modified. Diagnostic DDS/PNG copies and the read-only
geometry report used for inspection live outside the repository under
`Codex/work/environment-sky-probe` and `Codex/work/sky-geometry-report.json`.
