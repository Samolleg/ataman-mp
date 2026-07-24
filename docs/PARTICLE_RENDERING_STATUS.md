# Particle rendering status

Last updated: 2026-07-25

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

## Reconstructed weather previews

| ID | Name | Implemented | Remaining control |
|---:|---|---|---|
| 8202 / `0x200A` | Tornado | Dedicated persistent six-source spiral, native phase partitions, spawn/lifetime/rotation/contraction/height/alpha rules, sunlight tint, `Tornado` atlas, and ordinary alpha blending. | Capture a moving native carrier to validate source-spine bending; the standalone catalog control correctly exercises the stationary path. |
| 8240 / `0x2030` | Lightningblast | Dedicated finite ten-source burst with source flashes, ground clouds, short bolts, native phase/lifetime/gravity/generation rules, `TendriLight` atlas, and additive blending. The editor repeats the completed burst every 80 quants solely for inspection. | Capture a native spell owner to validate owner-supplied size and destination; the confirmed standalone visual path is implemented. |

## Ordered reconstructed previews

| ID | Name | Implemented | Remaining control |
|---:|---|---|---|
| 8203 / `0x200B` | Casting | Dedicated three-source orbiting and rising tails followed by the closing ring, native phase cadence, `FireSmoke` atlas, and ordinary alpha blending. The standalone catalog uses an explicit synthetic carrier bound equal to authored scale and repeats the finite sequence every 72 quants. | Capture attached carriers with different bounding volumes; the standalone bound is a documented editor control, not a native ownerless default. |
| 8204 / `0x200C` | Nuke | Dedicated finite 13-quant generator with the native shrinking attempt count, cylindrical spawn volume, vertical attenuation and gravity, half-scale impact plane, radial impact motion, phase progression, expansion and alpha decay, using the `AcidRay` atlas and ordinary alpha blending. The editor repeats the completed burst every 80 quants. | Capture a native placed/spell control as a visual validation case; the stationary callback path itself is closed. |
| 8205 / `0x200D` | Bansheecasting | Dedicated eight-quant elevated closing ring with native short-lived phase-advancing sprites, `Banshee` atlas, and ordinary alpha blending. The standalone catalog uses an explicit synthetic carrier bound equal to authored scale and repeats the finite sequence every 32 quants. | Capture attached carriers with different bounding volumes; the standalone bound is a documented editor control, not a native ownerless default. |
| 8206 / `0x200E` | Mushroom | Dedicated finite 30-quant rising and logarithmically expanding source, native 16–19-quant particle lives, phase/scale/alpha/velocity rules, `ChangeHero` atlas, and ordinary alpha blending. The completed cloud repeats every 64 quants for catalog inspection. | Validate the type-specific one-shot auxiliary dispatch separately; it is not part of the billboard population. |
| 8207 / `0x200F` | Blood1 | Dedicated six-quant phase-12 directional burst with native velocity/scale/gravity and alpha-table updates, using `RainSnow` and ordinary alpha blending. The catalog uses authored scale as an explicit synthetic carrier bound and repeats every 32 quants. | Capture attached carriers with different bounds; a standalone record does not author the carrier-size input. |
| 8208 / `0x2010` | Firewall | Confirmed zero-wall-vector standalone path through the TendriSmoke spawn/update family, restricted to fire phases with ordinary alpha blending. | Capture a spell-owned nonzero wall vector to validate elongated placement and terrain snapping along the wall. |
| 8209 / `0x2011` | Firearrow | Dedicated owner-activated FireArrowSmoke head/trail callback replay with native phase, lifetime, class, scale, and ordinary-alpha rules. The standalone catalog deliberately uses zero owner speed, so its trail remains collapsed at the source. | Capture a moving projectile owner to supply and validate native velocity plus the two perpendicular trail basis vectors. |
| 8210 / `0x2012` | Acidray | Dedicated continuous stationary path with native one-particle-per-quant creation, 15-quant phase/lifetime rules, and exact width/size tables, using `AcidRay` and ordinary alpha blending. | Capture a moving owner only for the transverse ray geometry; the placed zero-speed path is closed. |
| 8211 / `0x2013` | Bluegas | Dedicated carrier-cylinder cloud with native orbit, rise acceleration, growth, phase cadence, final alpha decay, `Stench` Base-fallback atlas, and ordinary alpha blending. The catalog uses authored scale as an explicit synthetic carrier bound. | Capture attached carriers with different bounding volumes; no ownerless native default exists. |
| 8212 / `0x2014` | Link | Dedicated ten-attempt directional stream using the native default ten-unit wall vector, rotating cross-section, parabolic arc, endpoint alpha fades, phase cadence, `Link` atlas, and ordinary alpha blending. | Capture an effect-owned non-default destination vector; the bare placed-record constructor path is closed. |
| 8213 / `0x2015` | Sphereacid | Dedicated rotating carrier shell with native population, lifetime/phase cadence, size table and rare size class, using `ProtectionA` and additive blending. The catalog uses authored scale as an explicit synthetic carrier bound. | Capture attached carriers with different bounding volumes and motion; the stationary synthetic-bound control is closed. |
| 8214 / `0x2016` | Sphereelectricity | Dedicated rotating carrier shell with the electricity-specific rare size class, native population, lifetime/phase cadence and size table, using `ProtectionE` and additive blending. The catalog uses authored scale as an explicit synthetic carrier bound. | Capture attached carriers with different bounding volumes and motion; the stationary synthetic-bound control is closed. |
| 8215 / `0x2017` | Spherefire | Dedicated rotating carrier shell with the fire-specific 2.0 size factor, native population, lifetime/phase cadence and size table, using `ProtectionF` and additive blending. The catalog uses authored scale as an explicit synthetic carrier bound. | Capture attached carriers with different bounding volumes and motion; the stationary synthetic-bound control is closed. |
| 8216 / `0x2018` | Clayring | Dedicated finite ten-source expanding/rotating ground ring with native anchor/trail populations, exact radius/alpha tables, phase/scale/offset decay, `ClayRing` atlas and additive blending. The editor repeats the completed one-shot for catalog inspection. | Validate non-flat terrain snapping and a negative authored-scale control; the positive flat catalog path is closed. |
| 8217 / `0x2019` | Teleport | Dedicated finite signed radial column with native radial distribution, dual phase bands, vertical/alpha evolution, population limits and `Teleport` atlas using ordinary alpha blending. The editor repeats the completed one-shot for catalog inspection. | Capture a negative authored-scale game control; both signed callback paths are implemented from binary evidence. |
| 8218 / `0x201A` | Antimagic | Dedicated short-lived rotating carrier shell with native 24-attempt cadence, four-quant life, six-entry scale table, rare size class, `AntiMagic` atlas and additive blending. The catalog uses authored scale as an explicit synthetic carrier bound. | Capture an attached carrier with a native bounding volume; the stationary synthetic-bound control is closed. |
| 8219–8230 / `0x201B`–`0x2026` | Modifier1–Modifier12 | Dedicated finite carrier-source family with subtype phases/axes, signed contraction or expansion, anchor and trail populations, exact alpha/scale evolution, `Stun` atlas and additive blending. The catalog uses authored scale and identity orientation as an explicit synthetic carrier. | Capture representative attached carriers to validate their actual bounds and direction; the complete identity-carrier controls are implemented. |
| 8241–8243 / `0x2031`–`0x2033` | Blood2–Blood4 | Shared native Blood burst with subtype-specific RainSnow phases 13–15, finite source, motion/scale damping, gravity, alpha table and ordinary blending. The catalog uses authored scale as an explicit synthetic carrier bound. | Capture attached carriers with different bounds; the standalone phase-specialized controls are closed. |
| 8244–8247 / `0x2034`–`0x2037` | A4bloodred/green/blue/black | Dedicated one-quant multi-source radial splatter with exact random ranges, gravity/terminal velocity, ground-zero retirement, phase-12 RainSnow atlas and additive blending. Bare records correctly remain the same white burst because color is supplied externally by gameplay owners. | Capture an owner-colored gameplay control for each external tint; the standalone factory path is closed. |
| 8248 / `0x2038` | Zoneexit | Dedicated continuous rising phase-row column with the native fixed 25-birth cadence, lifetime/alpha sequence, size distribution, `ZoneExit` atlas and ordinary blending. Bare records retain the native zero planar basis; spread requires external owner parameters. | Capture an owner-configured exit zone to validate nonzero planar basis and wall-direction tint; the bare record path is closed. |
| 8249 / `0x2039` | Path | Dedicated finite green phase-15 path point using the native lifetime brightness sequence and `ShapeChange` atlas. A bare stationary record cannot provide the predicted carrier positions required for a trail. | Validate on a moving gameplay carrier; the truthful stationary standalone control is closed. |
| 8250–8251 / `0x203A`–`0x203B` | Pathdestination, Pathfailed | Dedicated finite green ring and red ring-plus-cross controls with exact point counts, offsets, alpha/scale evolution, `ShapeChange` atlas and ordinary blending. | Validate bridge-plane projection on non-flat terrain; the flat standalone controls are closed. |
| 8252 / `0x203C` | Moshka | Dedicated carrier-swarm simulator with 80 anchors, native source resets and random-ball motion, short gradient trails, phase/alpha/size tables, `Bag` atlas and ordinary blending. The catalog uses authored origin/scale as an explicit synthetic stationary carrier. | Capture an attached moving carrier and detach behavior; the stationary synthetic-carrier control is closed. |
| 8253 / `0x203D` | Portalstar | Dedicated persistent phase-zero `Casting6` star with the native per-quant random scale range and ordinary alpha blending. | Validate a gameplay-owned portal-star carrier; the standalone visual path is closed. |
| 8254 / `0x203E` | Portal | Dedicated additive `Teleport` column with the persistent elevated head, dual phase-band rotating trails, radial birth distribution, accelerating rise, height termination, scale and alpha evolution, and native population cap. | Validate owner-driven size below one, which intentionally suppresses the elevated head; the normal standalone control is closed. |
| 8255–8256 / `0x203F`–`0x2040` | Cylinder1, Cylinder2 | Dedicated finite counter-rotating two-source helices with native anchor/trail cadence, source growth, tangent motion, scale/alpha evolution, and distinct `Casting2`/`Casting1` atlases using ordinary alpha blending. The editor repeats the completed one-shot for catalog inspection. | Validate gameplay-owned carrier bounds and orientation; the stationary synthetic-bound controls are closed. |
| 8257–8258 / `0x2041`–`0x2042` | Firestar, Acidstar | Dedicated three-quant phase-15 flash with exact two visible sizes, ordinary blending, and the type-specific `FireArrowSmoke` or `AcidRay` atlas. The catalog uses authored origin/size as a synthetic carrier and repeats the finite one-shot. | Validate gameplay body-part attachment; the stationary synthetic-carrier controls are closed. |
| 8259 / `0x2043` | Sparks | Dedicated additive `Casting6` source flash plus nine radial trails with native random ranges, phase selection, lifetime alpha table, motion, size and velocity damping. The catalog uses authored origin/size as a synthetic carrier and repeats the finite burst. | Validate gameplay body-part attachment and owner motion; the stationary synthetic-carrier control is closed. |
| 8260–8262, 8268 / `0x2044`–`0x2046`, `0x204C` | Visionstar1–Visionstar4 | Dedicated persistent additive `Stun` stars with subtype phases, one-point/pentagon populations, pentagon rotation, Visionstar4 elevation, and native random size smoothing. The catalog uses authored origin/size as a synthetic stationary carrier. | Validate above-head attachment and detach fade on gameplay carriers; the persistent synthetic-carrier controls are closed. |

## Preview-only families

These IDs currently receive a recognizable editor style, but their native
runtime behavior has not been reconstructed:

| IDs | Names |
|---|---|
| 8192, 8194, 8231 | Fireball, Fireblast, Castingfire |
| 8198, 8263 | Healing, Regeneration |
| 8199 | Poisonfog |
| 8200, 8201 | Aggressionfog, Geyser |
| 8232–8239 | Elemental, divination, illusion, domination, enchantment, healing, and failed casting variants |

Texture names used by these previews are limited to direct resource matches
where available. Procedural soft sprites are intentional placeholders rather
than borrowed unrelated assets.

## Named types without a dedicated gameplay preview

The parser preserves all of these type IDs and their authored scale, but they
still require both native reverse engineering and a dedicated renderer:

| IDs | Names |
|---|---|
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
## All-type editor test scene

The repeatable generator `scripts/build_particle_catalog_mob.ps1` builds a
minimal supplemental MOB with one scale-1 standalone record for each of the 83
public particle IDs. The default 10-by-9 layout uses 8-world-unit centers
(4 terrain tiles), sequential names and IDs, and an adjacent JSON manifest.

The current generated artifact is
`artifacts/particle-catalog-grid/all-particles-grid.mob`. It is intended for
loading by itself over EI Mapper's neutral grid or alongside a suitable MPR.
Large effects can be regenerated with wider spacing. Standalone presence in
this catalog does not replace attached or transient native controls for types
whose real owner supplies additional parameters.

The portable one-click control is
`artifacts/particle-catalog-collection/collections/particle-catalog.collection`.
It loads the same 83-type MOB over a deterministic, flat, 3-by-3-sector synthetic
MPR. The MPR uses the `zone1` internal namespace and atlas slot 1, so the active
Jamais Vu Zone 1 atlas supplies the common-ground texture; no texture imagery is
embedded in the fixture. The grid spans `(12, 16, 0)` through `(84, 80, 0)`,
centering it at `(48, 48)` inside the 96-by-96-world-unit plane.
