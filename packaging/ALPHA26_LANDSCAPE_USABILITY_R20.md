# Landscape sculpt usability R20

## Changelist

- `M` now toggles main-terrain visibility in every Landscape Tools mode.
- `O` now toggles overlay visibility in every Landscape Tools mode; the old
  tile-brush-only `J` binding is retired.
- `Z`, `X`, `C`, and `V` select Raise, Lower, Flatten, and Smooth while
  sculpting terrain or overlays.
- Flatten target is shown only for Flatten. Raise, Lower, and Smooth expose a
  selectable Smooth, Linear, or Constant falloff instead.
- Right-click in a sculpt mode samples the visible terrain or overlay point,
  displays its raw 0-65535 height beside Mouse Coords, and assigns that value
  as the Flatten target.
- `Alt+Shift+Wheel` adjusts the Raise/Lower step. Existing radius, strength,
  paint-material, manual-tile rotation, and camera-wheel controls are retained.
- Object transform controls and their key hints are hidden while Landscape
  Tools is active.
- Landscape Tools now has an **Open MPR Settings...** button below heightmap
  import.

The R19 overlay-stroke ownership and undo behavior is unchanged in this build.

## Focused manual checks

1. Open an MPR, press `L`, and verify `M` and `O` toggle their matching layers
   in all five landscape modes.
2. In Sculpt terrain, press `Z`, `X`, `C`, and `V`. Confirm the selected tool
   follows the shortcut and only Flatten shows Flatten target.
3. Hover terrain and right-click. Confirm Mouse Coords includes `Raw`, and the
   sampled value becomes Flatten target. Repeat in Sculpt overlay surfaces.
4. Confirm Smooth/Linear/Constant falloff produces progressively different
   brush edges for Raise, Lower, and Smooth.
5. Use `Alt+Shift+Wheel` and confirm Raise/lower step changes without changing
   radius, strength, camera zoom, or camera speed.
6. Use **Open MPR Settings...**, close/reopen Landscape Tools, and confirm the
   object transform buttons return only after Landscape Tools closes.
7. After the checks, close the editor normally and confirm it exits promptly.
   The supplied R19 log recorded one delayed post-cleanup access violation after
   the session had already been marked clean; R20 does not yet claim that
   unrelated shutdown issue is fixed.

## Known limitations

- Height sampling requires a visible hit on the active sculpt layer. In overlay
  sculpt mode, right-clicking an area without an overlay deliberately does not
  sample the terrain beneath it.
- One supplied R19 session logged a shutdown-only access violation after view
  deletion and after the backup session was marked clean. It was not an MPR
  save/validation failure and has not been reproduced by the headless R20 checks.
