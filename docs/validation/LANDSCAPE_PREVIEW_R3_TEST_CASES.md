# Landscape Preview R3 focused test cases

Use disposable copies only. Do not test changed saves against the live game map
corpus. Changed MPR saving is expected to remain disabled.

## R3-01 — Paint-to-sculpt cursor transition

1. Open a copied MPR and press `B`.
2. Choose a visible atlas tile and confirm the paint cursor appears.
3. In Landscape Tools, switch to **Sculpt terrain** without disabling editing.

Expected: the atlas-tile cursor and tile preview disappear immediately; the
viewport uses the normal sculpt cursor and sculpting works.

## R3-02 — Land-paint Undo continuity

1. Select **Paint land tiles** and paint two distinct strokes.
2. Invoke Undo once through the menu or configured keyboard shortcut.
3. Move the pointer and paint another stroke without re-entering paint mode.
4. Undo the remaining paint actions, then Redo them.

Expected: the first Undo visibly reverts the newest stroke. No invisible
mode-cancellation action is consumed, the paint cursor remains active, painting
continues immediately, and each stroke is one history action.

## R3-03 — Overlay-paint Undo continuity

Repeat R3-02 with **Paint existing overlays** on existing overlay cells.

Expected: identical history and cursor behavior; inactive overlay cells remain
unchanged and no liquid surface is created implicitly.

## R3-04 — Heightmap lighting refresh

1. Import `heightmaps/radial_hill_257.png` into a copied MPR using a range that
   makes the shape obvious.
2. Orbit the camera and inspect diffuse lighting on the new slopes.
3. Undo, inspect the restored terrain, then Redo.

Expected: lighting follows the imported slopes immediately rather than retaining
the original geometry's normal pattern. Undo restores the original geometry and
lighting; Redo restores both the imported geometry and its lighting.

## R3-05 — Save gates unchanged

1. Reopen a clean copied MPR and perform an unchanged same-path Save.
2. Compare size, SHA-256, and modification time before and after.
3. Make one terrain edit and attempt Save.

Expected: the unchanged save is a true no-op. The changed save is refused and the
destination remains byte-identical.

