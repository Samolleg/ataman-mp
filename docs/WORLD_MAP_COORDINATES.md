# Persistent world-map coordinates

This stage gives every map in a resource profile a stable two-dimensional
world origin and lets the user correct inferred positions directly in the
viewport.

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

The neighbor-open signal originally passed the active origin by reference to
the view's mutable state. Qt delivers this connection synchronously, and
loading the target MPR refreshes that state before the open handler returns.
The old source origin could therefore turn into the target origin while the
same call was still running, reducing the camera correction to zero. The
double-click handler now copies both origins before emitting the request, so
the source snapshot remains stable throughout the nested load.

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

## Arranging maps in the viewport

Open **Visuals**, enable **Neighbor maps**, and then enable **Arrange maps**.
Tinted neighboring terrain becomes draggable with the left mouse button. The
current zone and the world X/Y origin of its local `(0, 0)` corner are shown
while it moves. Releasing the button saves that origin for the active resource
profile; it does not modify the MPR, MOB, or `map.txt` files.

The component's base map is fixed at `(0, 0)` and cannot be dragged. For stock
allods these bases are `zone1` on Gipat, `zone11` on Ingos, and `zone18` on
Suslanger. Place every other map relative to that base. Middle-button camera
navigation remains available while arranging, and ordinary selection/terrain
editing plus double-click map switching are paused so a placement gesture
cannot edit map content accidentally. Turn **Arrange maps** off before using
double-click navigation again.

**Reset map positions...** removes the active allod's manual coordinates after
confirmation and recreates them from the current `map.txt`. The document APIs
behind these controls remain separately testable:

- `setZoneOrigin(...)` saves an explicit origin with `manual` provenance.
- `resetZoneToInferred(...)` restores one zone from current `map.txt`
  inference, aligned to its saved component.
- `resetAllodLayout(...)` replaces one profile/allod layout with a fresh
  deterministic inference.

A neighbor double-click passes copied source and destination absolute origins
to the camera transform. The source camera snapshot is taken before workspace
unload and restored in the destination frame before its first paint, preserving
both camera position and pivot through an A → B → A round trip while leaving Z
and rotation unchanged.
