# Landscape Preview R5 focused test cases

Use a disposable copied MPR. No changed MPR save is required.

## R5-01 — History-window navigation preserves land painting

1. Select **Paint land tiles**, choose a brush, and paint two strokes.
2. Open the History window and click an earlier state, then a later state.
3. Move the pointer and paint another stroke without toggling Landscape Tools.

Expected: the chosen history states apply, painting remains active, the checkbox
remains consistent with the active tool, and a new stroke works immediately.

## R5-02 — History-window navigation preserves overlay painting

Repeat R5-01 with **Paint existing overlays**.

Expected: identical tool continuity; only existing overlay cells can change.

## R5-03 — Sculpt-to-land brush reset

1. Select an overlay brush, switch to Sculpt, then switch to land painting.
2. Move and click over terrain before choosing a new brush.
3. Choose a land brush and paint.

Expected: no cursor tile or paint occurs before selection. The previous overlay
brush is not reused. After selection, the cursor shows the chosen land brush and
painting works.

## R5-04 — Land/overlay boundary reset in both directions

1. Select and use a land brush, then switch directly to overlay painting.
2. Verify selection is empty; choose an overlay brush and paint an active cell.
3. Switch directly back to land painting and repeat.

Expected: each layer transition requires a fresh brush selection. Neither layer
can inherit the other layer's tile or rotation.

