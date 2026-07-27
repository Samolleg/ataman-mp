# Material atlas profile contract

Endurance first checks for an exact-bound explicitly user-authored local
override, then resolves the namespace, decoded-pixel hash, and slot-count
binding against its compact known-map preset catalog. A matching preset is the
immutable built-in default and is immediately approved and usable. Modified and
custom atlases fall back to
deterministic inference from decoded atlas tiles, the MPR's per-slot category
metadata, and packed tile usage observed in the open map. Zone 1's authored
table is the behavioral oracle for translating either source into the painting
solver. All profiles store the corners visibly
shown by the tile preview. Compilation reflects them once into paint-grid space
and uses the renderer's single global packed-rotation convention.

The deterministic parser:

- fingerprints the decoded RGBA tile sequence with SHA-256;
- finds uniform material anchors using perceptual colour, local texture spread,
  saturation, edge energy, and roughness, preserving distinct grass/stone hues
  and low-saturation snow/ice
  families, then assigns two-, three-, and four-corner transition signatures
  without transitively merging distinct anchors;
- promotes visually novel transition corners to their own family even when no
  uniform tile exists, retaining the tile-preview order supplied by
  the tile decoder;
- classifies observed land and overlay use separately;
- classifies underwater only from land use beneath active overlays;
- makes every structurally valid assignment immediately available to painting;
- leaves ambiguous or cross-layer tiles unassigned; and
- reports missing material combinations as hard-edge fallback diagnostics.

JSON schema version 7 declares `tile-preview-nw-ne-se-sw-v1` corner space and
stores exact material names, allowed layers, surface/underwater-only/mixed role,
member slots, per-slot corner signatures, assigned state, and an optional
user-selected member thumbnail, plus whether an internal authored family is
paint-selectable. Known authored legacy profiles receive one deterministic
paint-grid-to-preview reflection; pixel-inferred profiles retain their visual
assignments. Schema 1/2 profiles migrate conservatively: ambiguous slots
become unassigned and existing families remain selectable. Profiles are exact-bound to the
case-insensitive MPR namespace, decoded pixel hash, and slot count.

Approval is derived metadata for the current safe catalog, not a separate user
workflow and not the switch that enables painting. Unassigned slots remain
excluded. A
corrupt assigned slot that crosses Ground/Overlay boundaries is never admitted
to a catalog, and visual editing rejects such a change before mutation.

The built-in catalog covers all 38 reviewed installed namespaces and stores
derived names and corner-family IDs only. Profiles contain metadata only—never atlas imagery, MPR/SEC payloads, resource
archives, or installation paths. Runtime never falls back to legacy authored
slot tables when an exact preset is missing. The managed local override lives
below the application configuration directory in `terrain-atlas-profiles`,
under a sanitized namespace plus namespace hash. Built-in defaults do not
create files when merely viewed or applied unchanged. Landscape Tools and the
Material Atlas window identify the active source; a local override shows its
path. The window can save and reopen profile copies at user-chosen locations.
Loading a copy revalidates all three binding fields; its outer filename is
irrelevant. Only `user-authored-custom-profile` provenance can override a
built-in default, and distribution packages must contain no local override
files.

The editor uses a visual material tree, assigned tile thumbnails, an unassigned
pool, and four droppable/clickable corner targets. Material confirmation is a single
explicit action only when identity or classification remains unresolved. Whole
tile moves and corner changes recompute family membership automatically. Paint
palette previews use the lowest uniform four-corner tile when one exists. A
transition-only family instead uses a four-tile size-1 vertex junction when a
valid 1-on-3 support exists. When several supports exist, the UI exposes
contextual cards under one profile family ID rather than inventing duplicate
semantic families. A family with neither uniform nor 1-on-3 support remains
visible for review/debug but does not become an unusable palette brush.

Semantic painting uses one shared vertex-region resolver for ordinary and
transition-only families. The brush assigns material identities to terrain
vertices, resolves every incident cell through an authored four-corner
signature, and emits ordinary packed-tile deltas. A transition-only family keeps
the center vertex mandatory and accepts additional brush vertices only while
all incident cells remain representable. Ordinary families may use a bounded
same-footprint authored fallback when an exact boundary signature is absent.
The thumbnail remains presentation metadata and is never the fixed paint
payload.
