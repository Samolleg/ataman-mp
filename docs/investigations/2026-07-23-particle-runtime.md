# Evil Islands particle runtime evidence

Last updated: 2026-07-23

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
It contains four torch records and eight particle-source records. The complete
records are reproducibly emitted by `scripts/particle_mob_report.py`.

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

The class contains no second particle-effect pointer or second factory call in
this path. Method `0x52D3F0` then invokes that emitter's vtable slot `+0x18`
exactly 60 times, prewarming the effect before its first ordinary render. This
class-specific evidence, rather than the rejected global lookup
interpretation, establishes that a torch's core fire, peripheral flame, and
smoke atlas stages are one emitter population.

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

The string `TendriSmoke` is at VA `0x78AEA4`. Its factory path reaches the constructor at `0x485B72`. Nearby registered particle families include Banshee, Tornado, Fireworks, PoisonCloud, Healing, Vulkan, Explosion, Stench, Link, and AcidRay.

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
the runtime late-life motion values. The scale mutator at
`0x485480` multiplies `+0x128` along with the spawn bounds, initial scale,
vertical velocity, and displacement threshold. Field `+0x150 = 0` makes the
Campfire family select render state 5/value 6, not the value-2 path used when
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
    four scheduler-refreshed globals described above; their higher-level engine
    semantics remain unlabelled.

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

## Current editor mismatch

The current implementation is still partly inferred:

- `source/render/particle_animation.h` creates six deterministic slots staggered over 32 ticks, with fire frames 0–7 and smoke frames 8–14.
- `source/render/view.cpp` function `drawFireSmoke` uses invented rise, scale, alpha, tint, and noise curves.
- These approximations explain why the editor resembles fire but does not match the game's dense core, secondary flame sprites, or expanding orange-to-gray smoke column.
- Do not tune these constants further by eye. Replace them only with resource and executable evidence.

## Open questions, in priority order

1. Label the higher-level engine meaning and update provenance of globals
   `0x7AF5DC..0x7AF5E8`, which the scheduler copies into ordinary TendriSmoke
   `+0x140..+0x14C`; the exact consumers/formula are now established. Across
   other factories, separately label `+0x128` and the type-`0x2010`-only
   `+0x130..+0x138` vector without applying them to ordinary Campfire.
2. Verify native OpenGL and ANGLE blend/color behavior from identical authored records.

## Required implementation discipline

- Add a repeatable extractor/report before changing renderer behavior.
- Keep extracted field provenance, executable fingerprint, and source resource path in every report.
- Add fixtures for at least `gz1g` campfire, a torch with built-in particle/sound, and `gz10g` lava.
- Preserve the editor-only particle marker independently of gameplay sprites; the marker remains selectable and is not part of the effect.
- Validate native OpenGL and ANGLE separately.
- Update this ledger with each confirmed finding before implementing it, so context compaction cannot reset the investigation.
