# Alpha 26 terrain paint modes R166 load-performance test

This revision removes the large synchronous RES archive copy and hash pass that
made map loading appear frozen at 33% and then again at 66% after R164.

## Changes

- **Published semantic metadata is read by index.** Opening a finalized terrain
  map now reads the RES header, member table, filename buffer, and only the two
  small JSON members needed for the map's semantic field and embedded atlas
  profile. Unrelated MMP atlas pages are neither copied nor hashed.
- **The existing validation remains fail-closed.** The semantic document still
  has to match the MPR, atlas pixels, profile, dimensions, and packed grid.
  Duplicate requested member names, malformed tables, and truncated payloads
  are rejected instead of selecting an arbitrary archive member.
- **Load logs identify the remaining phase.** `workflow.log` now records
  separate landscape catalog/semantic-restore timings, MOB post-preload phases,
  and the outer landscape/MOB/recent-files/map-context phases.

No terrain resolver, Manual paint, authored mask, semantic assignment, runtime
terrain type, or atlas-capacity behavior changed in this revision.

## Focused manual checks

1. Open Zone2 and confirm the landscape no longer sits at 33% for roughly
   12 seconds before the MOB phase.
2. Open Zone1 and confirm the former much longer load stall is also gone.
3. Paint and undo one Manual tile as a quick regression check, then use Modern
   paint and Shift+R once. Their R165 behavior should be unchanged.
4. If either 33% or 66% still pauses noticeably, close the editor and send the
   new `logs/workflow.log`; its phase timings now identify the remaining owner.

## Automated validation

- Indexed RES regression: a requested semantic/profile pair was read from a
  synthetic archive containing an unrelated 8 MiB atlas page while consuming
  less than 1% of the archive bytes.
- Document suite: 121 passed, 0 failed, 6 expected fixture skips.
- Virtual-atlas suite: 113 passed, 0 failed.
- Landscape Tools widget suite: 5 passed, 0 failed.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: PASS.
- Headless `--ui-widget-check`: explicit PASS.

## Manual validation boundary

The old and new workflow logs plus the R164 commit boundary identify the full
archive load as the first incorrect operation. Automated tests verify bounded
member reads and unchanged terrain suites, but only the user's installed
resource profile reproduces the exact live Zone1/Zone2 progress-bar timing.
