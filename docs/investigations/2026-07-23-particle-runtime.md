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

All virtual addresses below apply only to this fingerprint.

## Confirmed authored objects

- `CParticleSource` parsing is in `source/objects/particle.cpp` and `.h`; it retains the authored particle kind/type and scale.
- `CTorch` parsing is in `source/objects/torch.cpp` and `.h`; a torch is an ordinary object mesh with a particle attachment point, strength, and sound. Its visible object surfaces are not particle sprites.
- The current resource-name mapping includes:
  - `8193` — Campfire
  - `8196` — Smoke
  - `8197` — Vulcansmoke
- Exact emitter records from representative `gz1g` fire and `gz10g` lava sources still need to be dumped and compared. Do not replace authored fields with renderer presets until this is complete.

## TendriSmoke factory and layout

The string `TendriSmoke` is at VA `0x78AEA4`. Its factory path reaches the constructor at `0x485B72`. Nearby registered particle families include Banshee, Tornado, Fireworks, PoisonCloud, Healing, Vulkan, Explosion, Stench, Link, and AcidRay.

Confirmed constructor values:

| Emitter offset | Value | Established role |
| --- | ---: | --- |
| `+0xF0..+0xF8` | `(-0.2, -0.2, 0)` | lower randomized spawn bounds |
| `+0xFC..+0x104` | `(+0.2, +0.2, 0)` | upper randomized spawn bounds |
| `+0xCC` | `0x58` | particle-record size, 88 bytes |
| `+0xD0` | `6` | pool/emission-related count; exact scheduler meaning remains open |
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

The wrapper at `0x48ED60` calls this spawn callback and applies another randomized offset from emitter fields `+0x130/+0x134`, invokes world-height query `0x564070`, and then adjusts Z relative to emitter field `+0xA0`. The precise authored meaning of those fields is not yet established.

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
10. For frames 7 and above, scale grows by the constant referenced at `0x73CE38` (currently interpreted as approximately `1.06`; its exact float must be recorded before treating that number as final), and movement incorporates emitter orientation fields `+0x140/+0x144/+0x148` plus fields around `+0x14C`.

## Pool and render path

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
- Emitter `+0x150` selects blend state: value 1 drives state 5/value 2; value 0 returns state 5/value 6 after that path.

## Rejected hypotheses and corrected labels

- `0x485380` is an active-list clear/destructor path, not the emission scheduler.
- `0x4976B0` is render preparation/visibility/vertex allocation, not the emission scheduler.
- A prior label of these functions as the scheduler was incorrect and must not be propagated.
- Visual descriptions and captured GIFs are validation targets and search pointers only. They do not establish density, lifetime, blend, color, or movement rules.

## Current editor mismatch

The current implementation is still partly inferred:

- `source/render/particle_animation.h` creates six deterministic slots staggered over 32 ticks, with fire frames 0–7 and smoke frames 8–14.
- `source/render/view.cpp` function `drawFireSmoke` uses invented rise, scale, alpha, tint, and noise curves.
- These approximations explain why the editor resembles fire but does not match the game's dense core, secondary flame sprites, or expanding orange-to-gray smoke column.
- Do not tune these constants further by eye. Replace them only with resource and executable evidence.

## Open questions, in priority order

1. Locate the function that invokes callbacks at `+0x1E0/+0x1E4` and establish exact allocation/emission cadence, count limits, and reuse rules.
2. Dump the complete authored particle and torch records for representative `gz1g` campfires and `gz10g` lava, including scale, strength, attachment transform, kind/type, and any flags.
3. Establish exact meanings of emitter fields `+0xD0`, `+0xD4`, `+0xD8`, `+0x128`, `+0x130`, `+0x134`, `+0x140..+0x14C`, and `+0x150` across multiple factories.
4. Record exact float constants referenced by the update and wrapper paths.
5. Determine whether fire core, peripheral flames, and smoke are one state machine over atlas frames or multiple co-located emitters for each authored source.
6. Establish the texture-atlas orientation and frame ordering from decoded source data. The current editor presentation appears vertically inverted relative to the game.
7. Verify native OpenGL and ANGLE blend/color behavior from identical authored records.

## Required implementation discipline

- Add a repeatable extractor/report before changing renderer behavior.
- Keep extracted field provenance, executable fingerprint, and source resource path in every report.
- Add fixtures for at least `gz1g` campfire, a torch with built-in particle/sound, and `gz10g` lava.
- Preserve the editor-only particle marker independently of gameplay sprites; the marker remains selectable and is not part of the effect.
- Validate native OpenGL and ANGLE separately.
- Update this ledger with each confirmed finding before implementing it, so context compaction cannot reset the investigation.
