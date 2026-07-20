# Landscape automatic semantic painting R8

R8 removes terrain-tileset sidecars from the map-maker workflow. Loading an MPR
automatically analyzes the atlas selected by its embedded namespace and active
resource profile. Atlas overrides therefore follow the same path as the game;
the user does not create, load, or maintain editor metadata.

## Automatic analysis

For each supplied atlas slot, Endurance samples four inset corner patches and
records mean colour plus texture variance. It then:

1. finds visually uniform source tiles;
2. groups natural variants only when their visual features and read-only MP
   surface category agree;
3. classifies every other slot's NW/NE/SE/SW corners against those visual
   materials;
4. registers all four packed rotations in the exact transition catalog; and
5. exposes one representative atlas slot for each inferred material.

MP tile category is a conservative grouping constraint and display hint only;
it is never treated as the visual material identity. Ambiguous corners and
atlas slots are excluded. Painting near an excluded or undescribed tile fails
closed with its coordinate instead of substituting a visually unrelated slot.

The resulting catalog is in memory and tied to the atlas currently resolved by
the Base/JMV profile. There is no `*.eitileset.json` UI or required sidecar.

## Painting and responsiveness

The R7 exact shared-corner solver remains. Each stamp reads only its footprint
and one-tile halo. Packed-tile reverse lookup and signature candidate selection
are indexed, so neither map size nor unrelated atlas variants are scanned during
the drag. Natural distribution remains deterministic by namespace, coordinate,
corner signature, and seed.

The material selector identifies each inferred material by a representative
atlas slot and read-only surface-category hint. Selecting it updates the atlas
preview; right-click sampling works on uniform classified tiles.

## Diagnostics and safety

Paint diagnostic JSON now reports automatic-analysis mode, atlas pixel hash,
material count, described-slot count, ambiguous-slot count, and the preview
delta audit. It contains no MPR bytes or atlas pixels.

Changed-MPR Save and Save As remain disabled. No EI-HD tool, source, YAML,
descriptor, atlas, or texture is included. The external research attribution
remains in `NOTICE.md`.

## Automated evidence

The synthetic analyzer test verifies automatic separation of two visual source
materials and exact classification of a two-material corner transition. The
full document suite additionally covers transition rotation, deterministic
selection, brush/halo locality, no-op repaint, undo/redo, unchanged archive
identity, and failed-save behavior. The full release application builds with Qt
5.12.12 / MinGW 7.3.

Real-atlas clustering and visual seam quality require human review because the
private Base/JMV resource corpus is not read by the automated build process.
