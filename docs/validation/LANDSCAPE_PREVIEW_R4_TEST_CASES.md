# Landscape Preview R4 focused test cases

Use a disposable copied MPR. No changed MPR save is required.

## R4-01 — Land paint Undo-to-zero and Redo

1. Enable **Paint land tiles**, select a tile, and paint two distinct strokes.
2. Press Ctrl+Z twice so both strokes are undone.
3. Press Ctrl+Y once, move the cursor, and paint a new stroke.
4. Continue alternating Ctrl+Z and Ctrl+Y several times.

Expected: Redo restores the first stroke without leaving paint mode. The tile
preview follows the pointer, and a new stroke can be painted immediately without
toggling Landscape Tools.

## R4-02 — Overlay paint Undo-to-zero and Redo

Repeat R4-01 with **Paint existing overlays**.

Expected: identical cursor, history, and continued-paint behavior. Only active
overlay cells change.

## R4-03 — Intentional paint exit cleanup

1. Enter either paint mode and select a tile.
2. Switch to **Sculpt terrain**.
3. Switch back to painting and select/paint normally.

Expected: the paint cursor and preview disappear on exit, Sculpt is usable, and
painting can be re-entered without a stranded cursor or stale visibility state.

