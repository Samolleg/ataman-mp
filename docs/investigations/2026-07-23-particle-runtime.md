# Evil Islands particle runtime evidence

Last updated: 2026-07-25

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
2. Across non-Tendri factories, establish their independent scheduler,
   callback, texture, blend, and interpolation rules before replacing their
   current editor preview styles with native simulations.
3. Establish the special local-light flag `+0x40` semantics and the exact
   non-terrain object-light receiver path. Ordinary terrain candidates and
   their channelwise-maximum composition are confirmed; the editor's 64-light
   capacity covers the complete selected JMV MOB corpus without the rejected
   nearest-camera truncation.

## Required implementation discipline

- Add a repeatable extractor/report before changing renderer behavior.
- Keep extracted field provenance, executable fingerprint, and source resource path in every report.
- Add fixtures for at least `gz1g` campfire, a torch with built-in particle/sound, and `gz10g` lava.
- Preserve the editor-only particle marker independently of gameplay sprites; the marker remains selectable and is not part of the effect.
- Validate native OpenGL and ANGLE separately.
- Update this ledger with each confirmed finding before implementing it, so context compaction cannot reset the investigation.

## Deterministic all-type editor catalog

`scripts/build_particle_catalog_mob.ps1` creates a supplemental MOB containing
one standalone `PARTICL` record for every public particle type from 8192 through
8274. It clones a structurally valid authored particle record through the
fingerprinted MobExplorer serializer instead of synthesizing MOB container
bytes. Both input MOBs are read-only and the output must be a separate path.

The generated control at
`artifacts/particle-catalog-grid/all-particles-grid.mob` has:

- 83 unique particle records and no other objects;
- particle IDs 900000 through 900082;
- type IDs 8192 through 8274 in row-major order;
- a 10-by-9 grid spanning `(12, 16, 0)` through `(84, 80, 0)`, centered at
  `(48, 48)`;
- center spacing of 8 world units, or 4 terrain tiles, and authored scale 1;
- SHA-256
  `3BA88B8FD932D4B46DC632233541FF63B218FBDC6C4996BFA614877527379638`.

The independent particle MOB reporter finds exactly 83 unique IDs and types.
EI Mapper strict validation covers all 12,094 bytes, finds 83 objects, and
performs a byte-identical unchanged serialization. A second generation from
the same fingerprinted inputs produces the identical output hash. The adjacent
JSON manifest records the complete layout and input/output fingerprints.

This catalog is an editor rendering inventory, not proof that every native
family can operate as a standalone map source. Carrier-dependent, transient,
moving, wall, and spell-owned types require separate native controls with their
real owner parameters before their visual behavior can be called reconstructed.

### Portable flat-ground collection

`scripts/build_particle_catalog_collection.py` packages the catalog with a
fully synthetic 3-by-3-sector MPR and a directly discoverable collection file.
The MPR has the internal member `zone1.mp` plus the nine sector members for
X/Y indices 000 through 002, declares the stock 512-slot terrain atlas, has
constant height and normals, and assigns all 2,304 terrain tile words to atlas
slot 1 with rotation 0. It embeds no proprietary
imagery; at render time the configured Jamais Vu Zone 1 atlas supplies the
uniform common-ground tile.

The original synthetic writer emitted a linear member table with every
`NextIndex` set to -1. That archive was editor-readable, but this is rejected
as runtime-safe evidence: the shipped lookup hashes the lowercase CP1251
filename by byte sum into the member table and then follows the `NextIndex`
collision chain. The corrected deterministic writer case-insensitively sorts
and rejects duplicate member names, assigns the hash slots, and links
collisions through valid in-range acyclic chains. The ten-member fixture
deliberately exercises collisions in three buckets (including a three-member
chain), and regression lookup resolves every MP/SEC payload using uppercase
requests.

The corrected canonical generated MPR is 85,611 bytes with SHA-256
`63E8AF4DA06B384253B8D167FF14452325F6057CEE7E567CCA66C03EBA621C0E`.
Independent parsing confirms the map member plus nine flat sectors, 512 atlas
slots, and the uniform slot-1 tile assignment. EI Mapper parses all bytes and
performs a byte-identical unchanged round trip. Two independent generations
produce identical collection, MPR, MOB, and manifest bytes. The collection
selects the `jamais-vu` profile and registered `gz1g` metadata, resolves the MPR
first and then the existing 83-type MOB by package-relative paths, and does not
modify or depend on a live installed map file.

## Weather-family reconstruction: Tornado and Lightningblast

The fingerprinted executable selects Tornado (type 8202) and Lightningblast
(type 8240) as independent particle families. They do not use the generic
Tendri fire/smoke callbacks, and the catalog's former single textured circle is
therefore rejected.

For Tornado, the factory branch at `0x00486574..0x0048679F` confirms four spawn
attempts per logical quant, probability 100, capacity 800, six local sources,
a 12-world-unit source spine, ordinary source-alpha blending, texture
`Tornado`, and callback addresses `0x00489F70`, `0x0048A310`, and
`0x0048A560`. The callback ranges establish the visual rules:

- particles select one of three classes, live 100 through 108 quants, and
  spawn at a random relative height along the six-source spine;
- two classes use phases 0..11 and billboard radius based on spiral radius;
  the third uses phases 12..15 and a small fixed radius;
- particles rotate around the source spine, angular speed grows by 1.01 per
  quant, spiral radius contracts by 0.99, and relative vertical speed gains
  0.0001 per quant;
- alpha ramps from 0.1 toward 1 and is multiplied by `sqrt(1-height)`, while
  RGB is captured from the current sunlight color;
- the local-source callback anchors the base one world unit above terrain and
  bends the six-source spine when its carrier moves. A stationary standalone
  catalog source therefore uses a straight vertical spine.

For Lightningblast, the factory branch at `0x00487746..0x0048783F` confirms
probability 88, initial 250 spawn attempts, capacity 600, lifespan/phase count
15, one-quant phase timing, +/-0.2 random position bounds, additive blending,
texture `TendriLight`, and callbacks `0x004923B0`, `0x00492B80`, and
`0x00492CC0`. The callback ranges establish that it is a finite burst, not a
persistent glow: it creates ten moving local sources, emits source flashes,
ground-cloud phases, and short animated bolt particles, applies 0.075 downward
acceleration to each source, reduces generation to 50 attempts, and stops
generation after ten local-source updates. A catalog preview may repeat the
complete finite burst for inspection, but each individual cycle must retain
the native timing and motion rather than turning it into a continuous emitter.

`scripts/particle_texture_report.py` independently fingerprints the active JMV
texture archive (SHA-256
`2E8CA42FB75FAC12E5659C563C547FEE0F72D6C1B6E2E1C454B3072E3BE0703B`).
Both `tornado.mmp` and `tendrilight.mmp` are 1024-by-1024 DXT3 textures with 11
mip levels and 16 phases in a 4-by-4 atlas. Their payload SHA-256 values are
`549EBC0C8FC0DC57544DB97FB0E426E92F85863315A00035BC54A198473C280A` and
`F63D73CE04ED76339D2B15296E91B2089873C7FA115099CEDD596831ACBACC88`
respectively. The established top-row-first, half-texel-inset atlas addressing
therefore applies unchanged.

The binary-report fixture now pins each factory and callback range plus the
12.0 tornado height, 0.2 factory speed factor, and 0.075 Lightningblast gravity
constants. Remaining controls before claiming complete native equivalence are
a moving Tornado carrier (for source-spine bending) and a runtime
Lightningblast spell capture (for owner-supplied size and destination). These
do not block faithful stationary catalog previews of the confirmed paths.

## Ordered catalog reconstruction: Casting, Nuke, and Bansheecasting

The three catalog IDs immediately after Tornado bind distinct factories and
must not reuse Tornado, Tendri fire/smoke, or the generic soft-circle preview.

Nuke (type 8204) is fully defined for a stationary map source. The factory at
`0x00486A72..0x00486BD4` confirms an initial 128 attempts, capacity 400,
one local source, texture `AcidRay`, ordinary alpha blending, and callbacks
`0x00488F90`, `0x00489190`, and `0x004892E0`. The callbacks confirm a finite
13-quant generator: the attempt count halves toward 8; particles begin inside a
0.2-scale cylinder up to two scale units high; early alpha grows by four; upward
velocity is attenuated by 0.85 and receives 0.055 downward acceleration per
quant. On falling below the half-scale impact plane, particles spread radially,
advance toward phase 14, expand by 1.13, and lose 16 alpha units per quant.

Casting (type 8203) and Bansheecasting (type 8205) explicitly require a carrier.
Their factory branches at `0x00486993..0x004869DC` and
`0x004869DD..0x00486A71` confirm textures `FireSmoke` and `Banshee`, one-quant
phase timing, indefinite system lifespan, and their distinct callback triplets
starting at `0x00489320` and `0x00489BE0`. Callback evidence confirms that both
derive their ring radius and height from the carrier bounding volume; a bare
standalone particle record does not contain those values. Casting uses three
orbiting/rising tails followed by an expanding closing ring. Bansheecasting uses
an eight-quant elevated ring whose radius converges toward a carrier-derived
target while its short-lived sprites advance atlas phases.

For the catalog only, the editor may supply an explicit synthetic carrier bound
equal to the authored particle scale and replay each finite sequence. That is a
stable visualization control, not a claim that an ownerless native record has
implicit bounds. Attached runtime controls remain required to validate arbitrary
carrier sizes.

The active texture archive confirms 4-by-4 DXT3 atlases for all three paths.
`firesmoke.mmp` is 1024 square with SHA-256
`EF61A113CCB8598D37C6A5147C852C0D2A594156EB51B8092313AA62BAA312CA`;
`banshee.mmp` is 1024 square with SHA-256
`5FAC702469510F5C1E6ACB5C520629DF40810C837AA83A76AB1871C013EFDC63`;
and `acidray.mmp` is 512 square with SHA-256
`48C7F220718AE66AD98C17B6C53C3230E2F9ED7C2FB88320809E19650F0A2ADE`.

### Implemented ordered controls

`source/render/particle_animation.h` now contains separate deterministic
simulators for these three families, and `source/render/view.cpp` dispatches
types 8203, 8204, and 8205 before the generic preview path. Each uses its
confirmed atlas and ordinary-alpha submission. Nuke retains the complete
stationary callback path. Casting and Bansheecasting explicitly construct the
synthetic scale-derived carrier bound described above; they do not silently
claim an ownerless native default. Each finite effect repeats only after its
particles have aged out so it remains inspectable in the all-type catalog.

The focused animation tests pin deterministic replay, phase bounds, finite
retirement, restart behavior, Nuke impact phases, and the elevated ring control.
The complete application builds successfully after the graphless-test merge,
and the noninteractive widget smoke test passes. Native OpenGL and ANGLE visual
approval remain required before promoting the two carrier-dependent controls
to fully validated status.

## Ordered catalog reconstruction: Mushroom, Blood1, and Firewall

The executable's complete 83-entry particle dispatch table maps types 8206,
8207, and 8208 to factory branches `0x00486BD5`, `0x00486C43`, and
`0x00485B72` respectively. This rejects the catalog's former generic glowing
circle for Mushroom and Blood1, and rejects the generic orange-fire style for
Firewall.

Mushroom (type 8206) uses the `ChangeHero` atlas and the callback triplet
`0x004889D0`, `0x00488DB0`, and `0x00488F00`. The factory fixes ten creation
attempts, capacity 4000, indefinite system life, one-quant phase timing, and a
single local source. Callback evidence establishes a finite generator: the
source expands logarithmically, rises by 0.12 authored-scale units per quant,
and disables generation after 30 updates. Particles live 16 through 19 quants,
advance only through phase 14, grow by 1.1 while below phase 8, damp velocity by
0.7, ramp alpha upward in 0x40 steps, and fade during the last four quants.
Their horizontal velocity is additionally steered toward the expanding local
source. The callback contains a separate type-8206-only one-shot auxiliary
effect dispatch at source age zero; that non-particle gameplay effect is not
part of the standalone billboard preview.

Blood1 (type 8207) uses `RainSnow`, ordinary alpha blending, and callbacks
`0x0048EEA0`, `0x0048F300`, and `0x0048F380`. Its factory fixes capacity 300,
six-quant particle life, 15 phase ticks, and one local source. The first create
callback derives both its local-source count and ten-times-larger creation
attempt count from the attached carrier's size field, then emits phase-12
particles from the carrier position along randomized unit directions. Per-quant
updates move the particle, multiply velocity by 0.9 and scale by 0.8, add a
scale-proportional downward term of 0.05, and use the seven-entry native alpha
sequence. Generation stops after the local source passes age five. A bare
`PARTICL` record contains only type, position, and scale, so the all-type catalog
does not author the carrier-size input. As with Casting, the editor catalog may
use authored scale as an explicitly synthetic carrier bound; attached-carrier
captures remain required before claiming arbitrary-owner equivalence.

Firewall (type 8208) shares the `TendriSmoke` atlas and ordinary Tendri update
callback with the fire/smoke family, but the factory specializes it at
`0x00485BBF..0x00485D46`: 20 base attempts, capacity 1500, the wrapper at
`0x0048ED60`, and local-source callback `0x0048EE20`. The local callback derives
the active attempt count from wall-vector length, clamps it to at least six and
to capacity/16, while the wrapper distributes births along that vector and
snaps each birth to terrain height. The standalone MOB record has no wall-vector
field; the constructor's zero vector therefore selects the confirmed six-attempt
point-source path. Firewall terminates particles after the fire phase range and
must render the `TendriSmoke` atlas, not a procedural orange disc. A nonzero wall
control must be captured from the spell/effect owner before the elongated wall
form can be validated.

The binary evidence report now fingerprints all three factory specializations,
their callback ranges, callback-selection sites, texture-name sites, and the
bounded motion/lifetime/source-update instruction windows. Texture payload
fingerprints for `changehero.mmp` and `rainsnow.mmp` are recorded separately
before renderer dispatch is enabled.

### Implemented ordered controls

The editor now dispatches Mushroom and Blood1 to separate deterministic
simulators and routes Firewall through the confirmed TendriSmoke fire-phase
path. Mushroom repeats only after its 30-quant generator and live population
finish. Blood1 repeats its short burst with authored scale explicitly serving
as the catalog's synthetic carrier bound. Firewall retains the constructor's
zero wall vector for standalone records; no invented wall direction is added.
All three preserve the independent cyan editor selection marker.

The active texture archive pins `changehero.mmp` as a 1024-square DXT3 atlas
with SHA-256 `36BFF7B3FD282EBF45A97DAD5A752B7B61102AC7831EA31D87A984CC6BE574B9`
and `rainsnow.mmp` as a 2048-square DXT3 atlas with SHA-256
`BF324F155FF82C9C6C17942BDBA1ADC45423FFB4AA2A1AF85E26177B28FF7E43`.
The executable fixture also pins Blood1's alpha table as
`0, 64, 128, 192, 255, 255, 255`. Focused tests cover deterministic replay,
finite retirement/restart, atlas phase bounds, Blood1's fixed phase 12, and
Firewall's phase-0-through-7 restriction.

## Ordered catalog reconstruction: Firearrow, Acidray, and Bluegas

The fingerprinted executable maps types 8209, 8210, and 8211 to three distinct
factories. Their former generic orange, green, and blue circles are rejected.
All three use ordinary source-alpha/inverse-source-alpha blending.

Firearrow (type 8209) selects `FireArrowSmoke` and callbacks `0x0048CBA0` and
`0x0048CED0`. The factory fixes one creation attempt, 15-quant particle life,
one-quant phase timing, a 0.3 base size, and 1.03 late size growth, but also
clears the generation flags. This is an owner-activated projectile trail, not
an independently persistent map emitter. Once generation is enabled, the
first particle is a 0.4-size-factor head anchored to carrier position plus
carrier velocity and randomly selects phases 0..2. Later particles start at
0.2 size factor in phase 2 or 3. Ordinary trail particles grow by 1.4 below
phase 6, then by 1.03, while their stored transverse displacement grows by
1.1; the alternate roughly 29-percent class grows only below phase 6 and
damps its displacement by 0.95. The actual trail geometry depends on the
owner's velocity and its two perpendicular basis vectors, neither of which is
authored by a standalone `PARTICL` record. The catalog control must therefore
be labeled as an explicitly activated zero-speed callback replay; inventing a
projectile speed or direction is rejected.

Acidray (type 8210) selects `AcidRay` and callbacks `0x0048D050` and
`0x0048D330`. It creates one particle per quant at the default 100-percent
probability, with 15-quant life, phases starting at 0 or 1, and 0.3 authored
base size. Its 19-entry width table is
`0, .85, .93, 1, .97, .93, .87, .8, .73, .65, .55, .43, .3, .2, 0, 0, 0, 0, 0`;
the size table is identical except for an initial 1.1. Position is the spawn
snapshot plus the transverse vector multiplied by authored size and the
current width. With no moving carrier the transverse vector is exactly zero,
so a standalone map source has a fully defined stationary pulsing-drop path at
its authored origin. A future moving-owner fixture is required only for the
ray's transverse geometry.

Bluegas (type 8211) selects `Stench`, sets 80-percent birth probability,
45-quant life, three quants per phase, and the carrier-required flag, then
uses callbacks `0x0048D420` and `0x0048D7F0`. Creation fails when no carrier is
attached. From the carrier bound it derives a horizontal radius of 0.3 times
the bound, a height of twice the bound, and `max(1, round(5 * authoredScale))`
attempts. Each particle begins inside that cylinder at 0.7 radius size,
rotates around the carrier axis by an angle proportional to its starting
radius, rises at a random 0.02..0.03 fraction of the height, accelerates that
rise by 1.02, expands by 1.01, and loses 32 alpha units per quant during its
last seven updates. The all-type catalog may use authored scale as an explicit
synthetic carrier bound, as for Casting and Blood1; it is not evidence for a
native ownerless default.

The active JMV texture archive (SHA-256
`2E8CA42FB75FAC12E5659C563C547FEE0F72D6C1B6E2E1C454B3072E3BE0703B`)
contains `firearrowsmoke.mmp`, a 1024-square DXT3 4-by-4 atlas with payload
SHA-256 `9624B8F3F769DDAD435F69D0D780BBD1F5EA7EA50DA1019E0484836FCD41466B`.
`acidray.mmp` retains the previously pinned 512-square DXT3 atlas fingerprint.
`stench.mmp` is absent from that JMV archive and resolves through the active
Base fallback texture archive instead. That archive has SHA-256
`79548FBD94E797FE9EDC3E990EB0231A350571B2CEF575B009FA972F0002268C`;
its `stench.mmp` payload is a 256-square `0x00004444` texture with three mip
levels and SHA-256
`D2468084C3D8FFEFDD0FCC1614220A5CE43C2799AA860545BBF4CF13CB816CE5`.
The repeatable binary and texture fixtures pin all three factories, callbacks,
tables, constants, texture names, and archive provenance before renderer
dispatch is enabled.

### Implemented ordered controls

The editor now dispatches all three IDs before the generic preview path.
Firearrow replays the explicitly activated zero-speed callback population with
its persistent head and phase-advancing collapsed trail; no un-authored carrier
velocity is introduced. Acidray continuously runs its confirmed stationary
one-particle-per-quant size-table path. Bluegas constructs the documented
authored-scale synthetic carrier cylinder and runs its native orbit, rise,
growth, phase, population, and terminal-alpha rules. Each path uses its own
confirmed atlas, ordinary alpha blending, and the separate cyan selection
marker. Focused tests pin deterministic replay, phase bounds, stationary
Firearrow/Acidray positions, Firearrow head scale, and the bounded Bluegas
population.

## Ordered catalog reconstruction: Link and the acid/electric sphere shells

The fingerprinted executable maps Link (type 8212) to factory range
`0x0048599D..0x00485A9D`, callbacks `0x0048D8D0`, `0x0048DE30`, and
`0x0048E040`, and the `Link` atlas. The factory fixes 80-percent birth
probability, ten attempts per quant, 45-quant nominal life, three quants per
phase, unit authored size, and a default wall vector of `(-10, 0, 0)`. A bare
`PARTICL` record does not carry another destination/vector field, so this
constructor default is the exact standalone catalog control rather than an
editor-authored guess.

The Link create callback builds an orthonormal basis around the wall vector,
starts particles within the authored +/-0.2 source box, and advances them
along the ten-unit vector. One quarter of central births use progression
0.10..0.12 and size 0.13..0.16; the rest use progression 0.17..0.25 and size
0.03..0.05. Each birth stores an angular step of `0.2 * radialDistance`.
Updates rotate the radial offset, terminate after the accumulated progression
exceeds the wall length, and add the confirmed parabolic height
`part * (1 - part) * wallLength * 0.9`. Alpha is the source alpha clamped by
`1000 * min(part, 1 - part)`, producing the native endpoint fades. The local
source callback fades that source alpha by 16 per quant only after generation
is disabled. This rejects both the previous no-preview state and any straight
beam substitute.

Sphereacid (type 8213) and Sphereelectricity (type 8214) share factory range
`0x0048560E..0x00485734` and callbacks `0x0048E070` and `0x0048E7C0`, but bind
the separate `ProtectionA` and `ProtectionE` atlases. Both are additive,
carrier-required systems with 90-percent birth probability, 15-quant life,
15 active phases, and one-quant phase timing. The first creation obtains the
carrier bounding sphere, multiplies its radius by 1.2, and places births on a
spherical shell at 0.65 of that radius. Particles rotate around the local Z
axis by 0.01 radians per quant and are projected back against smoothed carrier
motion; the stationary catalog control therefore remains a rotating sphere.

For both shell types, one birth in eleven receives a larger random size
multiplier: 1.5..2.5 for acid and 3.5..4.5 for electricity; other births use
0.7..1.3. Size follows the exact 16-entry lifetime table
`.01, .02, .03, .035, .035, .035, .035, .035, .035, .035, .035, .035,
.035, .03, .02, .01`; acid additionally multiplies it by 1.6. Initial phase is
0..10 and subsequent phases advance modulo 15. A bare particle record has no
carrier bounding volume, so the catalog/editor control must explicitly use
authored scale as a synthetic carrier bound, as already documented for
Bluegas, Casting, and Blood1. This does not claim a native ownerless default.

The active JMV texture archive pins `link.mmp` as a 1024-square DXT3 atlas with
SHA-256 `443B941A24B0984F4F53448CF2747E7D84F83A004E1C5557480C4DEECAF45A1C`,
`protectiona.mmp` as a 1024-square single-mip DXT3 atlas with SHA-256
`7CB038591A9092BA9DC0E745539EB2966B86037DFD2B92B91BAA9619D00F0CAD`,
and `protectione.mmp` as a 1024-square DXT3 atlas with SHA-256
`5BEC16963B7548BAF64B9C4DE985B945EF45FAA14B773037D12F2CBF1426896F`.
The Base fallback archive contains independently fingerprinted 256-square
versions of all three. The binary fixture now pins the two factories, five
callback bodies, callback and texture selections, bounded rule windows, size
table, and scalar constants before renderer dispatch is changed.

### Implemented ordered controls

The editor now dispatches Link, Sphereacid, and Sphereelectricity before the
generic preview path. Link replays the constructor's confirmed default wall
vector with native births, rotating cross-section, arc, phase progression, and
endpoint fades through the `Link` atlas and ordinary alpha blending. The two
sphere effects use separate deterministic shell simulators, their confirmed
size classes and lifetime table, their own atlases, and additive blending.
Because standalone records do not provide a carrier bounding volume, authored
scale is explicitly the synthetic stationary carrier bound for both shell
controls. The cyan editor selection marker remains a separate overlay.

Focused tests pin Link determinism, population bounds, phase/alpha limits, arc
height and travel distance, plus both sphere populations, shell radius, phase
bounds, scale positivity and opacity. The complete executable and texture
reports regenerate byte-identically from their fingerprinted read-only inputs.

## Ordered catalog reconstruction: Spherefire, Clayring, and Teleport

Spherefire (type 8215) is binary-confirmed as the third ordinary member of the
same factory and callback pair used by Sphereacid and Sphereelectricity. It is
carrier-required, additive, uses `ProtectionF`, inherits the same 90-percent
birth probability, 15-quant life, 0.65-of-carrier-shell placement, 0.01-radian
rotation, 16-entry lifetime size table and stationary/moving-carrier projection.
Its one-in-eleven large class uses 1.5..2.5 like acid, while final submitted
size is `speedClass * carrierRadius * sizeTable[lifetime] * 2`. As with the two
earlier sphere controls, authored scale is an explicitly synthetic stationary
carrier bound for a bare catalog record; no native ownerless bound is claimed.

Clayring (type 8216) selects factory range `0x004870D2..0x004872E4`, callbacks
`0x0048F7F0`, `0x0048FAB0`, and `0x0048FBE0`, the `ClayRing` atlas, and additive
blending. It creates ten local sources on a circle, uses 80 attempts per quant,
15-quant nominal life and two quants per phase. Each source begins with life 14.
The first local update decrements it to 13 and creates one phase-0, scale-0.2
anchor for each source; those anchors track the moving source and take alpha
from the source lifetime. Other births select a random source segment, live five
quants, start in phase 1 at scale 0.1, interpolate between the source's previous
and current points, add a +/-0.1 offset, damp that offset and scale by 0.9, and
lose 48 alpha units per update.

The exact Clayring radius sequence is
`0, .025, .056, .092, .136, .188, .251, .326, .417, .525, .656, .812, 1, 1, 1, 1`;
its alpha sequence is
`0, 128, 255, 255, 255, 255, 255, 255, 255, 160, 80, 32, 0, 0, 0, 0`.
Positive authored size indexes the radius table outward as source life falls;
negative size indexes it in the opposite order. Source direction advances each
quant by the binary-confirmed `.95/.31225` rotation and each point is snapped to
terrain height plus one. The flat catalog control therefore uses its authored
origin as the terrain plane and places the ring one unit above it. Generation
stops after the sources pass their final table entry.

Teleport (type 8217) selects factory range `0x004872E5..0x0048745E`, callbacks
`0x0048FDB0`, `0x00490060`, and `0x00490110`, the `Teleport` atlas, ordinary
alpha blending, 20 attempts, capacity 320, 14-quant life and one quant per
phase. Birth radius uses `(u^2 + u) / 2`, followed by a uniform angle. The lower
birth plane is a random -0.4..-0.2 authored-radius offset. Inner births select
phases 4..15 when a second random value exceeds radial fraction; outer births
select phases 0..3, and updates wrap independently within those two phase bands.

For positive authored size, Teleport begins fully opaque, rises by
`radius * .15 * (4 - radialFraction)`, multiplies that height by 1.2 each update,
and subtracts 19 alpha units. Negative size begins transparent, uses
`radius * 2.25 * (4 - radialFraction)`, contracts height by `1/1.2`, and adds 19
alpha units. Visible scale is zero on birth and the last live quant, otherwise
`.2 * (2 - radialFraction)`. Its single local source stops generation after 36
updates. The catalog's positive scale-one record therefore has a complete native
finite path; the editor may repeat it only after the source and population have
finished so the one-shot remains inspectable.

The active JMV archive pins 1024-square DXT3 atlases `protectionf.mmp`,
`clayring.mmp`, and `teleport.mmp` with respective payload SHA-256 values
`BC8A148B9431DBDCE011A0DD800AD317BF2F819E91860F5315EA76AD591A9261`,
`9B62B28521029B2CC4A3F342BC6542168638F75B0E43882136764F7118D04585`,
and `1D6AABCE5725E108CC8A3B03A18B98970624C29F49DA8E51B8A69B44D661921E`.
The Base archive contains separately fingerprinted 256-square fallbacks. The
binary fixture pins both new factories, six callback bodies, texture/callback
selection sites, both Clayring tables, and all scalar constants before code
dispatch is changed.

### Implemented ordered controls

The editor now routes Spherefire through the shared deterministic carrier-shell
simulator with its own atlas and exact 2.0 size factor. Clayring independently
replays the ten source records, source-table progression, anchors, segment
trails, finite generator and additive atlas path. Teleport independently replays
both signs of authored size, its radial/phase partitions, finite generator,
vertical and alpha evolution, and ordinary-alpha atlas path. Clayring uses the
catalog's flat authored origin as its terrain control; no arbitrary terrain
shape is synthesized. Both finite effects repeat only after their native source
and live population have completed. All three preserve the separate cyan editor
selection overlay.

Focused tests cover the Spherefire shell radius/population/phase constraints,
Clayring deterministic finite retirement and preview restart, and Teleport's
positive/negative population, phase, scale, alpha, retirement and restart. The
expanded executable and texture reports remain deterministic fingerprints of
the read-only shipped inputs.

## Ordered catalog reconstruction: Antimagic

Antimagic (type 8218 / `0x201A`) is binary-confirmed as a fourth specialization
of the same carrier-shell factory and callback pair used by Sphereacid,
Sphereelectricity, and Spherefire. It is additive and uses the distinct
`AntiMagic` atlas. The factory retains 90-percent birth probability and the
same shell placement, carrier projection, and 0.01-radian rotation, but selects
24 creation attempts per authored carrier-bound unit, four-quant particle life,
and the six-entry lifetime size table `.01, .015, .02, .03, .015, .01`.
Particles still begin in a random phase 0..10 and advance within the 15-phase
range during their short life. The one-in-eleven large size class is 2.5..3.5;
ordinary births retain the shared 0.7..1.3 class. Unlike the other sphere
specializations, Antimagic applies no additional final type multiplier.

A bare `PARTICL` record has no native carrier bounding volume. As for the three
other catalog shell controls, the editor therefore treats authored scale as an
explicit synthetic stationary carrier bound rather than claiming an ownerless
native default. The adjacent Modifier1..Modifier12 IDs do not use this sphere
path: the top-level dispatch table sends all twelve to a separate `Stun`-atlas,
carrier-dependent local-source family. That relationship is confirmed but is
not used to approximate Antimagic or implemented as part of this control.
The selected JMV texture archive does not override `antimagic.mmp`; the active
resource therefore resolves the Base 256-square, three-mip atlas with payload
SHA-256 `64DF1A10242CAF1A1616E0BCF75666F634A155095BAD727FD3E2312A34C3C30E`.

## Ordered catalog reconstruction: Modifier1 through Modifier12

Types 8219..8230 (`0x201B..0x2026`) all select factory range
`0x0048745F..0x004874B0`, callbacks `0x00490130`, `0x00490990`, and
`0x00490B00`, the `Stun` atlas, additive blending, 80 creation attempts,
capacity 500, nominal life 14, and two quants per phase. The create callback
requires a carrier in the native path. It allocates
`max(1, round(abs(authoredScale) * 10))` local sources and derives their radius
from half the carrier bounding-box diagonal. A standalone catalog record does
not supply either the carrier bounds or its orientation; the editor control
therefore uses absolute authored scale as an explicit synthetic carrier radius
and an identity carrier orientation.

Each source starts on a random direction perpendicular to a subtype axis. For
Modifier1 the axis is world Z. Modifier2 and Modifier3 use world Z and lift the
source center by `.4 * carrierDiameter`; Modifier4 lowers it by the same amount.
Modifier5..Modifier8 use the carrier's rotated local +Y direction (world +Y
for the documented identity synthetic carrier). Modifier9..Modifier12 use no rejection axis and
therefore distribute sources over the sphere. All source centers are additionally
raised by one carrier radius. Positive authored scale starts at full carrier
diameter and contracts direction by `1 / 1.4` per local update. Negative scale
starts at `1 / 23` of that diameter and expands by `1.4`. Sources update ten
times, then clear generation.

The first creation pass occurs before the newly allocated sources have received
a local update. Its first `sourceCount` successful attempts create 25-quant
source anchors in phase 0..11 according to Modifier subtype. Positive anchors
start at scale `.125`, grow by `1.07` per update, and take alpha from
`0,32,64,128,255,255,255,255,255,255,255,128,0`; negative anchors start at
scale `.2`, multiply by `.9345793724`, and use
`0,128,255,255,255,255,255,255,192,128,64,32,0`. Remaining births choose a
random source segment, use phases 12..15, life five, scale `.06`, random
`+/- .04` offsets, then damp offset and scale by `.9` and subtract 48 alpha
units per update. Their birth alpha follows source age through
`0,32,64,128,255,207,159,111,63,15,0`.

The active JMV `stun.mmp` is a 1024-square, 11-mip DXT3 atlas with payload
SHA-256 `2A2084F1DC9A0A2F1946A36F3D7C54D510E3A51BD39B2351EF3354AF83E51170`.
The separately fingerprinted Base fallback is 256-square DXT3. These confirmed
rules replace neither Antimagic nor any generic marker preview.

## Ordered catalog reconstruction: Blood2..4 and A4 blood variants

Blood2, Blood3, and Blood4 (types 8241..8243 / `0x2031..0x2033`) select the
same factory and callback triplet as Blood1. The only factory specialization is
the fixed RainSnow atlas phase: Blood1 uses 12, while Blood2, Blood3, and Blood4
use 13, 14, and 15 respectively. They retain the confirmed ten creation
attempts times the synthetic carrier-derived count, six-quant life, scale and
velocity damping, downward acceleration, alpha table, finite six-update source,
ordinary alpha blending, and capacity 300. A bare catalog record still lacks
the carrier bound used by the native spell path, so authored scale remains the
explicit synthetic bound for all four controls.

A4bloodred, A4bloodgreen, A4bloodblue, and A4bloodblack (types 8244..8247 /
`0x2034..0x2037`) instead select factory range `0x00486E7F..0x004870D1`,
callbacks `0x0048F3B0`, `0x0048F670`, and `0x0048F6F0`, the `RainSnow` atlas,
additive blending, phase 12, 45 creation attempts, capacity 300, six-quant
nominal life field, and a single generation quant. The factory creates four through eight
local radial sources. On their first update, each receives radius `.1..0.3` and
angle `0..6` radians; the next local update clears generation. Births choose a
source, begin at the emitter origin, use scale `.1..0.2`, and receive horizontal
velocity along that source line with signed radial magnitude between minus and
plus half its radius. Vertical acceleration is `-.18..-.06`; downward terminal
velocity is `-.6..-.2`. Updates add velocity, retain particle scale, and clamp
vertical velocity to that terminal value; the update callback does not consume
the nominal life field and particles retire only after falling below world Z zero.

No A4 callback or factory branch inspects which of the four named A4 type IDs
was selected, and the particle starts white. The red/green/blue/black distinction
therefore comes from an external gameplay owner setting particle color, not
from standalone `PARTICL_TYPE` data. The editor's bare catalog controls correctly
remain the same white additive burst; hardcoding four colors from their symbolic
names would contradict the standalone native path.

## Ordered catalog reconstruction: Zoneexit through Moshka

Zoneexit (type 8248 / `0x2038`) selects factory range
`0x0048783B..0x00487887`, callbacks `0x00492D30`, `0x00492F40`, and
`0x00492FB0`, the `ZoneExit` atlas, ordinary alpha blending, ten initial
attempts, capacity 600, fixed power fields five and five, and pause-when-hidden.
Each creation callback replaces the attempt count with
`min(40, round(power * power2))`, so its fixed factory fields produce 25 births
per quant. The authored-scale mutator does not alter either power field. Births
choose a random atlas phase and two independent -1..1 offsets.
Those offsets are transformed by the emitter's two planar direction parameters
and fixed powers. Both direction parameters remain zero in a bare `PARTICL`
record, so its native standalone control is a coincident rising column; a
spread field requires external owner code to supply the planar basis.
Birth size is `(1.9 - x^2 - y^2) * .05 * random(.7, 1.3)`. Particles start at
the terrain surface plus .1, rise .05 per quant, retain that size, advance
within their original four-phase atlas row, and use the exact alpha sequence
`0,80,176,255,255,255,255,255,255,255,255,255,255,255,128,64` over their
15-quant life. The wall-direction branch only changes the birth RGB from white
to pale red; a bare `PARTICL` record supplies no wall direction, so the catalog
control correctly uses white.

Path (type 8249 / `0x2039`) selects factory range
`0x00487888..0x004878CC`, callbacks `0x00492FC0`, `0x00493280`, and
`0x004932F0`, the `ShapeChange` atlas, ordinary alpha blending, 2500 attempts
and capacity 2500. This is not an autonomous placed field: each successful
birth samples a map-object carrier's predicted position at an increasing
future quant, rejects repeated positions, and snaps the point to the bridge
surface plus .15. It uses phase 15, scale .07, and a staggered life computed
from its index and final population. The source disables generation after the
first pass. The constructor's default power value one selects green RGB; zero
and two select gray and orange. Authored scale does not alter this power field;
other values require external owner code, and only exact values zero, one, or
two select those initialized colors.
Because a bare stationary catalog record has no predicted path, the editor may
show only the single native-style green phase-15 point at its authored origin;
it must not fabricate a moving trail or reinterpret authored scale as path
power.

Pathdestination and Pathfailed (types 8250 and 8251 / `0x203A` and `0x203B`)
share factory range `0x004878CD..0x00487A3F`, callbacks `0x00493300`,
`0x004933F0`, and `0x004934F0`, the `ShapeChange` atlas, and ordinary alpha
blending. Pathdestination creates 128 green points on a radius-.1 ring.
Pathfailed creates the same ring plus four 32-point diagonal arms and uses pale
red. All points use phase 15, six-quant life, initial submitted size .07, and
expand their planar offset by 1.05 per update. Their alpha lookup is
`0,64,144,255,255,255,255,255,255,255,255,255,255,255,128,64`.
The native callback also projects each update to terrain/bridge geometry. The
flat catalog control uses its authored Z as the known surface and applies the
confirmed +.1 offset; arbitrary terrain projection remains renderer-context
work rather than a synthetic particle rule.

Moshka (type 8252 / `0x203C`) selects factory range
`0x00487A40..0x00487B93`, callbacks `0x00493560`, `0x00493770`, and
`0x004938C0`, the `Bag` atlas, ordinary alpha blending, 100 attempts, capacity
800, carrier-required generation, and 80 local sources. The first 80 births are
source anchors. Each source repeatedly chooses phase band 0..2, a point within
+/- `.2 * authoredSize` of the carrier, a random-ball direction scaled by
`.2 * authoredSize`, and life 7..9. On later updates it increments the source
phase, has a one-in-six chance to perturb direction by a random-ball vector
scaled by `.05 * authoredSize`, and advances its point by that direction.

The 80 anchors follow their corresponding sources, cycle atlas phases 0..3,
and use submitted size `.15 * authoredSize * .93^sourcePhase`. The ordinary
trail population chooses a source, starts between its current point and one
direction step behind it, uses phases 4 upward, initial life `min(5,
sourceLife + 1)`, and size `.1 * authoredSize * .93^sourcePhase`. Its exact
12-entry RGB gradient runs from `(251,254,143)` through warm pink into
`(239,144,249)`; alpha uses `0,64,112,255` followed by full opacity. Trail
particles only advance phase and alpha before retiring. When the carrier is
removed, sources enter terminal state -13 and their anchors retire. A bare
catalog record has no native carrier object; the editor therefore treats its
authored origin and size as an explicit stationary synthetic carrier solely to
exercise the confirmed swarm visuals.

The active JMV archive pins 1024-square DXT3 atlases `zoneexit.mmp`,
`shapechange.mmp`, and `bag.mmp` with payload SHA-256 values
`29E3C3005E508A6D37C68CCCDBB0036817E1224F4742DF87C924C88BB64A2E80`,
`84BB998BD3FAB3BD72F947FF48A6DC1E06D890402670ED1C488687E4DF8A3666`,
and `C69C26230EAC0830E69E5F40E9748014A6C9A74270BA3410CB80BAE15F6635BE`.
The Base archive contains independently fingerprinted 256-square variants.
These findings are pinned from the shipped executable and selected archives;
they precede renderer dispatch changes.

## Ordered catalog reconstruction: Portalstar through Cylinder2

Portalstar (type 8253 / `0x203D`) selects factory range
`0x00487D03..0x00487D3D`, callbacks `0x00493D10`, `0x00493D50`, and
`0x00493DD0`, the `Casting6` atlas, ordinary alpha blending, one attempt and
capacity one. Its only particle stays at the carrier point in phase zero while
generation is enabled. Birth scale and alpha are zero; every update makes it
fully opaque and chooses submitted size `.8..1.2 * authoredSize`. A bare placed
record therefore has a complete persistent standalone path at its authored
origin and needs no invented owner state.

Portal (type 8254 / `0x203E`) selects factory range
`0x00487B94..0x00487D02`, callbacks `0x00494470`, `0x00494770`, and
`0x00494930`, the `Teleport` atlas, additive blending, 50 initial attempts,
capacity 800, nominal life ten, and three quants per atlas phase. Its first
particle is a persistent phase-15 portal head at `origin.z + 6.4`; when authored
size is at least one, every update gives that head a random submitted size
`1.2..1.8`, otherwise it remains hidden. Other births use fixed radius 2.5 and
radial fraction `(u^2 + u) / 2`. They choose phases 4..14 inside the fraction
test and phases 0..3 outside it, rotate by positive or negative .1 radians per
update, and advance within their phase band every three quants.

Portal trail submitted size is `.3 * (2 - radialFraction)`. Its initial vertical
step is `.0375 * (4 - radialFraction)`, its birth Z receives a random
`-.5..+.5` fraction of that step, and each update adds `step * authoredSize`
before multiplying the step by 1.03. Alpha is the clamped quarter-scale value
derived from remaining height and never exceeds 64/255. A trail reaching eight
units above the carrier becomes transparent and retires on the next update.
The local source replaces subsequent creation attempts with
`int(10 * authoredSize)`. The fixed-radius horizontal shape is intentionally
not rescaled: the native authored-scale mutator changes vertical motion and
cadence through `fpSize`, not the callback's fixed 2.5 radius.

Cylinder1 and Cylinder2 (types 8255 and 8256 / `0x203F` and `0x2040`) share
factory range `0x00487D3E..0x00487EE9`, callbacks `0x00493DE0`, `0x00494170`,
and `0x00494340`, ordinary alpha blending, 30 initial attempts, capacity 600,
one quant per phase, two carrier-required local sources, and the exact alpha
table `0,32,64,128` followed by full opacity. Cylinder1 uses `Casting2`;
Cylinder2 uses `Casting1` and reverses both confirmed rotation signs. A bare
record has no native carrier bounds, so the flat catalog uses authored scale as
an explicit synthetic spherical carrier radius and identity orientation.

The two sources begin on opposite points of a `.7 * carrierRadius` circle at
height `.2 * carrierRadius`. Their initial orbit step is
`2*pi/15 * .3`, their life is reset from the factory placeholder 100 to 18,
and their initial vertical increment is
`carrierRadius * (2/18) * .04`. Every update rotates and raises the source,
multiplies vertical increment by 1.25, and rotates the stored orbit basis by
`2*pi/15 * .1` in the opposite sense. Source zero reduces the creation-attempt
count once every update and once more on odd source life; generation stops when
a source update observes life zero.

The first two particles are source anchors. They follow their source at
submitted size .3 and use source-life alpha. Later births require nonzero source
motion, begin on a random point from one source step behind through its current
point, use phase one, submitted size .1, and life `min(14, sourceLife + 6)`.
Every update advances phase, moves by the stored source velocity, renormalizes
the horizontal point back to the `.7 * radius` circle, replaces velocity with a
.6-scaled tangent (and .6-scaled vertical component), and multiplies submitted
size by .9.

The active JMV archive pins `casting6.mmp`, `casting2.mmp`, and `casting1.mmp`
as 1024-square, 11-mip DXT3 atlases with payload SHA-256 values
`9B62B28521029B2CC4A3F342BC6542168638F75B0E43882136764F7118D04585`,
`ABA7C36565888863AEED4C1AF0295C9A21486FBDB6F9F61272A555F37436E3CC`,
and `FAFC10F85C3458A96BB74F701183BDF7801017FA5F385C58AD1B1AC24A65C613`.
The Base archive contains separately fingerprinted 256-square variants. The
Portal texture was already fingerprinted for Teleport. These findings precede
renderer changes and distinguish the ordinary Portalstar/Cylinders from the
additive Portal.

## Ordered catalog reconstruction: Firestar through Visionstar4

Firestar and Acidstar (types 8257 and 8258 / `0x2041` and `0x2042`) share
factory range `0x00487EEA..0x00487F97` and callbacks `0x00494950`,
`0x004949B0`, and `0x00494A00`. They require a body carrier in gameplay, use
one creation attempt, ordinary alpha blending, and stop generation in their
first successful birth. Both select phase 15. Firestar uses
`FireArrowSmoke`; Acidstar uses `AcidRay`. The particle is born transparent
at submitted size zero with a three-quant life and the authored size stored as
its pending size. Its first two updates make it opaque at authored size and
`.9 * authoredSize`; the third update retires it. A bare catalog record has no
native carrier, so the editor uses its authored origin and size as an explicit
synthetic stationary carrier and repeats the finite flash for inspection.

Sparks (type 8259 / `0x2043`) selects factory range
`0x00487F98..0x00487FEB`, callbacks `0x00494A10`, `0x00494CD0`, and
`0x00494DF0`, the `Casting6` atlas, additive blending, ten creation attempts,
and capacity 300. It also requires a gameplay body carrier. The first birth is
a phase-zero source flash. It begins at size zero; its first update assigns
`.55 * authoredSize * random(.8,1.2)`, and the following update retires it.
The other nine births in that first scheduler pass disable further generation.
Each chooses phase 1..3, a normalized random-sphere direction, speed
`random(.8,1.4)^2 * authoredSize * .2`, a random point from the carrier origin
through one full speed step, submitted size
`.2 * authoredSize * random(.8,1.2)`, and life six. Each update decrements
life, looks up alpha from `0,64,128,255,255,255,255,255`, advances position,
increments phase, multiplies size by .9, and multiplies velocity by .6; it
retires once life becomes negative. The editor supplies the same explicit
synthetic stationary carrier used for other owner-required catalog controls
and repeats this finite burst.

Visionstar1, Visionstar2, Visionstar3, and Visionstar4 (types 8260, 8261,
8262, and 8268 / `0x2044`, `0x2045`, `0x2046`, and `0x204C`) share factory
range `0x00487FEC..0x00488089` and callbacks `0x00494E00`, `0x00494F30`,
and `0x004950A0`. They use the additive `Stun` atlas, three attempts per
quant, and bind above a gameplay carrier's head. Their fixed atlas phases are
respectively 0, 1, 2, and 3. Visionstar1, Visionstar3, and Visionstar4 permit
one persistent particle at the carrier point; Visionstar4 additionally adds
.7 to Z. Visionstar2 permits five particles on a radius-.5 pentagon and
rotates their stored offsets by .07 radians per update. The three-attempt
scheduler fills three points in the first quant and the remaining two in the
second.

Every Visionstar update makes the particle opaque, resnaps it to the current
carrier point and optional offset, and smooths submitted size as
`oldSize * .95 + authoredSize * coefficient * random(.1,1.9) * .05`, where
the coefficient is .17 for Visionstar2 and .3 for the other variants. When
generation or the carrier disappears, the native path instead decays both the
particle and system size until submitted size falls below .03. A bare placed
record cannot supply the required carrier object; the editor explicitly treats
its authored origin and size as a stationary synthetic carrier so the
confirmed persistent visuals remain inspectable.

The selected JMV archive fingerprints `firearrowsmoke.mmp`, `acidray.mmp`,
`casting6.mmp`, and `stun.mmp` payloads as
`9624B8F3F769DDAD435F69D0D780BBD1F5EA7EA50DA1019E0484836FCD41466B`,
`48C7F220718AE66AD98C17B6C53C3230E2F9ED7C2FB88320809E19650F0A2ADE`,
`9B62B28521029B2CC4A3F342BC6542168638F75B0E43882136764F7118D04585`,
and `2A2084F1DC9A0A2F1946A36F3D7C54D510E3A51BD39B2351EF3354AF83E51170`.
Their dimensions and formats are already pinned in the deterministic texture
fixture. These binary/resource findings precede simulator and renderer changes.

## Ordered catalog reconstruction: Regeneration through Feetcloud2

Regeneration (type 8263 / `0x2047`) selects factory range
`0x0048808A..0x004880D3`, callbacks `0x004950B0`, `0x00495120`, and
`0x00495310`, the additive `Stun` atlas, two creation attempts, and a gameplay
body carrier. The creation callback admits exactly two particles. Both begin
at the carrier point in phase three with zero submitted size; their stored
signs are -1 and +1. With a carrier, each update targets the carrier point plus
the carrier-local X axis times `sign * .65 * carrierRadius`. Position is
smoothed as `old * .7 + target * .3`, with an additional `.4 * authoredSize`
on Z. Submitted size is
`.25 * authoredSize * random(.8,1.2)`, and alpha is fully opaque. When the
carrier or generation is absent, system size is multiplied by .75 and a
particle retires below .02. A bare catalog record has no carrier transform or
bounds, so the editor uses its origin, identity X axis, and authored size as a
stationary synthetic carrier radius.

Silence (type 8264 / `0x2048`) selects factory range
`0x004880D4..0x00488127`, callbacks `0x00495320`, `0x004955D0`, and
`0x004957D0`, the additive `Stun` atlas, four creation attempts, capacity 300,
and a gameplay body carrier. Its first two births are persistent phase-six
anchors at the carrier point. Their initial angles are pi and zero, their
horizontal steps are .1 radians, their vertical-angle steps are .12 radians,
and their initial submitted size is zero. Each update places an anchor on a
normalized direction `(cos(a), sin(a), .5*sin(verticalAngle))` at
`.5 * carrierRadius`, then adds another `.5 * distance` to Z. The stored
velocity is the new position minus the old position. Anchor size is
`.22 * authoredSize * random(.9,1.1)`, with full opacity.

Once both Silence anchors exist, later births select either anchor, interpolate
from one previous velocity step through its current point, and add a random
unit-ball displacement scaled by `.8 * anchorSize`. A trail uses phase six,
life seven, zero initial submitted size, and pending size
`.08 * authoredSize * random(.8,1.2)`. Each update decrements life, copies the
pending size to submitted size, and multiplies the pending size by .8; it
retires when life reaches zero. On detach, the system size is multiplied by
.85 and anchors retire below .02. The catalog supplies origin, identity
orientation, and authored size as the stationary synthetic carrier.

Feeblemind (type 8265 / `0x2049`) selects factory range
`0x00488128..0x0048817B`, callbacks `0x004957E0`, `0x00495950`, and
`0x00495A20`, the additive `Link` atlas, five initial creation attempts,
capacity 300, and a gameplay head carrier. Each birth replaces following
attempts with `round(2 * authoredSize)`, chooses phase 0..15, and begins on a
radius-.05 horizontal ring at the carrier point. It starts transparent with
life 20 and target size
`.035 * authoredSize * random(.9,1.1)`. Every update decrements life and uses
the exact alpha ramp `0,32,64,128` followed by full opacity through life 20.
The stored radial offset is removed, rotated by .07 radians, expanded by 1.1,
and added back; Z rises by .05, phase advances modulo 16, submitted size takes
the target size, and the next target is multiplied by 1.04. It retires after
life becomes negative. The standalone editor control uses the authored origin
and size as a stationary synthetic head carrier.

Feetcloud1 and Feetcloud2 (types 8266 and 8267 / `0x204A` and `0x204B`) share
factory range `0x0048817C..0x0048836E`, callbacks `0x00495A30`,
`0x00495C80`, and `0x00495E00`, the additive `VisionFog` atlas, eight initial
creation attempts, capacity 300, a required gameplay carrier, and two local
sources. Gameplay updates those sources from the left and right foot/leg
attachment points when available and otherwise uses the carrier point. A bare
record has neither body-part attachment points nor bounds; its two synthetic
sources therefore coincide at the authored origin and use authored size as
their radius.

Each Feetcloud birth replaces following attempts with
`round(8 * authoredSize)`, randomly chooses one source and phase 0..15, and
starts at that source with zero submitted size. Velocity is a random point in
the unit disk scaled by `.1 * sourceRadius * authoredSize`. Pending size is
`.4 * sourceRadius * authoredSize`, blend is one, accumulated offset is zero,
Z receives `1.2 * pendingSize`, and life is ten. Each update decrements life,
uses alpha `0,32,64,128,255,255,255,255,255,255,255`, accumulates velocity,
and blends from the old position to `sourcePoint + accumulatedOffset`. It then
copies pending size to submitted size and multiplies pending size by .8, blend
by .98, and velocity by .9. On detach, remaining life is capped at eight.
Feetcloud1 uses packed color `0x000000FF` (blue in the native ARGB value) and
Feetcloud2 uses `0x00FF0000` (red).

The fingerprinted executable stores the selected atlas name as `VisionFog`.
The active JMV archive pins `visionfog.mmp` as a 1024-square, 11-mip DXT3 atlas
with payload SHA-256
`C0A34776BB04D999BE9DB33908C734D6DE637D75775A7A6722A4E2921B0D2575`.
The Base archive contains a separately fingerprinted 256-square, three-mip
`0x00004444` variant with payload SHA-256
`0D7A9F860ED7716F47BD013DC4B4B9B41EB24CEEBD8EDA15A63C256D29BA4A47`.
All behavior and resources above were confirmed before renderer wiring.

## Ordered catalog reconstruction: Ballofstars through Transform

The final six catalog IDs are three dedicated families and three exact factory
reuses. The fingerprinted executable maps Ballofstars, Cursestars, and
Starttrans (types 8269, 8271, and 8273 / `0x204D`, `0x204F`, and `0x2051`) to
factory ranges `0x0048836F..0x004883C2`, `0x004883C3..0x00488416`, and
`0x00488417..0x004885B3`. Their callback triplets are respectively
`0x00495FB0` / `0x004961B0` / `0x00496260`, `0x00496270` /
`0x004964B0` / `0x004965D0`, and `0x004965E0` / `0x00496840` /
`0x00496990`. This rejects the former generic colored-circle previews.

Ballofstars uses the additive `Stun` atlas, phase three, one default creation
attempt, and capacity 300. A birth chooses a point from the unit ball, scales
it by `.8 * carrierRadius`, and adds it to the carrier point. It begins
transparent with zero submitted size, stores
`random(.7,1.3)` as its size multiplier, and lives 20 quants. Each update
decrements life, makes the particle opaque, and submits
`authoredSize * storedMultiplier * random(.8,1.2)`; it retires once life
becomes negative. The gameplay run-pose specialization changes the system to
four attempts and life five, but that condition is not present on a bare map
particle. The editor therefore uses the placed origin and authored size as a
stationary synthetic carrier and retains the confirmed default path.

Rickarrow (type 8270 / `0x204E`) enters the FireArrow factory range
`0x00485803..0x004858E2`. The type comparison at `0x00485872` selects
`RikArrowSmoke`, while callbacks `0x0048CBA0` and `0x0048CED0`, capacity,
lifetime, phases, and ordinary-alpha behavior are identical to FireArrow.
Rickarrow must consequently reuse the FireArrow simulator with its own atlas;
it is not a separately tuned trail.

Cursestars uses the additive `Healing` atlas, eight creation attempts per
quant, capacity 300, and a gameplay body carrier. Each birth selects phase
0..15 and a random unit-ball displacement scaled by authored size. It starts
at `carrierPoint + displacement`, with life 20, zero submitted size, and
velocity equal to the normalized displacement times `.1 * authoredSize`.
Every update decrements life and applies the exact alpha ramp
`0,32,64,128,255` followed by full opacity through life 20. A zero submitted
size is replaced by `.04 * authoredSize * random(.8,1.2)`; subsequent updates
multiply size by .95. Position advances by velocity and velocity is multiplied
by .9. The particle retires once life becomes negative. The catalog control
supplies the placed origin and authored size as the stationary carrier.

Curseholder (type 8272 / `0x2050`) selects factory range
`0x004867A0..0x00486992` and reuses the Tornado callbacks and `Tornado` atlas.
It retains four attempts, capacity 800, ordinary alpha, and six local sources,
but deliberately changes three constructor controls: the active source-spine
height is 4.8 instead of 12, carrier-motion bending is zero instead of .5,
and spiral-radius retention is .997 instead of .99. Its packed initial color
is fixed `0x19000000`, rather than the normal Tornado's captured sunlight
color. Thus the stationary catalog form is a shorter, denser, dark tornado;
reusing the ordinary Tornado parameters is explicitly rejected.

Starttrans uses ordinary-alpha `ZoneExit`, one initial attempt, capacity 300,
and one local source initialized with life 30, radius zero, and attempt
accumulator one. The first birth is a special phase-12 carrier anchor with life
four. While attached it is opaque and its size is the expanding source radius
times `random(.8,1.2)`. Later births live 15 quants, select phases 0..11, and
start at the carrier plus a random unit direction scaled by authored size.
Their velocity points back toward the carrier and is the negative displacement
divided by 18; pending size is `.05 * authoredSize`. Updates decrement life,
advance the phase modulo four without leaving its atlas row, move by velocity,
copy pending size to submitted size, and apply the exact 16-entry alpha ramp
`0,80,176,255,255,255,255,255,255,255,255,255,255,255,128,64`.
The local-source callback decrements its 30-quant life, grows radius by
`.02 * authoredSize`, adds .4 to the attempt accumulator, rounds and caps the
active attempts at eight, then stops generation after expiry. The editor may
repeat the complete finite cycle for catalog inspection.

Transform (type 8274 / `0x2052`) enters the same factory range and callbacks
as Mushroom: `0x00486BD5..0x00486C42`, `0x004889D0`, `0x00488DB0`, and
`0x00488F00`. The type branch selects `ChangeHero`, ten creation attempts,
capacity 4000, and the already reconstructed finite Mushroom source law.
Transform therefore reuses that simulator and atlas exactly; a second inferred
animation is neither present nor needed.

The active JMV texture archive independently pins `healing.mmp` and
`rikarrowsmoke.mmp` payloads as
`D417D779BF68C551EFDEAEE5010C7D9BBA4627E746AAA6CD897EFBB489078968`
and `8360A32C7D862DA20C6892D815E2CF9C96EC5EDCCCA88A42237844B69316242F`.
Both are 1024-square, 11-mip DXT3 atlases. The Base archive contains
256-square, three-mip variants with payloads
`A39229D751FD037648DF96B9DCF3D19A83C9D39796EF70F3C0B96A523FE03FB8`
and `1487BD789EDD1B8D01310FF0EFA14E3FE086D5CE1ABB00E84CEA8384DC1DFEB9`.
`Stun`, `Tornado`, `ZoneExit`, and `ChangeHero` were already fingerprinted;
the selected archives confirm all six batch dependencies in both profiles.
All findings above precede simulator and renderer changes.

## Early spell reconstruction: Fireball through Geyser

The fingerprinted executable closes six formerly preview-only factory paths.
Fireball (8192 / `0x2000`) uses factory range
`0x00485735..0x00485802`, callbacks `0x0048C800` and `0x0048CAD0`, the
`FireSmoke` atlas, ordinary alpha, 88 percent birth probability, one minimum
attempt, life and phase count 15, one-quant phases, factory size
`.8 * authoredScale`, and size factor .94. Attempt zero is a
`1.2 * factorySize` carrier-point head; later attempts are
`.5 * factorySize` trail sprites placed from carrier velocity
and its two transverse basis vectors. On its first update the head is halved.
Every update advances the atlas phase, applies the .94 size factor, removes
then reapplies the stored transverse offset for trails, and multiplies that
offset by .95. A bare catalog record has zero carrier velocity, so the editor
must truthfully exercise the stationary head path rather than inventing a
projectile trail.

Fireblast (8194 / `0x2002`) owns factory range
`0x00485D58..0x004861D7`, callbacks `0x0048C090`, `0x0048C590`, and
`0x0048C760`, and the ordinary-alpha `Explosion` atlas. This corrects the
earlier naming ambiguity around callbacks also reused by Mushroom and
Transform: Fireblast is the full 15-source explosion specialization, whereas
those later types select the same callback family with different constructor
state and `ChangeHero`. Fireblast creates 15 radial sources, 60 particles per
quant, capacity 600, life 15, source random speed scaled by .8..1.2 and .8,
source growth controls `(0,.4,.7)`, and per-source class controls 0..5 and
.7..1.4. Source updates expand the shell, damp velocity by .75, subtract .03
from vertical speed, and stop generation once phase exceeds six. Births split
between central ball clouds and source trails; their exact phase, size,
velocity damping, and the 16-entry alpha table
`255,255,207,175,128,128,128,128,128,175,207,192,128,48,0,0` are pinned by
the callback bytes. The editor may repeat the completed one-shot solely for
catalog inspection.

Healing (8198 / `0x2006`) uses factory range
`0x004862B8..0x00486318`, callbacks `0x0048B630`, `0x0048BBE0`, and
`0x0048BCE0`, additive `Healing`, life eight, and a required gameplay carrier.
At first creation it derives
`round(6 * (.5 * authoredScale) + 2)` spiral sources from the carrier bound.
Sources start around the lower bound, expand then contract
radially over 36 quants while turning ten degrees and rising each quant.
One phase-0 anchor follows every source, cycles phases 0..7, grows for the
first half and shrinks for the second half, then fades over eight quants.
Additional eight-quant tail particles occupy phases 8..15 around the moving
sources. The standalone control explicitly uses authored size as a synthetic
spherical carrier bound; this is the same documented convention used for
other carrier-required catalog effects.

Poisonfog and Aggressionfog (8199 and 8200 / `0x2007` and `0x2008`) share
factory range `0x00486319..0x00486371`, callbacks `0x0048B080` and
`0x0048B570`, ordinary-alpha `PoisonCloud`, 50 percent probability, and one
creation attempt. Their first callback fills a one-unit grid inside the
authored radius, jitters X/Y by plus or minus .25, assigns size
`1 + random(-.25,.25)`, and places Z one particle size above the sampled
surface. Phases begin randomly in 0..14 and advance every quant. Poisonfog is
white with alpha 160; its independent power field remains at the constructor
default and is not changed by `PARTICL_SCALE`. Aggressionfog is
opaque `(255,175,127)`. They remain persistent while generation is enabled;
the callback's nine-alpha-step finish fade is therefore not active on a bare
placed control.

Geyser (8201 / `0x2009`) uses factory range
`0x00486372..0x00486573`, callbacks `0x0048A8F0`, `0x0048ADA0`, and
`0x0048AEC0`, additive `Fireworks`, 88 percent probability, 20 attempts, and
three alternating local sources. The first particle is a persistent phase-15
source flash whose size is rerolled from `.03..3 * authoredSize` each quant.
Active sources emit phases 0..11 with `.2 * authoredSize` pending size,
direction selected from source azimuth, 0..10 degree polar angle, radius
.4..1, and speed `.3 * authoredScale`. Each update rotates within the particle's four-frame
atlas row, multiplies size by .9, applies motion and `.3 * .06` downward
acceleration, and subtracts nine alpha. Sources work for a random 1..7 quants,
relax for 3..36, then choose a new direction. The stationary placed-record
path supplies the native origin and needs no invented owner motion.

The same pass independently closes Vulcansmoke's constructor and callbacks:
factory `0x004861D8..0x004862B7`, creation `0x0048BD50`, and update
`0x0048C010`. Its 60-percent upper-cloud and 40-percent rising-pillar split,
phase delays, velocities, scale multipliers, and `Vulkan` resource name are
confirmed. However, exhaustive read-only member scans of the selected Base
and JMV resource sets find no `vulkan.mmp` payload. A second installation-wide
scan on 2026-07-25 parsed all 259 actual `.res` archives and all 208 `.mpr`
archives beneath the read-only game installation without parser failures. It
found no member containing either `vulkan` or `vulcan`, and the loose-file
inventory found no matching filename. A borrowed atlas would be fabricated
evidence, so texture fidelity remains explicitly unresolved even though the
behavior is now closed.

The active JMV archive pins `explosion.mmp`, `poisoncloud.mmp`, and
`fireworks.mmp` as 1024-square, 11-mip DXT3 atlases with payload SHA-256
`AF3356FCA6CE0B1B997E284E5DF757EE9B48A56A3EFA257AB6DC356ED68C2AA4`,
`FCE07B6BABFFC1B03D29D99344723797B9B87E9C208A625C908BE3EA22177690`,
and `F3A1D2A98A8BB87D1FE4FAD675904F0636F527CF107D46122134F2ABACE2B`.
The Base variants are 256-square, three-mip `0x00004444` textures with hashes
`241AAF55FD59944E7FC4214A0652F12CEEC348A8B90D1691C3FED95104420276`,
`308D208BB558461FE2DD2D737749A122C6E2C987A2886A7EEAF2B913DE30F8D0`,
and `8BF4915BD31A78854ABB98213F584E6436E767103E463F4954C57E95F7E92DDF`.
`FireSmoke` and `Healing` were already fingerprinted. These executable and
resource findings precede the simulator and renderer changes below.

## Casting specialization reconstruction

The fingerprinted executable maps the nine remaining preview-only casting
types to three separate callback families rather than to a recolored generic
effect. Castingillusion, Castingdomination, Castingenchantment, and
Castinghealing (8235--8238 / `0x202B`--`0x202E`) use factory range
`0x004874B1..0x004875D0`, callbacks `0x00490C10`, `0x00491230`, and
`0x00491390`, ordinary alpha, ten attempts, capacity 600, and a required
carrier. Their atlases and source counts are type-specific: Illusion uses
`Casting2` with two orbiting sources, Domination uses `Casting3` with one,
Enchantment uses `Casting6` with two, and Healing uses `Casting1` with one.

Each source begins on a half-radius horizontal ring. Illusion and Enchantment
rotate by .3 radians per quant at .4 carrier-radius height; Domination rotates
by .5 at 1.6 radius height and emits downward; Healing rotates by .5 at .4
radius height and emits upward. Each source owns a persistent phase-zero
anchor whose submitted size is `.4 * authoredScale * random(.8,1.2)`. Later
particles choose a source, interpolate between its previous and current point,
start at phase one with 28-quant life, rise with an initial vertical step
`.03 * carrierRadius * sign * random(.85,1.2)` that grows by 1.03, contract
their submitted size by .97, and cycle phases 12--15 after reaching phase 16.
The exact seven-entry detach-alpha table is pinned in the fixture. A standalone
catalog record uses its authored origin and scale as a stationary synthetic
carrier bound.

Castingfailed (8239 / `0x202F`) uses factory range
`0x004875D1..0x0048761E`, callbacks `0x00491420`, `0x00491830`, and
`0x004918D0`, ordinary-alpha `Teleport`, 25 attempts, and 13-quant births.
Particles begin around a radius-.9..1.1 horizontal ring, use phase one,
rise by `.1 * (2 * carrierRadius) * random(.7,1.3)` with 1.05 acceleration,
contract by .97, advance one phase per quant, and follow the carrier origin.
The exact twelve-entry alpha table is pinned. The standalone control again
uses authored size as the synthetic carrier radius.

Castingfire, Castingelectricity, Castingacid, and Castingdivination
(8231--8234 / `0x2027`--`0x202A`) use factory range
`0x0048761F..0x00487745` and callbacks `0x004918E0`, `0x00492030`, and
`0x004921D0`. Fire, Electricity, and Acid use 20 attempts, life five local
source populations, ordinary alpha, and respectively `FireSmoke`, `Casting8`,
and `Casting7`; Fire and Acid hold a phase for five quants while Electricity
advances every quant. Divination uses six attempts, five sources, additive
`Casting4`, and a persistent central phase-zero flash.

All gather-family births start at 60 percent of their synthetic carrier
radius and contract inward over 13 updates (26 for Divination), with phase 14,
life 13, and normalized directions perturbed by five wandering ten-unit local
sources. Non-Divination size is `.08 * radius * random(.6,1.4)` and grows by
1.07; Divination uses .07 and 1.05. The exact global-fade and two 15-entry
alpha tables are fixture-pinned. Fire's callback compensates every fourth
life step so its five-quant atlas timing remains exact.

The selected JMV archive independently pins `casting3.mmp`, `casting4.mmp`,
`casting7.mmp`, and `casting8.mmp` as 1024-square, 11-mip DXT3 atlases with
payload hashes `D1A3DEA5E732233B9E62F7FEDC391AE6588AD854B27350F42DBB9FBF2D6E4319`,
`99E7A0D94E00F5F11E4699F9EC8A019BB8A834C18F06448DD3A70AFF0007A3D5`,
`86B7483B09CDDDC059A465CA732B25F68F6D9073090B3D300C4E555DF1E18A86`,
and `7948829AE6636BCA5557A960B25C71D3E252372F38D6CD8C820F302F4C6BEAA4`.
The Base variants are 256-square, three-mip `0x00004444` textures with hashes
`6D0ED48BE10BFC981F50BD4693B74EC2A3C7DE80955283595C7DD3C154456FDA`,
`8D62D43F6F5194FFFD844133D11B7BCAFFADEEBE9BB935AFD6A2F3DED47E7DE5`,
`EDF9DE998B90AF338E00AEA294CB3AFADD34E03A139C4F513C8DE9D2FB727B37`,
and `B412D2B1E2F7D6D0269359CDAD048D9623469DD58E83ADC246F2C50B0EE79108`.
All executable and resource evidence above precedes implementation.
