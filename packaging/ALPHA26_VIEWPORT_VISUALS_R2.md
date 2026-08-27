# Viewport Visuals switcher r2

## What changed

- Added independent eye toggles for **Static objects** and **Dynamic objects**.
  Units and their equipment are dynamic; ordinary MOB scenery, levers,
  torches, and trap locators are static. Light, particle, and sound locator
  models remain independently controlled by **Object helpers**.
- Added **Object shadows**, separate from **Terrain shadows**. It controls the
  real-time static/dynamic object shadow maps and is disabled when Full
  environment desktop shadow rendering is unavailable.
- Changed **Freeze animations** to a conventional checkbox. It remains off by
  default and is disabled when **Animate unit idle cycles** is off.
- All new visibility preferences persist between sessions and participate in
  **Show all** / **Hide all**. Freeze animations is an action preference, not a
  visibility channel, so the bulk visibility buttons do not change it.

## Focused checks

1. Expand **Visuals** and toggle Static objects and Dynamic objects separately
   on a map containing scenery and units. Confirm hidden channels are also not
   pickable in the viewport.
2. Confirm **Object helpers** still affects only light, sound, and particle
   locator models, independently of both object channels.
3. In Full environment mode, toggle **Object shadows** and confirm object-cast
   shadows disappear while **Terrain shadows** remains independent.
4. Switch to Unlit or Simple shading and confirm Object shadows is disabled
   with an explanatory tooltip.
5. Confirm Freeze animations uses a normal checkbox and remains independent of
   Show all / Hide all.

## Known limitation

Support-dependent controls retain their checked preference while disabled so
the requested channel returns when its renderer requirements become available.
