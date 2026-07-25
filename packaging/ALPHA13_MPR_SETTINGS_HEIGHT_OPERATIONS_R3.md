# EI Mapper Endurance 0.3.0-alpha.13 — MPR Settings R3

This tester build combines the current alpha.13 main boundary with the first
safe whole-map terrain-height workflow.

## Changes

- **MPR Settings** is a non-blocking, movable, floatable dock without a
  context-help button. Undo and redo remain available while it is open.
- Closing the dock hides it; **MPR Settings...** restores it. MPR unload safely
  discards the old panel before its terrain document is replaced.
- The atlas **Materials** page is available for read-only liquid-material and
  animated-tile inspection. Unsafe mutation controls remain disabled.
- The summary covers sector, tile, vertex, height-range, `maxZ`, namespace,
  atlas, and material information.
- Whole-map offset, flatten, clamp, and normalize/remap are single undoable
  operations with affected-vertex/sector previews and saturation refusal.
- Includes `origin/main` through `495aad7` and its alpha.13 Test Current Map
  runtime and temporary resource/minimap injection path.

## Deliberately deferred

- Liquid-material and animated-tile mutation.
- Editing `maxZ` or requantizing all heights.
- Cropping or expanding sector dimensions.
- Adding or removing SEC/archive members.
- Direct tile-type or atlas mutation.

Use `MPR_SETTINGS_HEIGHT_OPERATIONS_R1.md` for the focused manual cases.
