# Emissive terrain and physical materials

## Confirmed behavior

Evil Islands has two independent authored emissive channels.

1. MPR terrain-overlay materials serialize RGB and `selfIllumination`. Their
   emitted RGB is the component-wise saturated product
   `material.rgb * selfIllumination`. The overlay uses that value as part of
   its own vertex lighting. It does not write emission into the land geometry
   below or beside the overlay and does not generate an omnidirectional point
   light or screen-space bloom. The overlay texture's authored alpha confines
   the visible emissive result to the rendered liquid surface.
2. The physical-material archive supplies an emissive RGBA member for figure
   materials. In the shipped archive, only `magic.mat` is nonzero. The runtime
   promotes the Magic material's emissive RGB to `(1, 1, 1)` after loading it;
   this override is visible in the fingerprinted executable's material-loader
   path. Equipment can therefore be fully self-lit when its database material
   record has physical type `Magic`.

These channels affect the emitting geometry's lighting. There is no evidence
that `selfIllumination` creates an arbitrary-radius local light, so Endurance
does not synthesize one. Authored MOB `LIGHT` records and established particle
lights remain the only spatial point-light inputs.

## Read-only evidence

The audited Base `materials.res` is 870 bytes, SHA-256
`255C8EA1E616F1D4350BC3369BCB22DCCD68FFCD24D9DCB59913FD5683784880`.
Its eight members contain these emissive RGB values:

| Physical material | Emissive RGB in the archive | Runtime result |
| --- | --- | --- |
| crystal | 0, 0, 0 | non-emissive |
| wood | 0, 0, 0 | non-emissive |
| cloth | 0, 0, 0 | non-emissive |
| metal | 0, 0, 0 | non-emissive |
| leather | 0, 0, 0 | non-emissive |
| skin | 0, 0, 0 | non-emissive |
| stone | 0, 0, 0 | non-emissive |
| magic | 0.7, 0.7, 0.7 | forced to 1, 1, 1 |

The corresponding executable is SHA-256
`B871D70D3876B2A5634F0920C92118BC49058B50C7D8CBE68376349F51C2E570`.
Its loader reads the four material color vectors and power from each member,
then explicitly writes binary32 `1.0` to the three emissive channels for the
Magic material type. This independently confirms that the archive's `0.7`
value is not the final runtime intensity.

A complete read-only walk of 108 Base and active Jamais Vu MPR files found 11
nonzero terrain-material definitions. Every definition is the same record:

- terrain type 3;
- RGBA `(0.7960785031, 0, 0, 1)`;
- `selfIllumination = 1`;
- `waveMultiplier = 0.5`;
- `warpSpeed = 0`.

Those definitions account for 11,156 active overlay uses, and every use has
tile type 13 (lava). Thus the format and renderer support general colored
terrain emission, while the installed Base/Jamais Vu corpus authors it only
for lava.

The active Jamais Vu `zone16.mpr`, SHA-256
`1E67B69121CF837D40B11618ADE9FB791CDFB4DB3546ACC6CB24D9F679B63811`,
contains 2,403 lava tiles in 27 liquid sectors. Its paired MOB, SHA-256
`3DF96CE7648E9834FE518C221812C4EDFC91A47A855EB0023C63AFB798A5BCA4`,
contains no `LIGHT` or `PARTICL` records. This rejects an authored MOB point
light or particle source as the cause of the lava lighting in the comparison
capture.

No installed game, map, archive, database, or private reference was modified.

## Endurance implementation

- The overlay VBO carries a static RGB emission attribute derived once from the
  referenced MPR material. The land VBO carries an explicit neutral value, so
  overlay self-illumination cannot leak into terrain tiles or cliff faces. No
  per-frame buffer rebuild is introduced.
- Full-environment lighting saturating-adds emission to direct light, then
  takes the existing channel-wise maximum with ambient and point-light
  candidates. The existing projected-shadow and fog stages remain in place.
- Land under every overlay material receives exact zero emission. Water, swamp,
  lava, and future authored overlay types share the same general
  material rule; emission is not gated by texture appearance or tile name.
- Equipment resolution emits a per-part Magic flag only from an authoritative
  active-profile database material record. The render queue includes emission
  in batching compatibility so instanced draws cannot mix emissive and
  non-emissive instances. Ordinary world objects are not guessed emissive when
  their MOB/model path has no physical-material identity.
- Unlit compatibility mode remains raw texture color. Simple and full
  environment modes apply the emissive lighting floor. Selection and picking
  are unchanged.

## Focused runtime validation

1. Load Jamais Vu `zone16` in Full environment at the same time and camera as
   the game comparison. Lava must remain red/self-lit while surrounding land and
   cliff faces retain ordinary environment lighting. There must be no red tile
   halo outside the authored overlay alpha.
2. Toggle Unlit, Simple shading, and Full environment. Lava emission must be
   stable in the two lit modes; Unlit remains the authored texture preview.
3. Toggle landscape editing. Water motion must still become exactly neutral;
   lava emission must remain visible because editing disables geometry motion,
   not material identity.
4. Load water-heavy `zone18`. Water and swamp with zero self-illumination must
   remain non-emissive while their previously validated motion remains intact.
5. Inspect a unit equipped with an active-profile `.magic` item. Only the
   resolved equipment parts must be fully self-lit; skin, ordinary equipment,
   selection color, and unrelated world objects must remain unchanged.
6. Repeat zone16 and the Magic-equipment case on desktop OpenGL and ANGLE.
   Shader compilation, picking, transparency, and instancing fallback must
   remain valid on both paths.
