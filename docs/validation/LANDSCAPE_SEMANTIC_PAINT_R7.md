# Landscape semantic painting R7

R7 removes the unsafe R6 inference path. An MPR's MP tile-category array remains
read-only gameplay metadata and is never presented as a visual paint-material
list.

Semantic painting now requires an editor-side `*.eitileset.json` definition
authored for the supplied atlas. Loading is fail-closed:

- schema version and MPR namespace must match;
- decoded atlas slot count and canonical pixel SHA-256 must match;
- every material has a unique integer ID and display name;
- every atlas variant declares its four visual corner materials in
  north-west, north-east, south-east, south-west order;
- allowed packed rotations and positive natural-distribution weights are
  explicit;
- duplicate packed tiles, unknown materials, malformed rotations, existing
  undescribed tiles, inconsistent shared corners, and missing transitions all
  refuse the operation.

The descriptor is editor input only. Endurance neither creates nor edits an
atlas, and no EI-HD asset, descriptor, source, or tool is included.

## Brush behavior

A stamp decodes only the painted footprint and its one-tile halo into a shared
corner lattice. Painted tile corners receive the selected visual material and
the affected tiles are resolved by exact signatures. Existing variants are
retained when their signature still matches; otherwise deterministic weighted
selection provides natural distribution. Runtime work is proportional to brush
area, not total map size.

`Load terrain tileset definition...` activates painting. Without a definition,
painting is disabled with a user-facing explanation. `Export paint
diagnostic...` writes source-MPR SHA-256, namespace, descriptor filename, and
the preview delta audit to JSON without serializing or exposing a changed MPR.

Changed-MPR Save and Save As remain disabled. No part of this increment weakens
the staged-save contract.

## Descriptor example

```json
{
  "schema": "ei-mapper-endurance-terrain-tileset",
  "version": 1,
  "mprNamespace": "ExampleSet",
  "atlasSlotCount": 2,
  "atlasPixelSha256": "replace-with-the-diagnostic-exported-64-hex-character-hash",
  "materials": [
    {"id": 1, "name": "Grass"},
    {"id": 2, "name": "Ground"}
  ],
  "variants": [
    {"atlasSlot": 0, "corners": [1, 1, 1, 1], "rotations": [0], "weight": 1},
    {"atlasSlot": 1, "corners": [1, 1, 2, 2], "rotations": [0, 1, 2, 3], "weight": 1}
  ]
}
```

This is schema documentation, not a usable definition for a proprietary atlas.
Atlas authors must describe all variants and transitions that the target map
uses.

## Automated evidence

The synthetic document suite covers descriptor schema/namespace rejection,
reverse packed-tile lookup, exact transition selection, deterministic locality,
repaint no-op behavior, undo/redo, archive preservation, and failed changed-MPR
saves. A full release application build verifies the UI integration.
