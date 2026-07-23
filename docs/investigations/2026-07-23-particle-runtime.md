# Evil Islands particle runtime evidence

Last updated: 2026-07-24

## Purpose and method

This is the persistent evidence ledger for the EI particle-rendering work. Update it whenever a binary or resource finding is established, before relying on that finding in renderer code. Conversation history and visual inference are not authoritative.

The investigation follows the Evil Islands modding rule: when documentation is missing or conflicts, reverse engineer the read-only game executable and resource corpus instead of guessing. Game resources, maps, MOBs, and databases are inputs only and must never be modified during investigation.

## Reference build

- Executable: `D:\Evil Islands\Starter\Engine\game.exe`
- Size: 3,825,725 bytes
- SHA-256: `BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2`
- Principal temporary analysis script: `C:\Users\ilufu\Downloads\ПЗ Жамевю\Codex\work\particle-reverse-tools\disasm_particle.py`
- Latest temporary disassembly report at time of writing: `particle-disassembly-10.txt`
- Repository-local byte evidence report: `scripts/particle_binary_report.py`.
  Its stable fixture rejects executable fingerprint drift and pins the
  scheduler vtable slot, callback sites, blend setup and switch, logical-quant
  loop, global render-time particle lookup, callback ranges, and exact
  constants without modifying the executable.

All virtual addresses below apply only to this fingerprint.

## Confirmed authored objects

- `CParticleSource` parsing is in `source/objects/particle.cpp` and `.h`; it retains the authored particle kind/type and scale.
- `CTorch` parsing is in `source/objects/torch.cpp` and `.h`; a torch is an ordinary object mesh with a particle attachment point, strength, and sound. Its visible object surfaces are not particle sprites.
- The current resource-name mapping includes:
  - `8193` — Campfire
  - `8196` — Smoke
  - `8197` — Vulcansmoke
- `gz1g` maps to the installed JMV `zone1.mob`; `gz10g` maps to
  `zone10.mob` plus `zone10.mpr`. These mappings are established by the map
  catalog rather than inferred from filenames.

### Fingerprinted `gz1g` records

The read-only source `D:\Evil Islands\Starter\MODS\JamaisVu135\Maps\zone1.mob`
is 1,586,436 bytes with SHA-256
`FEFFDA09F7279F65562DB37BBDCDFC475D59C4EE6FF1678958E5117949FB9E5A`.
It contains seven standalone light records, four torch records, and eight
particle-source records. The complete records are reproducibly emitted by
`scripts/particle_mob_report.py`.

The seven `LIGHT` records have IDs 334, 335, 336, 337, 347, 348, and 349.
Every one stores authored Range `10.0`, Color `(1.0, 1.0, 1.0)`, and
`LIGHT_SHADOW = 1`; their exact positions, names, comments, offsets, raw bytes,
and record hashes are retained in the generated fixture. Thus the editor's
bright night fields are not caused by treating byte RGB as normalized floats:
these records really do author normalized white and radius ten.

Representative torch NID 1365 is at file offset 619,105, is 310 bytes, and has
record SHA-256
`D7450142441196107260624C61A4119B55D7FB11D443E7F9DE14FC13B4EDED34`.
Its complete decoded authored fields include:

- name `Camp fire 00-1365`, object type 58, mesh/template `stto5`, primary
  texture `Campfire00`, parent template `Camp fire 00`;
- world position `(133.5677947998047, 151.08291625976562,
  -0.05000000074505806)`, identity quaternion, complexion `(0.5, 0.5, 0.5)`;
- local `TORCH_PTLINK` `(0.10000000149011612, 0, 0.5)`, strength
  `0.4000000059604645`, sound `nature\fire\big.wav`;
- player 0, parent ID 1168, use-in-script 0, shadow 1, empty quest info and
  body-part list.

Representative independent particle source ID 357 is at offset 622,543, is
104 bytes, and has record SHA-256
`E2FFEEB487E2AA3CB51EFE2C1FCBD8CBDD19D44B9A5C290AAE005348167D7FD5`.
It is named `Particles source-357`, kind `0x2001`/Campfire, scale
`0.4000000059604645`, position `(178.94630432128906, 131.87026977539062,
8.293479919433594)`, and comment `none`.

No standalone particle source is co-located with any of the four torches. The
nearest source to torch 1365 is about 49.98 world units away; the nearest to
torch 331 is about 13.33 units away. Therefore the torch's attached visual
cannot be attributed to an overlapping authored `PARTICL` record.

The routine at `0x40EB60..0x40ECA5` performs one lookup for particle type
`0x2001`, but its confirmed caller at `0x4575C1` invokes it on the global
object at `0x79B2A8` under a render flag. It is therefore a global render-time
path, not a proven `CTorch` construction or attachment path. It cannot
establish the number of emitters used by a torch. The earlier single-emitter
interpretation of this routine is rejected.

The actual client-side torch class is fingerprinted by
`CTorchObjectClientSpecific` at `0x78F7BC`, returned by vtable method
`0x52D190`. The complete authored-to-runtime argument chain is now confirmed:

1. Native MOB tag dispatch at `0x46B3C0` recognizes
   `TORCH_STRENGHT`/`0xBBBF0001`, `TORCH_PTLINK`/`0xBBBF0002`, and
   `TORCH_SOUND`/`0xBBBF0003`. It stores the decoded float at runtime torch
   record `+0x8C`, the decoded vector at `+0x90..+0x98`, and the string at
   `+0x9C` respectively (`0x46B40C..0x46B48A`). These are the exact fields
   reported above for NID 1365.
2. Torch object integration at `0x592968..0x592983` invokes client vtable
   slot `+0x40` with arguments `(0x2001, &torchRecord[+0x90],
   torchRecord[+0x8C])`. Thus the built-in torch particle kind is hardcoded
   Campfire `0x2001`, its local attachment is authored `TORCH_PTLINK`, and its
   emitter scale is the authored `TORCH_STRENGHT`; no database/template lookup
   supplies either kind or scale on this path. For NID 1365 the exact scale is
   therefore binary32 `0.4000000059604645`.
3. That slot resolves to `CTorchObjectClientSpecific::setup` at `0x52D330`.
   Common helper `0x52C590` snapshots the arguments into client fields
   `+0x20`, `+0x40..+0x48`, and `+0x28`. Setup clears the prior effect held in
   the single pointer at object `+0x5C`, calls particle factory `0x498400`
   exactly once with the received `0x2001`, places the result at the authored
   attachment, applies the received strength through scale mutator `0x485480`,
   and attaches it to the torch object.
4. The immediately following vtable `+0x44` and `+0x48` calls are separate
   light and sound setup paths (`0x52D410` and `0x52D520`); they do not allocate
   more particle emitters. The sound call receives authored record `+0x9C`,
   which for NID 1365 is `nature\fire\big.wav`.

The light setup path is now decoded as well. At `0x592986..0x5929C2` the
torch integration constructs the constant linear RGBA value
`(0.8, 0.8, 0.8, 1.0)`, multiplies authored `TORCH_STRENGHT` by the binary32
constant `10.0` at VA `0x73BAAC`, and calls vtable slot `+0x44` with
`(&TORCH_PTLINK, strength * 10, &color)`. The slot resolves to
`0x52D410..0x52D517`. It creates exactly one point light of native type
`0x31`, sets its position to the torch client object's world position plus
the authored PTLINK, stores the received radius at light `+0xE0`, its square
at `+0x100`, and `1 / radius^2` at `+0x104`, then registers the light. The
particle setup at `0x52D34D..0x52D370` performs the identical component-wise
world-position-plus-PTLINK addition.

The underlying native D3D-light constructor at `0x68CEF0..0x68CF9F`
initializes D3D `Range` at `+0xE0`, `Falloff` at `+0xE4`, and attenuation
coefficients at `+0xE8..+0xF0` to `(1, 0, 0)`. Torch setup replaces Range and
caches its square and reciprocal square at `+0x100/+0x104`. The earlier
conclusion that these D3D fields prove constant final surface illumination is
rejected: the engine's higher-level software lighting path uses the cached
reciprocal square before vertex colors reach the backend.

The earlier identification of `0x68D450..0x68D590` as the terrain point-light
application is rejected. That helper does have squared radial falloff, but the
fingerprinted terrain caller does not invoke it. Terrain cell relighting at
`0x6C3A00..0x6C3C7B` first invokes the global-light path `0x6C36B0`, then the
cell-local-light path `0x6C31C0`; the latter calls the distinct point-light
helper `0x68D5A0..0x68D727`.

The actual terrain helper computes the exact radial factor
`1 - distanceSquared * light[+0x104]` and returns the input base color outside
the radius. Inside it computes the normalized surface-normal/light-direction
dot product and uses `1 + min(dot, 0)`: front-facing and perpendicular
surfaces receive full radial strength, while only back-facing surfaces are
reduced. It then multiplies that factor by the caller's scalar and stored RGB,
adds it to the per-vertex base color, and clamps each channel at `1.0`. Since
`+0x104 = 1 / radiusSquared`, native terrain falloff is
`max(0, 1 - distanceSquared / radiusSquared)`. The editor's positive Lambert
term `max(dot, 0)` is rejected. In particular, a torch light authored directly
on flat ground has dot approximately zero and therefore full native strength,
not the editor's nearly zero strength.

Registration at `0x6CA5D0` and cell insertion at `0x6CAC10` place each light
only in the spatial cells intersected by its radius. Terrain local lighting
traverses that cell-local collection. Each ordinary light is evaluated from
the same per-vertex base color, and `0x6C3431..0x6C35DD` takes the
channel-by-channel maximum of the current result and the candidate result.
Local lights therefore do not add to each other. This rejects both additive
overlap and the editor's global nearest-to-camera selection as native rules;
native eligibility is local to rendered geometry and overlapping candidates
combine by maximum.

A deterministic read-only scan of all 153 MOB files in the selected Jamais
Vu 1.35 Maps directory finds at most 34 authored lights plus torches in one
MOB (`zone10.mob`: 34 lights). The reported `zone3obr.mob` has 20. A preview
uniform capacity of 64 therefore contains every light in each fingerprinted
map corpus member; unlike the former nearest-eight list, nonintersecting
lights naturally contribute zero through their native radius test.

The global terrain path has the same non-additive structure. Directional
helper `0x68D310..0x68D443` adds only the positive
`dot(normal, -lightDirection)` directional RGB to the per-vertex base. The
caller at `0x6C3965..0x6C39EF` then takes the channel-by-channel maximum of
that candidate and the world ambient RGB. The faithful unbaked shader
equivalent is thus `max(ambient, sun * diffuse)`, not
`ambient + sun * diffuse`. The cell-local pass subsequently takes the maximum
of this global result and each point-light candidate. This explains the
controlled observations without visual tuning: late-night terrain is darker,
low-power torch halos remain visible on flat ground, and overlapping lights
do not make additive saturated fields.

Standalone `LIGHT` construction at `0x511830..0x51194C` copies authored Range
and Color unchanged, but adds the exact binary32 constant `2.0` at VA
`0x73DBE8` to the authored Z position before registering the runtime light.
This offset belongs to generic `LIGHT` records only. Torch lights use the
unrotated world-position-plus-PTLINK position already established above and
receive no additional two-unit offset.

Neither setup multiplies PTLINK by the torch rotation. This rejects the
editor's previous `rotation.rotatedVector(PTLINK)` attachment rule. It also
establishes that particle and generated light must share one unrotated native
attachment position. The native routines add no extra vertical pivot or
hidden particle-only offset; any remaining apparent source-height mismatch
must be sought in the object/display-position path or billboard presentation,
not corrected with an unsupported emitter offset.

### Fingerprinted zero-height torch-light controls

The reported missing-light controls all author a zero local PTLINK and retain
ordinary nonzero strength. In the read-only Jamais Vu resources:

- `zone3obr.mob`, SHA-256
  `F3F15E3A5DADD1ED52966DB94EFAB32D5E1780C2501FABC4C6B47E52CC3C7DC5`,
  contains 20 torches and no generic `LIGHT` record. NIDs 166, 167, and 449
  each author strength `0.5` and PTLINK `(0, 0, 0)`.
- `zone11.mob`, size 3,138,272 bytes, contains two torches and no generic
  `LIGHT` or standalone particle record. NID 3880 authors strength `0.4` and
  PTLINK `(0, 0, 0)`.
- `zone5_1.mob` (catalog map `gz5g`), SHA-256
  `73A9154818A3F27B4A03543845A7154E86B342D6FE62E3E83607150498900D97`,
  contains two strength-`0.5`, PTLINK-zero torches plus six generic lights.

The native torch setup still adds no hidden Z offset in any of these cases.
Their game-visible halos are instead explained by the confirmed terrain
normal rule above. Increasing authored strength to make the editor's positive
Lambert implementation visible was compensating for the wrong normal factor
and is not a native strength rule.

`OBJ_COMPLECTION` is not an input to this particle path. Setup
`0x52D330..0x52D3E4` receives and forwards only the hardcoded particle kind,
authored attachment, and authored strength; it neither reads the torch's
complexion nor passes one to the factory or scale mutator. The spawn callback
also initializes every Tendri particle color to opaque white. A controlled
runtime edit reported for zone18 torch 1182, changing the tested complexion
field to 2, independently left both mesh and particle appearance unchanged in
the game and editor. Complexion is therefore rejected as the cause of the
torch and standalone-particle size discrepancy.

The class contains no second particle-effect pointer or second factory call in
this path. Method `0x52D3F0` then invokes that emitter's vtable slot `+0x18`
exactly 60 times, prewarming the effect before its first ordinary render. This
class-specific evidence, rather than the rejected global lookup
interpretation, establishes that a torch's core fire, peripheral flame, and
smoke atlas stages are one emitter population.

### Light and projected-shadow composition

Stock projected shadows are composited silhouettes with union opacity
`80 / 255`, rather than removal of only the direct-sun term. A fully covered
receiver consequently retains visibility `175 / 255` (approximately
`0.6862745`) regardless of the current weather RGB. Local map lights are
combined after that environment-shadow composition. The editor expression
`ambient + sun * diffuse * shadow` is rejected because it makes visible shadow
contrast depend on the sun/ambient ratio and can erase shadows when the
unshadowed noon environment saturates. Faithful shader composition must first
form the channelwise-maximum global environment illumination, apply the
bounded projected-shadow visibility to that result, and only then take the
channelwise maximum with point-light candidates.
The shadow-map comparison itself therefore represents geometric coverage:
fully occluded is `0`, fully visible is `1`, and filtered edge samples average
between them. Applying the `80 / 255` silhouette opacity happens exactly once
after coverage filtering. The former shader value `0.58` inside each occluded
PCF sample and then applying a second outer opacity is rejected; it double
encoded shadow transparency and limited full occlusion to about `0.868`.

### Fingerprinted zone18 strength control

The visually closer torch NID 1182 supplies a controlled authored-data case,
not a rule inferred from its appearance. Its read-only source
`D:\Evil Islands\Starter\MODS\JamaisVu135\Maps\zone18.mob` is 4,498,380
bytes with SHA-256
`0B3E1115FEA2C892B597D969AC8AB60965F88D9083398AF09C0E84581F279A90`.
The complete fixture is emitted by `scripts/particle_mob_report.py` and stored
as `tests/fixtures/particle_runtime/zone18-torch-1182.json`.

Torch NID 1182 is at offset 2,004,158, is 308 bytes, and has record SHA-256
`8EB2E0F6B5E57BABD48BB57C354CE4DA7376D9DB1BBC3B00AECBBAD36F19BBD2`.
Its decoded fields include template `stto5`, primary texture `campfire00`,
attachment `(0, 0, 1)`, strength `1.0`, sound
`nature\fire\small.wav`, and position
`(131.2783660888672, 245.04629516601562, -0.03333371877670288)`. The same
MOB's world settings have record SHA-256
`1101AA43304CBAAF3974E785F11A42FED6E1FB92997D488DE80816781D49D625`,
wind direction `(-0.8320502638816833, 0.5547001957893372, 0)`, and wind
strength `0.800000011920929`.

This provides an exact contrast with gz1g NID 1365, whose authored strength is
binary32 `0.4000000059604645`. Both reach the same hardcoded Campfire factory
and the same scale mutator. Consequently the difference is not an
emitter-family or scheduler choice: every field multiplied by the scale
mutator must use the respective authored strength.

### Fingerprinted `gz10g` lava records

The read-only `zone10.mob` is 568,201 bytes with SHA-256
`6C2FF44B72CE9850B4BBFF9759A00D17BC7DA3DDCC9CC7E124138110D31B78B2`.
It contains zero `TORCH` and zero `PARTICL` records. This rejects the hypothesis
that the representative lava surface is a MOB particle source.

Lava is authored in `zone10.mpr`, 544,623 bytes, SHA-256
`56F0A601330D88912EFCC17EF58A2EDB5168640D58E965CC040FC30E9076B925`.
The embedded `zone10.mp` declares 512 tile entries, with indices 5, 6, and 7
typed as lava (tile type 13), and no animated-tile records. Ten of 48 sectors
contain liquid; together they contain 637 lava tiles, all using liquid material
1. Material 1 is terrain type 3 with RGBA
`(0.7960785031318665, 0, 0, 1)`, self-illumination 1, wave multiplier 0.5,
warp speed 0, and zero reserved fields. Thus this representative `gz10g` lava
belongs to the MPR liquid/terrain renderer, not the particle emitter renderer.
`scripts/particle_mpr_report.py` emits every tile record and a compact stable
fixture form.

## TendriSmoke factory and layout

The string `TendriSmoke\0` is pinned at VA `0x78AEA4` as bytes
`54 65 6E 64 72 69 53 6D 6F 6B 65 00`. Its factory path reaches the constructor
at `0x485B72`. Nearby registered particle families include Banshee, Tornado,
Fireworks, PoisonCloud, Healing, Vulkan, Explosion, Stench, Link, and AcidRay.

The read-only Jamais Vu 1.35 `config.ini` fixture is 2,627 bytes with SHA-256
`4F623E975DA1BEE8170B9E67EECE60604D5BF24E111FBB7486B63FF1AAC636DA`.
Its final `textures.res` mapping is exactly `textures.res=res\\textures.res`.
That resolved archive is the previously fingerprinted Jamais Vu `textures.res`
(SHA-256
`2E8CA42FB75FAC12E5659C563C547FEE0F72D6C1B6E2E1C454B3072E3BE0703B`),
whose `tendrismoke.mmp` is the editor fixture's exact payload (SHA-256
`D7FCA4D4FC6DDBB52F8024B57EFD56E8871A8BE5BBE22A684AF6E9FD41FED987`).
This rejects selection of the base-game TendriSmoke atlas as the cause of the
remaining smoke-width/color discrepancy. The fixture records the complete
configuration so repeated resource-order checks do not depend on an installed
mod's mutable state.

Confirmed constructor values:

| Emitter offset | Value | Established role |
| --- | ---: | --- |
| `+0xF0..+0xF8` | `(-0.2, -0.2, 0)` | lower randomized spawn bounds |
| `+0xFC..+0x104` | `(+0.2, +0.2, 0)` | upper randomized spawn bounds |
| `+0xCC` | `0x58` / `88` | per-attempt spawn probability in percent |
| `+0xD0` | `6` | minimum spawn attempts per emitter update |
| `+0xD4` | `1000.0` | displacement per extra spawn attempt |
| `+0xD8` | `500` | maximum active-particle count |
| `+0xDC` | `15` | exclusive terminal atlas frame |
| `+0xE0` | `4` | atlas columns, established by common atlas construction |
| `+0xE4` | `1` | frame-delay reset |
| `+0xEC` | `32` | particle lifetime in logical ticks |
| `+0x10C` | `0.8` | initial scale |
| `+0x110` | `0.3` | initial vertical velocity |
| `+0x114` | `0.85` | early-life scale multiplier |
| `+0x1E0` | `0x48E9E0` | spawn/initialization callback |
| `+0x1E4` | `0x48EC20` | update callback |
| `+0x1EC` | pointer | particle texture |

Type `0x2010` shares this family but overrides `+0xD0` to 20, `+0xD8` to 1500, and callbacks. It must not be treated as evidence for ordinary Campfire/Fire cadence.

The factory common prefix at `0x485540` establishes `+0xCC = 100` and
`+0xD8 = 500`; the TendriSmoke branch then overrides `+0xCC` to `0x58` (88).
The base initialization at `0x4849DA` establishes `+0xD4 = 1000.0`. These
values are not inferred from the rendered effect. The earlier interpretation
of `+0xCC = 0x58` as an 88-byte particle-record size is rejected; the payload
allocator independently proves a 100-byte allocation.

For ordinary TendriSmoke the base constructor initializes `+0x128 = 1`,
`+0x130..+0x138 = (0,0,0)`, `+0x140..+0x148 = (0,0,0)`, `+0x14C = 0`, and
`+0x150 = 0`; this factory branch does not override them. The scheduler later
refreshes `+0x140..+0x14C` from globals `0x7AF5DC..0x7AF5E8` at
`0x496ECC..0x496F17` on every emitter quant, so their constructor zeros are not
the runtime late-life motion values. These four globals are now established as
the world wind direction and strength, rather than an unnamed orientation
vector. The native common setter at `0x55FCB0..0x55FCD0` copies a three-float
vector into object `+0x24..+0x2C` and the following float into `+0x30`. The
world wrapper at `0x564190` applies that setter to the world object and forwards
the same values to its client and `CWeather` member. A VM handler at
`0x669800..0x669841` reads exactly 12 bytes plus 4 bytes from the script stream,
sets `ECX = 0x7AF5B8`, and calls that wrapper. Its function pointer is installed
at `0x6673FE..0x66740B`; the script metadata record at `0x78C460` independently
names `SetWind`, assigns ID 30, and gives signature `ffff`.

The authored-data path independently fixes the same field semantics. Native
MOB dispatch recognizes `WORLD_SET` tag `0xABD0` at `0x481F6C`; its branch at
`0x4821DC..0x48230D` reads `WS_WIND_DIR` (`0xABD1`) as three floats and
`WS_WIND_STR` (`0xABD2`) as the next float before calling the same common setter
through `0x58EE20`. The strength path is clamped to `[0,1]`; immediately before
that call the authored value is also limited against global `0x79BE58`, whose
higher-level configuration provenance is not needed to identify these fields.
The fingerprinted `gz1g` `zone1.mob` record contains direction
`(-0.7281999588012695, -0.6853646636009216, 0)` and authored strength
`0.20000000298023224`. Therefore ordinary TendriSmoke late frames receive the
current world wind direction in `+0x140..+0x148` and current wind strength in
`+0x14C` every logical quant.

The rest of the same `WORLD_SET` branch also rejects an earlier editor
assumption about authored lighting. The loader initializes time `14`, ambient
`0.2`, and sunlight `0.6`, then reads the authored wind vector, wind strength,
time, ambient, and sunlight fields in that order. On exit it calls `0x58EEA0`
with only the time and `0x58EE20` with only wind direction/strength. The parsed
ambient and sunlight floats are not consumed. Consequently `WS_AMBIENT` and
`WS_SUN_LIGHT` must remain round-trippable MOB data, but using them as render
intensity multipliers is rejected as non-native.

Native outdoor illumination instead comes from the weather-table loader at
`0x563930..0x563F60`. Its default resource is
`Config\\LightsGipat.ini`; sections `sunlight`, `ambient`, and `sky` are read
as RGB integer triples, normalized by the exact `1/255` float at `0x73C36C`,
and stored as 24 RGBA entries at weather-object offsets `+0x22C`, `+0x3AC`,
and `+0x52C`, respectively. Missing integer-hour entries are filled by linear
interpolation between authored keys. The runtime at `0x566360..0x566BF7`
reduces world time modulo 24 and linearly interpolates each table from
`floor(time)` to `(floor(time)+1) % 24`. A zero RGB ambient entry alone falls
back to interpolated sunlight times `0.8`; the stock outdoor tables are
nonzero. Sunlight RGB becomes directional-light diffuse, ambient RGB becomes
the global ambient term, and sky RGB is converted back to a packed 0..255
background color. There is no additional `WORLD_SET` intensity multiplier.

The read-only base `Config` fixture pins all six authored weather resources.
`LightsGipat.ini` and `LightsSuslanger.ini` are byte-identical (SHA-256
`8D64191F2CEC9C83E2545318B7EFB905575E94524E2A3CADFD3CCC5BA29A324C`);
`LightsIngos.ini` is
`93B176E3227DFDE8F48D5984361A3435409826B232710ADAF568F785593A05A1`;
all three cave tables are byte-identical
`68E9AC6A863F1D707969FBDE9A2BCC0422595F7FE01EE5A9C5826291BF802372`.
The authored `map.txt` `#zone` record already classifies catalogued maps as
`gipat`, `ingos`, or `suslanger`; that zone-type field selects the corresponding
outdoor table, while uncatalogued/direct MOB loads retain the executable's
Gipat default. JMV-specific zone types likewise retain that default unless
their authored table identifies another stock allod. Because the Suslanger
file is identical to Gipat, they share one immutable table in the renderer.

The same runtime proves the exact ordinary sun direction. With time `t` in
hours and azimuth `a = t * 2*pi/24`, elevation in degrees is
`60 - 3.75*abs(t-12)` for `4 <= t <= 20`,
`30 + (t-20)*45/4` for `t > 20`, and
`30 + (4-t)*45/4` for `t < 4`. After conversion to radians `e`, the direction
submitted by the game is `(cos(a)*cos(e), sin(a)*cos(e), -sin(e))`. Thus the
editor's previous fixed `-0.62` Z component and extra 35-degree azimuth phase
are rejected. At noon the exact direction is approximately
`(-0.5, 0, -0.8660254)`.

The complete scale mutator at `0x485480..0x485523` multiplies all six spawn
bound components `+0xF0..+0x104`, initial scale `+0x10C`, field `+0x128`,
vertical velocity `+0x110`, and displacement threshold `+0xD4` by its single
float argument. Applied to the TendriSmoke defaults, authored strength `s`
therefore produces bounds `(-0.2s, -0.2s, 0)` through
`(+0.2s, +0.2s, 0)`, initial scale `0.8s`, vertical velocity `0.3s`, and
displacement threshold `1000s`. The spawn callback's 40% branch uses the
already-scaled initial scale times `0.3`, so its extra bound expansion is
`0.24s`.

The previous editor implementation that used authored strength directly as
initial particle scale while leaving bounds at `+/-0.2`, vertical velocity at
`0.3`, and the branch expansion at `0.3s` is rejected. For gz1g strength
`0.4`, that made emission width and rise speed 2.5 times their native values
and initial scale `0.4` instead of `0.32`; strength-1 zone18 happened to mask
the first two errors by matching the constructor defaults.

Field `+0x150 = 0` makes the Campfire family select render state 5/value 6,
not the value-2 path used when
`+0x150 = 1`. Direct surrounding device calls at `0x56A1CD..0x56A1F1` set
Direct3D render state 19/source blend to 5/`SRCALPHA` and state 20/destination
blend to 6/`INVSRCALPHA`. The internal state-5 switch changes that destination
factor to 2/`ONE` for `+0x150 = 1` and restores 6/`INVSRCALPHA` for
`+0x150 = 0`. Thus ordinary TendriSmoke uses standard alpha blending
`SRCALPHA, INVSRCALPHA`; the alternate value-1 family uses additive blending
`SRCALPHA, ONE`.

## Spawn callback (`0x48E9E0`)

Confirmed particle-record initialization:

- `record +0x50 = 32` lifetime.
- `record +0x44 = 8` only for Smoke type `0x2004`; otherwise it starts at atlas frame 0. TendriSmoke therefore starts in the fire-frame range.
- `record +0x4C = 1` frame delay.
- `record +0x0C = 0.8` initial scale.
- There is a 40% branch that halves scale and uses an additional `0.24` temporary offset.
- X/Y position is randomized inside the emitter bounds.
- Initial lateral drift is randomized and multiplied by `0.5`.
- Z starts at emitter Z plus a random component multiplied by `0.3`.
- `record +0x40 = 0.3` initial vertical velocity.

The X/Y values used for initial drift are the same randomized values used to
offset the initial position, and the update callback applies them only while
the atlas frame is below 2. It never negates them or computes a vector toward
the emitter center. Thus native particle centers do not converge inward. The
observed fire-body taper is instead produced by the confirmed per-frame sprite
scale contraction (`0.85`) while centers rise, followed by the confirmed smoke
scale expansion (`1.059999942779541`). Any editor mismatch in that hourglass
profile must be sought in scale evolution or conversion from particle scale to
billboard dimensions, not an invented center-seeking velocity.

The wrapper at `0x48ED60` calls this spawn callback and applies another
randomized offset from emitter fields `+0x130/+0x134`, invokes world-height
query `0x564070`, and then adjusts Z relative to emitter field `+0xA0`.
However, the constructor branch at `0x485C3C` installs that wrapper only for
type `0x2010`; ordinary TendriSmoke keeps `+0x1E0 = 0x48E9E0`. Therefore the
wrapper and `+0x130/+0x134` offset are rejected as evidence for ordinary
Campfire particle placement.

## Update callback (`0x48EC20`)

Confirmed behavior:

1. Decrement lifetime and kill at zero.
2. Decrement frame delay; when it reaches zero, increment the atlas frame.
3. Kill at frame 15 or later.
4. Reset frame delay from emitter `+0xE4`.
5. For frames above 7:
   - Fire type `0x2003` dies.
   - Type `0x2010` dies.
   - Other compatible types multiply their delay by four.
   - A randomized branch kills approximately 25% and continues approximately 75%.
6. At frame 8, halve vertical speed.
7. Every tick, add vertical speed to Z.
8. For frames below 2, also apply initial XYZ drift.
9. For frames below 7, multiply scale by `0.85` each tick.
10. For frames 7 and above, scale is multiplied by the exact binary32 value
    `1.059999942779541` (`14 AE 87 3F` at `0x73CE38`). After that multiply,
    position receives
    `newScale * emitter[+0x14C] * 0.15000000596046448 *
    emitter[+0x140..+0x148]`, component by component. The motion constant is
    binary32 `9A 99 19 3E` at `0x73CD18`. The emitter vector/scalar are the
    four scheduler-refreshed world-wind globals described above.

This is multiplicative scale evolution, not endpoint interpolation inside the
update callback. Because ordinary TendriSmoke has frame delay one, the atlas
frame advances at the start of every surviving logical update. Frames below 7
apply the exact factory multiplier `0.8500000238418579` (`9A 99 59 3F` at
emitter `+0x114`); frames 7 and above use the exact growth multiplier above.

## Render interpolation (`0x4976B0`)

Before each update, the scheduler copies current position/scale from particle
`+0x00..+0x0C` to the previous snapshot at `+0x10..+0x1C`, color from `+0x58`
to `+0x5C`, and atlas frame from `+0x44` to `+0x48`
(`0x4972DE..0x497308`). Render preparation clamps the engine's fractional
logical-quant value to `[0,1]` and names it here as `t`; it then emits:

- position and scale: `previous * (1 - t) + current * t` for all four floats;
- ARGB color: the same per-channel linear interpolation, followed by the
  engine's x87-to-integer conversion routine;
- atlas rectangle: the current snapshotted frame `+0x48`, without frame-number
  interpolation.

The same snapshot initialization is performed when a particle is attached at
`0x496A20`. TendriSmoke initializes current color to `0xFFFFFFFF` and copies it
to the previous endpoint, and no Tendri update changes either endpoint.
Consequently its interpolated color remains exactly opaque white; texture alpha
and the confirmed blend state supply the visible transparency.

## Pool and render path

### Scheduler (`0x496EB0`)

The exact scheduler is the emitter virtual method at `0x496EB0`, installed at
vtable `0x73CFD0 + 0x18`. It invokes all three emitter callbacks:

- `0x497263` invokes `+0x1E8` for each auxiliary 0x38-byte record in
  `+0x154`, count `+0x158`, before particle-list processing.
- `0x497308` invokes the update callback at `+0x1E4` once for each live active
  particle.
- `0x497522` and the high-probability fallback at `0x497583` invoke the spawn
  callback at `+0x1E0`.

Allocation cadence is now confirmed:

1. Update and retire the existing active list first.
2. Compute `movementAttempts` by converting `+0xC8 / +0xD4` to an integer
   using the current x87 rounding mode.
3. Attempt `max(movementAttempts, +0xD0)` births.
4. Refuse an attempt when active count `+0x1C0` is at least cap `+0xD8`.
5. Otherwise draw `random % 100` and invoke the spawn callback only when the
   result is below probability `+0xCC`.
6. If no main-loop birth succeeded, probability is greater than 50, the
   computed attempt count is positive, and the cap still permits it, make one
   unconditional fallback spawn attempt.

For ordinary stationary Campfire/TendriSmoke (`+0xCC = 88`, `+0xD0 = 6`,
`+0xD4 = 1000.0`, `+0xD8 = 500`), this means six births are attempted per
scheduler invocation. Each attempt succeeds with 88%
probability; if all six fail, the high-probability fallback makes one
unconditional attempt. Thus a permitted invocation creates between one and
six particles, subject to allocator failure and the 500-active cap. The
previous six-slot editor model is therefore rejected: six is an emission-rate
floor, not a total record count.

The main logical-quant loop at `0x4572FC..0x45759A` uses the integer at
`0x78FF34`, which is exactly 55 milliseconds. At `0x457368` it calls world
quant `0x5641F0`; that function calls particle batch scheduler `0x498480` once
at `0x564485`. The batch iterates registered effects and invokes each eligible
emitter's vtable slot `+0x18` at `0x4984E6`, which resolves to this scheduler
for the particle vtable. After the world quant, the main loop subtracts 55
milliseconds, multiplies the interval by the float at `0x73C348`, exactly
`0.0181818176060915`, and calls `0x40FB30`. It loops back and repeats the
entire world-quant chain for every accumulated 55 ms interval. Ordinary
steady-state particle scheduling is therefore once per 55 ms logical quant,
approximately 18.1818 Hz.

Batch eligibility is exact but its flag names remain unknown. When the byte
argument passed from world field `+0x6C` is zero, the batch invokes every
registered emitter. When it is nonzero, it invokes only emitters whose object
flags at `+0x08` include bit `0x08`. A scheduler return of zero causes the
batch to queue and then remove/release that effect; nonzero retains it. The
main loop reaches world quant when predicate `0x566E20` is true or predicate
`0x566EC0` is false. These conditions must remain structural labels until
their gameplay meanings are independently established.

Particle retirement is two-stage. The update callback returning zero causes
the scheduler to set `record +0x60 = 1` after restoring the previous
position/frame/color snapshot. On the next scheduler traversal, the flagged
record is unlinked, its emitter list node is returned to the emitter free
list, active count is decremented, and the 100-byte payload is returned to the
owning particle allocator.

Emitter list-node allocation is block based. When `+0x1C4` is empty,
`0x496BA8`/`0x496E09` allocate `+0x1CC` nodes (default 10), each 12 bytes,
chain them into the free list, then pop one and append it to the active tail.
The payload allocator at `0x498870` separately reuses returned payloads and
allocates a zero-initialization-unspecified 100-byte record only when its
recycler is empty. This distinguishes emitter-node reuse from particle-record
reuse.

Confirmed emitter-list fields:

- `+0x1B8` active-list head
- `+0x1BC` active-list tail
- `+0x1C0` active count
- `+0x1C4` free-list head
- `+0x1C8` allocator/root
- list node layout: next at `+0`, previous at `+4`, particle payload at `+8`

Render preparation around `0x4976B0` and submission around `0x498940/0x498A5D` establish:

- `particle +0x48` selects a 16-byte atlas rectangle from emitter array `+0x1D0`.
- `particle +0x58` and `+0x5C` contain packed colors interpolated using the global frame fraction.
- Particles are camera-facing billboards before submission.
- Emitter `+0x150` selects destination blend: value 1 drives additive
  `SRCALPHA, ONE`; value 0 restores standard alpha
  `SRCALPHA, INVSRCALPHA`.

The TendriSmoke spawn callback at `0x48E9F4` initializes `particle +0x58` to
`0xFFFFFFFF`. On successful attachment, `0x496A4A..0x496A53` copies `+0x58`
to interpolation endpoint `+0x5C`. Both color endpoints are therefore opaque
white for this family. Its packed-color interpolation adds no tint or alpha
fade: visible color and transparency come from the sampled texture and the
standard-alpha blend state, not invented per-age color curves.

The exact scale-growth constant at `0x73CE38` is the IEEE-754 single
`0x3F87AE14`, or `1.059999942779541`. It replaces the earlier approximate
label of 1.06.

### Native billboard submission interface

A read-only runtime probe of the exact fingerprinted executable captured the
render-preparation thread at EIP `0x497C5E`. Its renderer argument was the
static object at `0x7AF060`, with vtable `0x73D93C`; vtable slot `+0x44`
resolves to the in-executable particle submission method
`0x502CA0..0x50367C`. These static addresses apply only to the executable hash
above. The process ID, thread ID, and any heap addresses observed by the probe
are transient and are not fixture evidence.

The call at `0x497CCB` supplies three arguments to this method: the additive
flag, the global interpolated position/scale vector at `0x7A9BD0`, and the
global UV/color vector at `0x7A9BE0`. Method `0x502CA0` reads the first vector's
data pointer and count at `0x502F8E` and the second vector's data pointer at
`0x502FAC`. Each position/scale entry is exactly the four interpolated floats
`(x, y, z, scale)` emitted by `0x497B4C`; UV/color entries are 32 bytes each.
This identifies the exact native scale-to-billboard path but does not yet
establish the final world-space quad width by itself.

The downstream implementation is the fingerprinted `3DfpFPU.dll` interface
slot `+0x60`, RVA `0x6F00..0x73BC` in the isolated memory image. Its five
arguments are the copied camera/projection matrix, input `(x,y,z,scale)`
records, four 32-byte output vertices per particle, clipping-byte output, and
particle count. At RVA `0x6FCF..0x6FEA` it loads source X/Y/Z and then the
fourth float as scale. It transforms the center, calculates the perspective
scale, and at RVA `0x70D3..0x70ED`, `0x7174..0x71B2`,
`0x721F..0x7260`, and `0x72CA..0x7301` emits the four screen-space corners as
center minus or plus that projected scale on each axis. Native scale is
therefore the billboard half-extent/radius: total sprite width and height are
exactly `2 * submittedScale` before projection.

EI Mapper's particle VBO uses local corners `-0.5..+0.5` and its shader
multiplies those by `u_size`. Passing submitted scale directly consequently
made total editor width equal to scale, exactly half native width. The exact
faithful conversion for that VBO convention is `u_size = 2 * scale`. This
factor is established by native vertex math; the screenshots only exposed the
discrepancy and did not determine it.

For backend provenance, the loaded read-only modules are:

- `D3DImm.dll`: 210,944 bytes, SHA-256
  `8339438C623662415851062723A4E5141717FF7AA54AB41EB0774C7431DB68AD`;
- `3DfpFPU.dll`: 147,456 bytes, SHA-256
  `A7B11BF5C85898C4A9E1F02F609F05C87BAC627834FAE21A3466230C63E6FC82`.

Isolated interface-export inspection rejects both initially suspected slot-44
entries as this particle method: `D3DImm.dll`'s returned interface does not
contain a callable slot there, and `3DfpFPU.dll`'s slot has a five-argument
four-vertex transform signature. The actual particle submission method is the
`game.exe` method above. Repository-local `scripts/particle_process_probe.cpp`
performs the read-only process inspection and short thread-context sampling;
`scripts/dump_d3dimm_interface.cpp` performs isolated DLL interface inspection.

Other exact constants used by these callbacks are now fingerprinted:

- `0x73B86C`: float `0x3F000000` = 0.5, used for half-scale and lateral drift;
- `0x73C54C`: float `0x3E99999A` = `0.30000001192092896`;
- `0x73C468`: double `0x3DF0000000100000` =
  `2.3283064370807974e-10`, the random-u32 normalization factor;
- `0x73CC88`: double `0x3FE0000000000000` = 0.5, used when frame 8 halves
  vertical velocity;
- `0x73CD18`: float `0x3E19999A` = `0.15000000596046448`, applied with
  emitter orientation after multiplying current scale and `+0x14C`.
- `0x73C348`: float `0x3C94F209` = `0.0181818176060915`, the logical-quant
  normalization multiplied by the 55 ms interval.

### Random-number generator and reproducibility boundary

Every random draw used by the ordinary Tendri spawn and update callbacks
comes from the same global generator: state at `0x7A9D40`, remaining-result
index at `0x7A9D44`, and the 256-word result array at `0x7A9D48`. Refill
routine `0x49EE70..0x49F148` is Bob Jenkins' ISAAC construction: it maintains
256-word state/results, uses golden-ratio word `0x9E3779B9`, performs the
standard eight-word initialization mix, and applies ISAAC's four alternating
shift/xor steps in its refill loop. Initializer `0x49F150..0x49F4AF` obtains
external clock/system entropy through `0x49F660`; the stock game therefore
does not have a resource-derived or otherwise repeatable random realization.

The scheduler and Tendri callbacks consume this global stream, rather than a
per-emitter PRNG. The spawn callback consumes one word for its 40-percent
half-scale/expanded-bounds branch and three normalized words for X, Y, and Z.
The late update consumes a word and tests its low two bits, killing when they
are zero. These accesses and the complete ISAAC routines are pinned in the
binary fixture.

Consequently a deterministic editor preview can reproduce the exact ISAAC
algorithm, draw order, distributions, and callback equations, but cannot
reproduce the game's particular run without capturing the game's transient
global state. EI Mapper must use an explicitly editor-local deterministic seed
for stable validation; this is a reproducibility adaptation, not an authored
particle rule and not evidence for a different RNG.

### Atlas rectangles and orientation

The common factory tail at `0x4885B4..0x488781` constructs the atlas rectangle
array directly. With `columns = +0xE0 = 4`, `step = 1 / columns`, and frame
index `i`, it stores:

`u0 = (i % 4) * step + 1/512`,
`v0 = 1 - floor(i / 4) * step - 1/512`,
`u1 = (i % 4 + 1) * step - 1/512`,
`v1 = 1 - (floor(i / 4) + 1) * step + 1/512`.

Frames are therefore row-major from the texture's top row to bottom row, with
a 1/512 inset on every edge. `particle +0x48` selects one of these rectangles.
This proves the prior editor presentation using the opposite vertical row
orientation is incorrect; it is not a subjective screenshot adjustment.

## Rejected hypotheses and corrected labels

- `0x485380` is an active-list clear/destructor path, not the emission scheduler.
- `0x4976B0` is render preparation/visibility/vertex allocation, not the emission scheduler.
- A prior label of these functions as the scheduler was incorrect and must not be propagated.
- The editor interpretation of `+0xD0 = 6` as six persistent/staggered emitter
  slots is rejected by the scheduler at `0x496EB0`.
- `0x40EB60..0x40ECA5` is not established as a `CTorch` attachment path. Its
  confirmed call at `0x4575C1` is a global render-time type-`0x2001` lookup, so
  it cannot prove that core flame, peripheral flame, and smoke use one emitter.
- Visual descriptions and captured GIFs are validation targets and search pointers only. They do not establish density, lifetime, blend, color, or movement rules.
- `OBJ_COMPLECTION` does not reach the torch particle factory or scale mutator
  and is rejected as a particle size, tint, or intensity control.
- The native Tendri callbacks contain no inward center motion. The apparent
  fire-to-smoke hourglass is the established shrink-then-grow sprite sequence,
  so adding convergence velocity would be an unsupported visual tuning rule.

## Implemented editor model

`source/render/particle_animation.h` now implements the confirmed ordinary
Tendri scheduler and callbacks instead of the rejected six-slot
approximation: 55 ms quants, six probability attempts plus fallback, the
500-active cap, two-stage retirement, exact spawn bounds/draw order, frame
delays and survival, vertical/drift/wind motion, scale constants, and
snapshot interpolation. Its ISAAC implementation uses a stable editor-local
seed for the reproducibility reason established above.

`source/render/view.cpp` now passes authored particle scale (and exact torch
strength), applies the loaded MOB's WORLD_SET wind, uses the native top-row
atlas rectangles and inset, submits opaque-white texture color with standard
alpha blending, and performs the native 60-quant torch prewarm. Campfire
flame and smoke are rendered as one population. Selected particle-source
nodes receive a separate procedural cyan editor marker; selection no longer
recolors the gameplay Tendri sprites.

Superseded implementation notes:

- The former helper created six deterministic slots staggered over 32 ticks;
  the scheduler evidence rejects that model.
- The former `drawFireSmoke` rise, scale, alpha, tint, and hash-noise curves
  were inferred and have been removed.

## Open questions, in priority order

1. Across other factories, separately label `+0x128` and the type-`0x2010`-only
   `+0x130..+0x138` vector without applying them to ordinary Campfire.
2. Verify native OpenGL and ANGLE blend/color behavior from identical authored records.
3. Establish the exact native per-surface spatial-cell light-list traversal and
   active-light limit. The editor currently exposes eight shader slots and
   selects frustum-visible lights by camera distance; this is explicitly not
   claimed as the native local-cell selection rule.

## Required implementation discipline

- Add a repeatable extractor/report before changing renderer behavior.
- Keep extracted field provenance, executable fingerprint, and source resource path in every report.
- Add fixtures for at least `gz1g` campfire, a torch with built-in particle/sound, and `gz10g` lava.
- Preserve the editor-only particle marker independently of gameplay sprites; the marker remains selectable and is not part of the effect.
- Validate native OpenGL and ANGLE separately.
- Update this ledger with each confirmed finding before implementing it, so context compaction cannot reset the investigation.
