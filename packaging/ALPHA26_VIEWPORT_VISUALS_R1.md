# Viewport Visuals switcher r1

## What changed

- Replaced the standalone Lighting mode popup with a collapsed-by-default
  **Visuals** switcher. **Show all** and **Hide all** affect every currently
  available visibility channel.
- Moved Lighting mode into the switcher and added eye-icon toggles for
  entrances, object helpers, terrain, overlays, fog, terrain shadows, skybox,
  and rendered particle effects.
- Removed the separate entrance eye from the map-test toolbar. Entrance
  visibility and its existing saved preference now belong to Visuals.
- Terrain and Overlays stay synchronized in both directions with Landscape
  Tools and the existing M/O shortcuts.
- Fog, terrain shadows, and skybox are disabled with an explanatory tooltip
  when the selected lighting/renderer/map state cannot display them.
- Added **Freeze animations**. It holds unit idle cycles at their current frame
  and is disabled when **Animate unit idle cycles** is off in Render settings.
- **Object helpers** controls only the small editor marker models for light,
  particle, and sound objects. Their real effects remain independently visible;
  use **Particle visuals** to hide rendered particle effects.

## Focused checks

1. Start the editor and confirm **Visuals** is collapsed. Expand it and check
   that the Lighting mode selector and all channels are present.
2. Toggle Entrances and confirm map.txt areas/labels disappear without changing
   the selected runtime entrance.
3. Toggle Terrain and Overlays from both Visuals and Landscape Tools; use M and
   O in Landscape mode and confirm both interfaces remain synchronized.
4. Switch among Unlit, Simple shading, and Full environment. Confirm Fog and
   Skybox are available only in Full environment, and Terrain shadows also
   requires loaded terrain, desktop rendering, and enabled shadow quality.
5. Toggle Object helpers and Particle visuals separately on a map containing
   light, sound, and particle objects.
6. With idle animation enabled, freeze and unfreeze units. Disable idle
   animation in Render settings and confirm Freeze animations becomes disabled.

## Known limitation

The support-dependent controls describe current viewport capability; a checked
control can remain checked while disabled so its preference returns when the
required lighting mode or map becomes available again.
