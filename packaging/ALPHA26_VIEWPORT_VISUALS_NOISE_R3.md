# Viewport Visuals and Noise sculpt brush r3

## What changed

- Added **Scene lights** to the Visuals switcher. This controls authored MOB
  point lights and torch lights and remains synchronized in both directions
  with **Scene point lights** in Render settings.
- Added the **Noise** sculpt tool for terrain and overlays. Each stroke creates
  a new stable coordinate-based noise field containing both positive and
  negative height changes. Repeated stamps during that stroke strengthen the
  same shape instead of flickering to new random values.
- Noise edits use the normal terrain/overlay transaction path, including seam
  synchronization, affected-normal updates, exact undo/redo, and staged save.
- Retains the r2 independent static/dynamic object, helper, object-shadow, and
  visibility controls and the conventional Freeze animations checkbox.

## Focused checks

1. Toggle **Scene lights** in Visuals and confirm **Scene point lights** in
   Render settings follows it. Change the Render setting and confirm Visuals
   follows it too.
2. Select Landscape Tools > Sculpt > **Noise** and drag once over terrain.
   Confirm the stroke produces both raised and lowered detail without changing
   pattern beneath the cursor during the same drag.
3. Undo and redo the stroke and confirm the exact shape is restored.
4. Repeat the same checks with an active overlay.
5. Start a new stroke over the same area and confirm it uses a new pattern.

## Known limitation

Noise is selected from the Tool list and has no dedicated keyboard shortcut.
Support-dependent Visuals controls retain their checked preference while
disabled so the requested channel returns when its renderer requirements are
available.
