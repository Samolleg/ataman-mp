# Ataman 14 — palette catalogue filter

2026-09-17. EI-PALETTE-CATALOG-014. Branch `codex/palette-filter-v14`,
base `37c1c550a97cd4b9482fad6a38bf54d77f0d5342` (installed version 13).

## Behavior

Modern and Classic material palettes initially show all available materials
in one family-grouped list, without separate used/unused sections.
“Only materials used on this map” restricts ground and overlay materials to
the current usage set. The preference persists in QSettings. Manual physical
atlas mode hides this semantic-material filter. Text search remains available.
Changing the filter or map usage updates the selected brush and emits its new
identity; an empty used palette has no selected material. No map data changes.

## Checks

- Fresh release build `build-release-14-palette`, log `build-palette14.log`: PASS.
  Executable reports version 14.
- `build-tests-14-palette/results-fonts.txt`: 10 PASS, zero failures. Covers
  all/used lists, combined family headings, ground/overlay and Modern/Classic,
  map-usage changes, selection notifications, empty lists, saved preference,
  Manual visibility, first-open/narrow-panel layout, batch refresh, physical
  atlas identities and delayed thumbnails.
- Initial test launch lacked QT_PLUGIN_PATH; only the owned test process was
  stopped. Offscreen layout initially failed four width checks with missing
  fonts; setting QT_QPA_FONTDIR to Windows Fonts made all cases pass.
- Version, database creation, resource navigation: PASS. Initial scenario run
  failed to create its synthetic recovery snapshot under sandbox and exited
  abnormally; re-run with permission for isolated Qt test data: exit 0,
  47 PASS, zero failures (`scenario-recheck.json` and scenario stdout).
- General UI-widget check: timeout 120 seconds, as in version 13; only the
  owned test process stopped. Not PASS. Installation evidence follows below.

## Manual acceptance

Open landscape painting: all families should appear under “Все материалы”.
Toggle “Только используемые на карте”, switch ground/overlay, then disable it:
the complete catalogue should return without switching paint modes.
Check the same preference after restarting. The actual user window has not
been opened by the agent. Neighbor-scene visual/FPS acceptance remains separate.
