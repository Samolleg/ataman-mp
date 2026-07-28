# Emissive terrain and physical materials

## Confirmed behavior

Evil Islands has two independent authored emissive channels.

1. MPR terrain-overlay materials serialize RGB and `selfIllumination`. Their
   emitted RGB is the component-wise saturated product
   `material.rgb * selfIllumination`. The overlay uses that value as part of
   its own vertex lighting. The same upper-layer emission is also applied to a
   paired lower-land vertex when the overlay vertex is strictly above it. This
   creates the game's subtle interpolated terrain glow without an
   omnidirectional point light or screen-space bloom.
2. The physical-material archive supplies an emissive RGBA member for figure
   materials. In the shipped archive, only `magic.mat` is nonzero. The runtime
   promotes the Magic material's emissive RGB to `(1, 1, 1)` after loading it;
   this override is visible in the fingerprinted executable's material-loader
   path. Equipment can therefore be fully self-lit when its database material
   record has physical type `Magic`.

Terrain emission is selected on the canonical 33x33 shared vertex lattice. At
a tile boundary, the engine chooses the first active adjacent overlay material
in deterministic northwest, southwest, northeast, southeast order (with only
the applicable two candidates on an edge). It does not take a component-wise
maximum of all adjacent materials. Lower terrain receives that selected
emission only where `overlayZ - landZ > 0`; coincident or lower overlays add
none. Normal triangle interpolation supplies the narrow transition visible in
the game. There is no arbitrary-radius local light, so Endurance does not
synthesize one. Authored MOB `LIGHT` records and established particle lights
remain the spatial point-light inputs.

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

The same executable independently corroborates the terrain representation:
two paired 1,089-vertex layers, an 84-byte vertex stride, a `0x16554` byte
upper-layer offset, and a per-upper-vertex inactive sentinel. Those structural
facts match the source-confirmed shared-vertex selection and depth-gated lower
terrain illumination model.

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

- The overlay VBO carries static RGB emission selected on the canonical shared
  vertex lattice. The land VBO receives the same value only at paired vertices
  where the overlay is strictly higher. Duplicate batched tile vertices use the
  same canonical selection, so interpolation is continuous without per-frame
  buffer rebuilding.
- Full-environment lighting saturating-adds emission to direct light, then
  takes the existing channel-wise maximum with ambient and point-light
  candidates. The existing projected-shadow and fog stages remain in place.
- Water, swamp, lava, and future authored overlay types share the same general
  material rule; emission is not gated by texture appearance or tile name.
  Non-emissive materials remain neutral, and the positive-depth gate prevents
  the former whole-tile red wash across banks and cliff faces.
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
   the game comparison. Lava must remain red/self-lit and produce the same
   narrow, softly interpolated red illumination on immediately paired terrain
   vertices. Banks and cliff faces beyond those positive-depth vertices must
   retain ordinary environment lighting; there must be no broad tile-sized wash.
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
