# EI Mapper Endurance 0.3.0-alpha.26 semantic-atlas prototype R7

## What changed

- Automatic ground and overlay-texture brushes now select terrain vertices and
  resolve the surrounding atlas tiles as one junction. Size 1 affects the
  nearest vertex and at most its four incident tiles.
- Larger circle and square brushes select a deterministic vertex footprint.
  Affected tiles remain limited to cells incident to those vertices, including
  vertices shared by two terrain sectors.
- Exact authored four-corner signatures are preferred. Ordinary materials use
  a local hard-edge fallback when a transition is missing. Detail-only
  materials skip unsupported outer junctions instead of inventing atlas
  semantics; the mandatory center vertex is still painted.
- Ground/underwater materials and overlay materials remain in separate palette
  tabs and can no longer be selected across modes.
- The paint diagnostic records the exact vertices accepted by the solver.
- Generated and authored profiles now store the NW/NE/SE/SW corners visibly
  shown by the tile preview. Zone 1's authored assignments are reflected into
  that visual space while compiling back to its proven paint catalog.
- Painting applies one explicit preview-to-grid reflection and the renderer's
  fixed packed-rotation rule. Atlas content no longer selects rotation meaning.
- Four-tile thumbnails use visual-space requests and the fixed QImage rotation,
  independent of the paint-grid representation.
- Circular brushes draw a true circular outline and highlight every selected
  terrain vertex. Square brushes retain a square outline.
- Palette thumbnails show the four tiles produced around a size-1 vertex.
  Transition-only families expose separate contextual cards for each authored
  1-on-3 support while retaining one underlying semantic family.
- Every authored 1-on-3 relation now exposes a contextual vertex card, even
  when the center material also has uniform tiles. Zone 2 therefore offers
  both `Ground on Granite` and the inverse `Stone on Ground` motif while its
  ordinary Stone area-fill brush remains a separate palette card.
- A contextual card now retains that support through selection and painting.
  Clicking it places the exact four-tile motif shown by the card instead of a
  nearest-family approximation based on unrelated terrain underneath.
- Sparse motifs cannot overlap unless the atlas contains the necessary
  multi-corner transitions. An impossible touching click is rejected before
  mutation with an actionable explanation instead of producing a checkerboard.
- The headless UI check uses temporary settings and cannot read or overwrite an
  active editor's brush preferences.

Manual atlas-tile painting and overlay surface tint/wave material assignment
remain tile-based and otherwise unchanged.

## Manual checks

1. Open Zone 1 Material Atlas and select Tile 50. Confirm the buttons say
   Grass/common on NW+NE and Grass/dark on SW+SE, matching the visible tile.
2. In Zone 1 Ground mode, confirm every palette card forms a coherent junction
   with its painted material in the center. Paint with size 1 and confirm the
   four tiles around the nearest vertex retain the previously correct edge
   directions; test undo and redo.
3. In Zone 2, confirm existing profile names and assignments remain present,
   and its palette cards remain coherent. Paint several ordinary materials and
   confirm every transition edge appears on the requested side.
4. Paint each Zone 2 irregular contextual card with size 1 over a different
   underlying material. Each result must match that card's four-tile preview,
   including its named support, with no nearest-family checkerboard.
5. Select Zone 2's `Ground on Granite` card and confirm tile 502 rotates around
   the selected vertex into one coherent Ground center. Select `Stone on
   Ground` and confirm tile 448 likewise forms a coherent Stone center. The
   ordinary Stone card must remain available and continue to fill normally.
   Try to place touching contextual motifs: they may join only when the atlas
   has the exact required multi-corner transition; otherwise the click must
   leave terrain unchanged and explain what is missing.
6. Paint Zone 2's underwater family over itself and confirm it remains
   selectable and blends as an ordinary ground-layer family.
7. In Overlay mode, paint an atlas texture over an existing overlay surface.
   Only active overlay cells around selected vertices may change; tint, waves,
   and other surface-material properties must remain unchanged.
8. Paint across a terrain-sector boundary and export a diagnostic. Confirm the
   stroke remains continuous, `paintedVertices` lists only the highlighted
   footprint, and the snapshot distinguishes `previewImageCorners` from
   `paintGridCorners`.

## Expected warning

A 1-on-3 contextual card is a vertex motif, not proof that its center material
can form a contiguous area on that support. Touching copies are refused unless
the profile also contains the exact 2-corner, 3-corner, or uniform transitions
needed at the resulting junctions. Add and assign those atlas tiles if the
motif should support contiguous painting.
