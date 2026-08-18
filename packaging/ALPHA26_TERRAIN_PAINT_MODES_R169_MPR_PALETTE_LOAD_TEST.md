# Alpha 26 terrain paint modes R169 MPR palette-load test

This revision targets the remaining MPR-only pause after R168 made MOB and
terrain-source loading fast.

## Root cause and change

- **Modern and Classic Auto palettes now commit atomically.** MPR activation
  previously rebuilt and destroyed all six material grids after each partial
  catalog update. The final catalog is now installed with one widget rebuild.
- **The hidden Manual grid is no longer built during MPR open.** Hashing every
  active physical tile and creating hundreds of icons is deferred until the
  Manual tab is actually selected. Atlas changes mark that grid dirty, so the
  first Manual open still shows the current physical atlas, including newly
  added Modern tiles.
- **Detailed load timings identify any remaining catalog cost.** The workflow
  log separates semantic compilation, source hydration, working-atlas setup,
  Classic data, Manual data, family usage, and final palette commit.

No terrain resolver, paint result, authored mask, semantic assignment,
runtime terrain type, atlas capacity, or save/publication rule changed.

## Focused manual checks

1. Start on the Modern or Classic Auto tab and open Zone2. Compare the MPR
   pause with R168 and the parallel current-main package.
2. Inspect `logs/workflow.log`. Copy the `Terrain material catalog phases`
   and `MPR editor load phases` lines if MPR loading still has a visible pause.
3. Open the Manual tab once. Its grid should populate with the current atlas;
   select and paint one tile, then undo it.
4. Add one tile through Modern painting, switch to Manual, and confirm the new
   physical tile appears without reopening the map.

## Automated validation

- Document suite: 123 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape-tools widget suite: 7 passed, 0 failed.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The tests prove that partial catalog updates do not rebuild widgets, the
Manual palette is requested only when its tab opens, and the broader terrain
suites remain unchanged. The editor/game was not launched by the build
process; the user's configured Zone2 profile is required to measure the final
interactive MPR time.
