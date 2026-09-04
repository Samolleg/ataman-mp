# Persistent world-map coordinates

This stage gives every map in a resource profile a stable two-dimensional
world origin. It deliberately adds the model and persistence boundary without
adding map-dragging controls.

## Root cause

The earlier neighbor layout was rebuilt from the currently active zone. It
placed that zone at `(0, 0)`, traversed at most three route steps, and exposed
the resulting temporary offsets directly to rendering and navigation. Opening
the same pair from the opposite direction therefore changed the apparent
origin, while camera rebasing subtracted an active-relative display offset.
There was no persistent identity or coordinate owner to keep an A → B → A
round trip in one frame of reference.

The first navigation implementation also read the camera only after unloading
the source workspace and deferred rebasing until the next UI turn. A target
terrain could therefore be painted with source-local camera coordinates. A
neighbor transition now snapshots camera position, orbit pivot, and rotation
before unload and applies the target-local values immediately after the target
MPR loads, before the next event-processing/paint boundary.

The persistent model separates those responsibilities:

- `CWorldMapLayout` infers the complete connected components and owns the
  local/world transforms.
- `CWorldMapLayoutDocument` owns schema validation, merge policy, and atomic
  persistence.
- `CNeighborMapLayout` selects at most five maps for display and derives their
  visual offsets from saved world origins. It does not infer or mutate them.

## Coordinate convention

World X and Y use the same axes and terrain units as MPR-local X and Y. A map's
saved value is the world position of its local `(0, 0)` corner:

```text
worldPoint = mapWorldOrigin + localPoint
localPoint = worldPoint - mapWorldOrigin
mapWidth   = sectorWidth  * 32
mapHeight  = sectorHeight * 32
```

Paired entrance deploy rectangles determine relative offsets. Only an
unambiguous pair on opposite MPR boundaries contributes an edge. Inference is
strictly partitioned by resource-profile identity and allod identity; travel
metadata never creates geometry across allods.

The main stock component of each allod uses an explicit MPR anchor at `(0, 0)`:
Gipat starts at `zone1`, Ingos at `zone11`, and Suslanger at `zone18`. If that
MPR is absent, or for any disconnected auxiliary component, the case-folded
lexical minimum zone identity remains the deterministic fallback. All zones in
the component are traversed and retained; the viewport's five-neighbor limit
is only a rendering selection. Disconnected components have independent
anchors and are never combined for background display.

## Persistent document

On Windows the schema-version-1 JSON document is stored at:

```text
%LOCALAPPDATA%\EI Mapper Endurance\world-map-layouts.json
```

The document stores only stable profile, allod, zone, and MPR identities,
finite X/Y origins, origin provenance (`inferred` or `manual`), and the
component anchor identity. It does not serialize source paths, archive paths,
MPR/MOB paths, or any installed-game location.

On first use, the document is seeded deterministically from parsed `map.txt`
topology. Later topology refreshes preserve every known saved coordinate and
add only unknown zones. A new zone in an existing component is translated into
the saved component frame using a deterministic existing zone; it never moves
the existing zones implicitly. A changed MPR identity is diagnosed and ignored
for display rather than rebound accidentally.

Writes use `QSaveFile` with direct-write fallback disabled. A malformed,
unsupported, unreadable, or unwritable document is left byte-for-byte
untouched. The editor uses an inferred in-memory layout for the session and
shows a warning in the log and UI; persistence remains blocked until the user
repairs the document and reloads the application.

## UI-independent mutation boundary

The document exposes the following operations for a later map-editing stage:

- `setZoneOrigin(...)` saves an explicit origin with `manual` provenance.
- `resetZoneToInferred(...)` restores one zone from current `map.txt`
  inference, aligned to its saved component.
- `resetAllodLayout(...)` replaces one profile/allod layout with a fresh
  deterministic inference.

This stage intentionally binds none of those operations to mouse input. A
neighbor double-click passes the source and destination absolute origins to
the camera transform. The source camera snapshot is taken before workspace
unload and restored in the destination frame before its first paint, preserving
both camera position and pivot through an A → B → A round trip while leaving Z
and rotation unchanged.
