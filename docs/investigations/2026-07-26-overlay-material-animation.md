# Overlay material animation investigation

## Outcome

Evil Islands liquid overlays use wind-driven vertex deformation. They do not
use a generic fragment-only UV drift. The renderer implementation is therefore
GPU vertex deformation on the existing sector-batched water draw, with authored
material parameters packed into the static water VBO.

This note records the behavior contract used by Endurance. It separates
serialized authorship from runtime behavior so future work does not reinterpret
`waveMultiplier` or `warpSpeed` from their names.

## Read-only corpus evidence

The 48 JMV MPR files under the controlled map corpus contain 154 material
records: 48 type-1 land records and 106 type-3 textured-water records. All
type-3 records used by overlay tiles serialize `warpSpeed = 0`. The used wave
multipliers and liquid-tile counts are:

| `waveMultiplier` | material records | liquid tile uses |
| ---: | ---: | ---: |
| 0.2 | 28 | 18,738 |
| 0.3 | 6 | 543 |
| 0.4 | 6 | 5,228 |
| 0.5 | 27 | 106,530 |
| 0.6 | 20 | 38,016 |
| 0.8 | 11 | 12,522 |
| 1.0 | 8 | 55,264 |

The strongest water validation map is `zone18.mpr` (49,152 liquid tiles;
SHA-256 `0A8A750CBDF29C2DF4328A492DA810E8D77F6230D161FAA3E0326ACECBCA69E1`).
The strongest lava validation map is `zone16.mpr` (2,403 lava tiles;
SHA-256 `1E67B69121CF837D40B11618ADE9FB791CDFB4DB3546ACC6CB24D9F679B63811`).
The repository tool `scripts/particle_mpr_report.py` reads this evidence without
modifying the archive.

The paired `zone18.mob` world settings author wind direction approximately
`(-0.83205, 0.55470, 0)` and strength `0.8`, so the full-environment runtime
case exercises active deformation rather than merely compiling an inactive
shader branch. `scripts/particle_mob_report.py` reads those fields without
modifying the MOB.

Material type gates are exact: type 1 is land, type 2 is untextured water,
type 3 is textured water, and type 4 is grass. Only types 2 and 3 enter the
liquid-overlay deformation path. Animated atlas frames are a separate texture
animation system and must not be conflated with wave deformation.

## Runtime behavior contract

Let `s = clamp(windStrength, 0, 1)`, `m = waveMultiplier`, and one logical tick
be 55 ms. The active amplitude is:

```text
A = s * 0.3 world units * m
```

The authored wind vector is normalized. Its spatial scale is:

```text
K = (pi / 7) * (asin(1 - s) + pi / 1.999)
```

Wind phase starts at `2*pi` and advances once per logical tick by
`s * (-pi/21)`. The vertical displacement is:

```text
z += tableSin(dot(worldXY, normalizedWindXY * K)
              + windPhase * m + vertexNoise) * A * 0.25
```

For liquid overlays, horizontal warp is:

```text
x += tableSin(gridX * pi/4 + preciseLogicalTime * 0.05) * A * 3
y += tableSin(gridY * pi/4 + preciseLogicalTime * 0.05) * A * 3
```

`tableSin` quantizes to a 512-entry angular table. `waveMultiplier` therefore
affects both amplitude and phase. Although `warpSpeed` is serialized and is
retained in Endurance's vertex payload for diagnostics, the active runtime
motion uses the fixed `0.05` rate above; changing the serialized field must not
change deformation.

The noise field is a periodic 33 by 33 runtime field assembled from 40 circles
and seam-averaged on opposing edges. Its particular random realization is not
map-authored. Endurance uses a deterministic realization of the same topology
so editor previews, tests, and captures are reproducible.

## Endurance implementation invariants

- `CTile -> CSector::generateVertexDataFromTile -> CSector::drawWater` remains
  one indexed sector-batched water draw. Material parameters, grid coordinates,
  and noise are uploaded only when sector draw data is regenerated; there is no
  per-frame VBO rebuild.
- A dedicated terrain vertex shader prevents object, selection, and shadow
  programs from inheriting overlay attributes. Ordinary land draws explicitly
  disable and zero those attributes for desktop OpenGL and ANGLE state safety.
- Editing uses an explicit `u_overlayMotionEnabled = false` gate. This returns
  the exact base vertex before any formula is evaluated; setting time to zero
  is not a neutral state.
- Terrain and liquid picking use base geometry. The visible liquid is also base
  geometry whenever landscape editing is enabled, so depth picking and tile
  selection remain coincident during editing.
- The map-owned monotonic animation timer starts on successful MPR load and is
  invalidated on unload. Sector payloads are destroyed by landscape unload and
  the water pass resets its motion uniform after drawing.
- Non-water material types and invalid material indices carry an inactive gate.
  The former fragment-only UV drift was removed, so lava or other overlays are
  not accidentally animated merely because they use the water draw path.

## Validation contract

The focused Qt suite covers authored parameter propagation, type gating, wind
clamping, explicit neutral geometry, editing disable/resume, inactive
`warpSpeed`, periodic deterministic noise, and the common GLSL syntax required
by desktop OpenGL and ANGLE. Runtime cases are:

1. Load `zone18.mpr` with its environment MOB, enable water and animation, and
   verify animated textured water with no shader or GL error.
2. Enable landscape editing and verify every liquid vertex snaps exactly to
   base geometry; disable editing and verify motion resumes without rebuilding
   sector buffers.
3. Repeat the water-heavy case through ANGLE.
4. Load `zone16.mpr` and verify lava/non-water overlay visuals remain stationary
   and free of shader/index errors.
5. Unload and reload each map and verify phase starts from the new map lifetime.

## Integrated-main validation status

- Desktop OpenGL: user-confirmed animated Zone 18 water and stationary Zone 16
  lava on integrated main `2994805`.
- ANGLE comparison: explicitly waived for this release validation pass.
- Exact landscape-editing neutral/picking coincidence and unload/reload phase
  reset remain separate behavioral checks until explicitly confirmed.
