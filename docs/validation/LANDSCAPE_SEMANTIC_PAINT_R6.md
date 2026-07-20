# Landscape semantic painting R6

> Superseded by R7. Runtime testing showed that MP tile categories describe
> gameplay surfaces, not visual paint materials. The R6 pixel-edge inference
> therefore mixed visually unrelated atlas entries and must not be used.

## Scope

This increment replaces direct atlas-slot painting in the Landscape Tools flow
with semantic terrain-material painting. Changed-MPR saving remains disabled.
The MPR's MP tile-category table and supplied atlas textures are read-only.

## Selection and blending contract

- The user selects a terrain category such as Grass, Ground, Stone, or Road.
  Selecting a tile in the atlas inspector selects that tile's read-only category;
  it does not request the exact slot or edit atlas metadata.
- Each supplied 8 by 8 atlas is decoded through the existing resource-profile
  resolver. Endurance samples the visible edges of every slot in all four
  rotations and constructs a map-local compatibility catalog.
- A paint stamp changes the semantic category inside the chosen circle or square
  and may retile only a one-cell halo. Candidate tiles remain inside the MPR's
  actual atlas namespace.
- Edge-error minimization chooses compatible authored transitions and rotations.
  Near-equal candidates use a stable hash of the embedded map namespace, tile
  coordinate, and semantic category for deterministic natural distribution.
- The editable region is canonically seeded before relaxation, so painting the
  same semantic field again is a no-op rather than an invisible undo action.
- Missing atlas pixels, missing categories, inactive overlays, and out-of-
  namespace selections fail closed. No transition tile is synthesized.

The atlas edge/padding model was informed by aspadm's EI-HD-tiles documentation,
which is attributed in `NOTICE.md`. No EI-HD tools, source, atlases, or assets are
included in Endurance.

## Production brush behavior

- Paint radius is expressed in terrain tiles and supports circle and square
  footprints.
- Fast pointer movement is interpolated so a small brush cannot leave gaps.
- Mouse press through release is one undo step, including every automatic seam
  update. Redo restores the exact packed selections.
- Right-click samples the map's semantic terrain category.
- Rotation is automatic; the mouse wheel no longer mutates a hidden manual
  rotation while semantic painting is active.
- Overlay mode affects only already-active overlay cells and cannot create a
  liquid surface implicitly.

## Automated evidence

The synthetic document suite covers semantic catalog validation, deterministic
selection, brush and halo locality, namespace validation, same-material no-op,
and exact apply/undo. Current result: 46 passed, 0 failed, 1 expected private-
fixture skip. The full release application also compiles successfully with Qt
5.12.12 / MinGW 7.3 in the ASCII build mirror.

## Manual validation required

Use copied maps only. For at least one Base map and one JMV map:

1. Open Landscape Tools, choose **Paint land tiles**, select a terrain material,
   and paint a compact island inside a different material.
2. Verify the interior uses natural variants and its edge uses plausible authored
   transition tiles without obvious orientation errors.
3. Drag rapidly across the map and verify there are no gaps.
4. Undo once and verify the whole drag, including seam-halo changes, disappears;
   redo once and verify the exact result returns.
5. Right-click a material, paint elsewhere, then switch land/overlay modes and
   confirm each mode requires a fresh selection.
6. In overlay mode, cross active and inactive cells and verify inactive cells do
   not gain an overlay.
7. Confirm Save/Save As still refuses a changed MPR and the source file's hash and
   timestamp remain unchanged until an explicit successful save exists in a later
   increment.

If a particular atlas has no suitable authored transition, record the map,
category pair, and screenshot. The editor must report/refuse that case rather
than silently inventing a tile.
