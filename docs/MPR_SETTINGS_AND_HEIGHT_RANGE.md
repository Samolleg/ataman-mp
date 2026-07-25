# MPR settings and height-range operations

## Purpose

The user-facing **MPR Settings** workflow must distinguish ordinary terrain
height edits from changes to the MPR's vertical scale and from structural map
resizing. They have different preservation and validation requirements and
must not be presented as interchangeable numeric fields.

## Fixed terrain geometry

Each SEC owns a fixed `33 x 33` vertex lattice and advances the canonical map
lattice by 32 vertices in each direction. Adjacent sectors therefore share
their boundary vertices. For an MPR containing `X x Y` sectors, the canonical
unique lattice is:

```text
vertex width  = 32 * X + 1
vertex height = 32 * Y + 1
```

The vertex count is fixed while the sector dimensions remain unchanged.
Raise/lower, flatten, clamp, offset, smoothing, heightmap import, and normalize
operations change height samples, not the number of vertices or SEC members.

## Raw height and `maxZ`

Terrain height samples are unsigned 16-bit values interpreted through the MP
vertical scale:

```text
world height = raw height * maxZ / 65535
```

`maxZ` is therefore the map-wide raw-to-world height range/scale. It is not an
ordinary maximum-height clamp.

### Clamp

A clamp edits only vertices below or above explicit raw/world limits. Values
already inside the interval remain unchanged, and the existing `maxZ` scale is
retained. A minimum clamp is possible even though `maxZ` has no corresponding
minimum-scale field.

### Change `maxZ`

Changing `maxZ` while retaining raw samples vertically scales every point in
the landscape. Changing `maxZ` while preserving world-space heights instead
requires requantizing every raw height. Requantization may round values even
where the intended world height is unchanged.

Lowering `maxZ` below an existing peak additionally requires clamping that
peak. A combined **Clamp and tighten vertical range** operation can improve
vertical precision within the smaller range, but it is both a terrain edit and
a scale migration and must preview both effects.

## Fixed-dimension operations

The first MPR Settings increment supports deterministic commands over the
canonical lattice:

- **Offset:** add a signed raw-height delta to every vertex. Saturation is
  refused unless the user explicitly accepts clamping or chooses a `maxZ`
  migration.
- **Flatten:** set every vertex to one explicit raw/world target.
- **Clamp:** restrict values to an ordered inclusive minimum/maximum, changing
  only samples outside the interval.
- **Normalize/remap:** linearly map the current minimum/maximum to a requested
  output interval. A flat input has no range to normalize and must use Flatten.

Every operation is previewed as a normal undoable height command, uses one
canonical delta per shared vertex, rebuilds only affected sector normals,
marks navigation preview data stale, and enters the existing staged MPR save
transaction. A no-op must not dirty the MPR.

## Structural resize

Changing sector dimensions is not a height operation. It changes the number of
SEC members, canonical vertices, tiles, and archive entries. Expansion must
initialize new sectors explicitly; cropping must preview discarded terrain and
overlay content. Added archive members require a runtime-compatible RES hash
table and collision chains. Associated `map.txt` dimensions and minimap framing
may also need an explicit, separately confirmed update.

Existing-map resizing must initially be **Save As** only. It must never silently
invent map registration, entrances, minimap identity, atlas imagery, overlay
content, or catalog metadata.

## Planned MPR Settings presentation

The settings window should separate:

1. **Summary:** profile, embedded namespace, sector/tile/vertex dimensions,
   `maxZ`, raw and world height ranges, materials, overlays, and dirty state.
2. **Height operations:** offset, flatten, clamp, and normalize with affected
   vertex/sector counts, before/after ranges, quantization, MOB-height, slope,
   and navigation diagnostics.
3. **Vertical scale:** explicit **Scale terrain with range** versus **Preserve
   heights by requantizing**, with a separate clamp decision where necessary.
4. **Resize:** crop/expand preview and Save As transaction.
5. **Atlas and liquids:** namespace review, atlas viewer, and validated
   liquid/overlay material editing without direct atlas-image authoring.

The inherited tile-type editor remains out of scope. Endurance derives and
uses atlas descriptions; it does not ask ordinary users to author encoded tile
types or silently modify atlas resources.

## Validation contract

- Synthetic fixtures cover no-op identity, complete and conditional edits,
  shared seams, undo/redo, invalid ranges, flat normalization, saturation,
  deterministic rounding, and fixed vertex/member counts.
- Changed candidates use staged serialization, fresh reopen, semantic diff,
  backup, source-race refusal, and atomic replacement.
- `maxZ` migration is enabled only through its dedicated undoable staged
  transaction, with synthetic reopen and untouched-member equality coverage.
  Crop/expand remains disabled until its separate transaction proves runtime
  hash-chain construction, untouched-member equality, and private copied
  Base/JMV validation.
- Live installations and proprietary map corpora are never mutation targets.
