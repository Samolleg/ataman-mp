# Overlay surface authoring R1

## Changelist

- Landscape Tools now exposes three explicit overlay coverage operations:
  **Paint active surfaces**, **Add surface**, and **Remove surface**.
- **Add surface** requires an existing MPR liquid-material record and an
  existing atlas family. It does not generate textures, atlas slots, or
  material records.
- Adding the first surface in a land-only SEC performs the validated native
  overlay-layout upgrade. Empty placement is projected against land; subsequent
  painting and sculpting use the overlay surface.
- Add/remove drags are one undo step. Removal changes only the verified inactive
  material sentinel and retains tile/vertex bytes for exact undo.
- **Sculpt overlay surfaces** provides Raise, Lower, Flatten, and Smooth using
  the normal sculpt controls. Only vertices belonging to active overlay cells
  are editable.
- Overlay sculpting synchronizes duplicated sector-seam vertices and rebuilds
  overlay normals independently. It does not change land vertices or overlay
  tile/material blocks.
- All changed SEC payloads continue through staged reopen, semantic locality,
  backup, and atomic replacement. Runtime liquid animation behavior remains a
  separate renderer concern and was not changed by this slice.

## Automated evidence

- Full release build: passed with Qt 5.12.12 / MinGW 7.3.
- `endurance_document_tests`: 70 passed, 0 failed, 3 expected private-fixture
  skips.
- Coverage includes structural type-0 to type-3 activation, exact deactivation,
  undo/redo provenance, untouched MP/unknown members, staged changed-MPR reopen,
  active-only overlay sculpt locality, land-byte equality, and two-sector seam
  equality.

## Manual test matrix

Use a copied MPR or an isolated mod workspace. Do not edit the installed Base or
JMV map corpus directly.

1. Open a copied Zone 1 MPR and Landscape Tools. Select **Paint existing
   overlays**. Existing water/swamp painting must retain its previous behavior.
2. Select **Add surface**, an existing overlay family, and an existing liquid
   material. A size-1 click on dry land must create one visible surface cell at
   the outline. Dragging must remain responsive and create one undo entry.
3. Undo and redo that stroke. Coverage, texture, height, and liquid appearance
   must return exactly; the brush must remain usable.
4. Select **Remove surface** and remove an interior and boundary cell. No land
   texture or height may change. Undo must restore the exact surface.
5. Select **Sculpt overlay surfaces**. Exercise Raise, Lower, Flatten, and
   Smooth on active water. The outline must follow and project onto the overlay;
   dry land must be a no-op. The land underneath must remain unchanged.
6. Sculpt across a sector boundary where overlay exists on both sides. No crack
   or lighting seam may appear. Undo/redo must be one step per drag.
7. Save As, reopen the saved MPR, and repeat undo-independent visual checks.
   Then run Test Current Map from the isolated workspace and verify overlay
   coverage, height, material appearance, and collision/navigation behavior.
8. Confirm Show main terrain / Show overlays still work and that entering or
   leaving Play from Here temporarily suspends both overlay paint and sculpt
   outlines without breaking the tools afterward.

## Known remaining work

- Editing the values inside MPR liquid-material records is still read-only.
  This build can select any existing record for newly activated cells.
- Creating entirely new material records or atlas content remains out of scope.
- Semantic atlas coverage outside reviewed namespaces still requires profile
  conversion/review or manual atlas-tile selection.
