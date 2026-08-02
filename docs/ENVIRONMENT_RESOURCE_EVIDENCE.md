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
uses `sky00`. The selected profile's `texts.res/map.txt` is authoritative for
the environment class: `#sky cave` disables the outdoor sky and selects the
black cave boundary/fog path. This identifies `gz5g`, `gz10g`, `bz7g`, and
`gz20g` without a map-name list and lets compatible mods declare the same
behavior. Missing or malformed geometry/texture disables the outdoor pass with
a diagnostic rather than substituting unrelated sky members.

A read-only corpus scan of every available Base/JMV `map.txt` yields only the
environment values `normal` and `cave` (109 normal and 121 cave entries across
the duplicated language/reference corpora). No map record selects `sky02`
through `sky05`, and the executable evidence above exposes no independent
per-map outdoor-sky selector. Endurance therefore does not guess a sky from a
map name: `normal` uses provider-scoped `sky00`, while `cave` uses the proven
black boundary path. Additional provider sky selection remains fail-closed
until a manifest or corpus field establishes it.

The stock `lightscavegipat.ini`, `lightscaveingos.ini`, and
`lightscavesuslanger.ini` tables are identical at every hour: sunlight is
`161,179,170`, ambient is `75,75,89`, and sky is black. Cave MOBs nevertheless
store distinct `WS_TIME` values, but the native cave lighting path bypasses the
outdoor clock track and supplies the same fixed normalized directional-light
ray for every cave. Cave color and shadow direction are both time-invariant;
the editor time preview cannot rotate cave shadows.

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

Stock projected shadows are composited silhouettes, rather than attenuation of
only the direct-sun term. The editor therefore applies shadow visibility to the
combined environment illumination before adding local map lights. The accepted
runtime union-shadow opacity of 80/255 corresponds to a fully occluded
visibility near 0.68 and supplies the bounded preview value.

No game archive was modified. Diagnostic DDS/PNG copies and the read-only
geometry report used for inspection live outside the repository under
`Codex/work/environment-sky-probe` and `Codex/work/sky-geometry-report.json`.
