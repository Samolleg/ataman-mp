# Particle rendering status

Last updated: 2026-07-24

This document records which Evil Islands particle families EI Mapper currently
reconstructs from executable and resource evidence, which families only have a
recognizable editor preview, and what remains to be reverse engineered. Visual
similarity alone does not promote an effect to reconstructed status. Detailed
addresses, fingerprints, formulas, rejected hypotheses, and authored fixtures
are retained in
[`investigations/2026-07-23-particle-runtime.md`](investigations/2026-07-23-particle-runtime.md).

## Status definitions

- **Reconstructed and validated** means the relevant native factory,
  scheduler, callbacks, atlas, blend, authored controls, and representative
  map objects have been established and implemented. It does not promise
  identical random sequences because the editor intentionally uses a stable
  local ISAAC seed.
- **Partially reconstructed** means some native type-specific behavior is
  confirmed, but the complete per-type factory/resource path or independent
  visual controls are not yet closed.
- **Preview only** means the editor deliberately uses a recognizable texture
  or procedural style. Its motion, lifetime, scale, color, or blend must not be
  treated as native evidence.
- **Not implemented** means the type is preserved in MOB data but has no
  dedicated gameplay-effect preview.

## Reconstructed and validated

### 8193 / `0x2001` — Campfire

Campfire is the completed reference family. The editor implements the
fingerprinted 55 ms scheduler, six allocation attempts plus fallback,
500-active cap, free-list reuse, two-stage retirement, spawn and update
callbacks, atlas sequence and orientation, scale evolution, vertical/drift and
WORLD_SET wind motion, color interpolation, standard-alpha submission,
billboard conversion, authored scale, and interpolation between logical
quants. Fire and smoke are one emitter population.

The attached torch path is also reconstructed: a torch makes one Campfire
emitter, prewarms it for 60 quants, places both particle and point light at
world position plus unrotated `TORCH_PTLINK`, scales the emitter by authored
`TORCH_STRENGHT`, plays authored `TORCH_SOUND`, and creates a neutral
`(0.8,0.8,0.8)` light with radius `strength * 10`. Complexion is not an input.

The associated terrain lighting work is complete for ordinary lights:
quadratic radial falloff, the native `1 + min(N dot L, 0)` terrain normal
factor, channelwise-maximum ambient/sun/local composition, generic-light
runtime Z lift, fixed projected-shadow opacity, and a 64-light preview capacity
covering the selected JMV corpus.

## Partially reconstructed Tendri types

| ID | Name | Confirmed | Remaining before native-equivalent status |
|---:|---|---|---|
| 8195 / `0x2003` | Fire | Uses the Tendri update path; particles terminate instead of entering smoke after the fire frames. | Independently pin its full constructor, texture selection, scheduler fields, blend state, and authored controls; add a fingerprinted placed control. |
| 8196 / `0x2004` | Smoke | Tendri spawn starts at atlas frame 8 and uses the shared later-frame update behavior. | Independently pin construction, texture/tint/scale, cadence, and a placed control. Current editor style is not claimed exact. |
| 8197 / `0x2005` | Vulcansmoke | The symbolic type and current Tendri-based editor presentation are known. | Establish its exact factory branch, starting frame, callbacks, texture, blend, and scale. The current smoke-only classification remains provisional. |
| internal `0x2010` | special Tendri wrapper | Has separate count/lifetime fields, a special spawn wrapper, world-height adjustment, and fire-only termination. | Identify the public effect/owner that constructs it and document its resource and attachment path. It must not be generalized to ordinary Campfire. |

## Preview-only families

These IDs currently receive a recognizable editor style, but their native
runtime behavior has not been reconstructed:

| IDs | Names |
|---|---|
| 8192, 8194, 8208, 8209, 8231, 8257 | Fireball, Fireblast, Firewall, Firearrow, Castingfire, Firestar |
| 8198, 8263 | Healing, Regeneration |
| 8199 | Poisonfog |
| 8200, 8201 | Aggressionfog, Geyser |
| 8202 | Tornado |
| 8210, 8213, 8258 | Acidray, Sphereacid, Acidstar |
| 8214, 8240, 8259 | Sphereelectricity, Lightningblast, Sparks |
| 8217, 8253, 8254 | Teleport, Portalstar, Portal |
| 8203–8205, 8232–8239 | Casting, Nuke, Bansheecasting, and the elemental/divination/illusion/domination/enchantment/healing/failed casting variants |

Texture names used by these previews are limited to direct resource matches
where available. Procedural soft sprites are intentional placeholders rather
than borrowed unrelated assets.

## Named types without a dedicated gameplay preview

The parser preserves all of these type IDs and their authored scale, but they
still require both native reverse engineering and a dedicated renderer:

| IDs | Names |
|---|---|
| 8206–8207 | Mushroom, Blood1 |
| 8211–8212 | Bluegas, Link |
| 8215–8216 | Spherefire, Clayring |
| 8218 | Antimagic |
| 8219–8230 | Modifier1 through Modifier12 |
| 8241–8247 | Blood2, Blood3, Blood4, A4bloodred, A4bloodgreen, A4bloodblue, A4bloodblack |
| 8248–8252 | Zoneexit, Path, Pathdestination, Pathfailed, Moshka |
| 8255–8256 | Cylinder1, Cylinder2 |
| 8260–8262, 8268 | Visionstar1 through Visionstar4 |
| 8264–8267 | Silence, Feeblemind, Feetcloud1, Feetcloud2 |
| 8269–8274 | Ballofstars, Rickarrow, Cursestars, Curseholder, Starttrans, Transform |

Some of these effects may be transient spell/script effects rather than
ordinary placed `PARTICL` records. Their creation owner and attachment rules
must be established instead of assuming the standalone-source path.

## Torch corpus audit

A read-only scan with `scripts/particle_mob_report.py` covered all 153 MOB
files in the selected Jamais Vu 1.35 Maps directory:

- 171 torch records occur in 30 MOB files;
- they cover seven object mesh templates, eight authored strength values,
  29 authored PTLINK vectors, and four sound values;
- none contains `PARTICL_TYPE` or any other particle field;
- the complete torch field set varies mesh, texture, transform, complexion,
  link, strength, and sound data only.

The fingerprinted native integration at `0x592968..0x592983` passes constant
type `0x2001` to every torch particle setup call. No alternate torch code path
or authored type selector was found. Therefore all audited torch meshes use
Campfire; `stto1`, `stto2`, `stto5`, `stto8`, `stto9`, `stto10`, and `stto11`
do not select different particle templates.

## Authored versus hardcoded boundary

Standalone `PARTICL` records author position, type ID, scale, name/comment, and
ID. Torch records author mesh/object data, PTLINK, strength, and sound, while
their Campfire type, neutral light color, and radius multiplier are hardcoded.
The native particle factory selects behavior, callbacks, textures, atlas,
blend, and capacity in executable code; no external editable particle-template
record has been found.

Existing effects can therefore be selected and scaled from MOB data. A global
visual replacement can safely be packaged by overriding the referenced MMP in
a mod-owned resource archive. A distinct new behavioral template or a second
colored fire alongside ordinary fire requires a new/repurposed type ID plus an
executable/addon factory extension and matching editor support. A stock torch
cannot select that new type without extending its hardcoded setup path.

## Remaining reverse-engineering checklist

For each incomplete family, do not replace its preview approximation until all
applicable items are evidenced:

1. Identify every factory branch and the object/script/spell path that requests
   the type.
2. Fingerprint constructor fields, callbacks, scheduler cadence, allocation
   cap, free-list behavior, lifetime, and prewarming.
3. Resolve texture/resource names, atlas rectangles, orientation, frame order,
   and per-frame delays.
4. Establish spawn volume, motion, collision/world-height behavior, scale,
   rotation, color/alpha interpolation, and wind response.
5. Establish blend and render state, draw order, billboard/orientation rules,
   clipping, and lighting interaction.
6. Dump at least one complete authored or runtime-owner control and add stable
   executable/resource fixtures.
7. Implement the family independently; do not reuse Tendri rules merely
   because an effect looks smoky, fiery, or sprite-based.
8. Validate identical controls under desktop OpenGL and ANGLE, while keeping
   selection markers separate from gameplay particles.

Lava in `gz10g` is not on this list: it is confirmed MPR liquid/terrain data,
not a particle emitter.
