# Alpha 26 terrain paint modes R168 load-parity test

This revision removes feature-branch-only terrain catalog and resource work
that made Zone2 load much more slowly than the current parallel `main` build.

## Root cause and change

- **Opening a map no longer rescans every configured map and RES root.** The
  Maps browser already owns the current catalog snapshot, so map activation now
  binds that snapshot exactly as current `main` does.
- **Published-atlas discovery no longer reads every complete MMP page.** The
  loader identifies the archive containing each page from its RES index entry
  without loading the page body.
- **Material-palette hydration reads only the required authored tiles.** A
  256x256 source tile is decoded directly from its DXT block-row band instead
  of loading and hashing a complete 2048x2048 page. The indexed result is
  cached by exact archive/member identity.
- **Global semantic families are compiled once per catalog rebuild.** Modern,
  Classic Auto, and overlay catalogs reuse the same compiled family set.
- **Load diagnostics now isolate these phases.** `workflow.log` reports
  `archiveBindingMs`, material-source `elapsedMs`, and whether any full source
  pages were read.

No terrain resolver, Manual paint, authored mask, semantic assignment, runtime
terrain type, atlas capacity, or RES publication rule changed in this revision.

## Focused manual checks

1. Open Zone2 from a cold editor start. Compare the 33%/66% pauses with the
   current-main `patrol-validation-r1` package; the modern branch should no
   longer freeze for several seconds in those phases.
2. Close and reopen Zone2 once to exercise the persistent tile cache.
3. Inspect `logs/workflow.log`. The material-source line should report
   `fullPagesRead:0`; the MPR line now includes `archiveBindingMs`, and the map
   workspace line should show a small `mapContextMs` rather than a full rescan.
4. As a behavior regression check, paint and undo one Manual tile, make one
   Modern stroke, and invoke Shift+R once. Painting behavior should remain the
   same as R167.

## Automated validation

- Document suite: 123 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape-tools widget suite: 5 passed, 0 failed.
- Direct indexed-tile regression proves the decoder reads less than one quarter
  of its test MMP member and returns the exact same pixels as full-page decode.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The tests prove the removed full-page reads, exact decoded pixels, source
preference, cache identity, and unchanged paint suites. The editor and game were
not launched by the build process; only the user's installed Zone2 profile can
measure the final interactive load time against the current-main control.
