# Ataman 37 — EI-TRAVEL-GLOBAL-POSITION

2026-09-20. Branch codex/travel-global-position-v37, base
feb92243128c70f2739dd7861544b639b71d8f5e. Parent EI-TRAVEL-EDITOR remains open.

The route wizard previously averaged the source and target #position, even
when the target had zero position. This value does not describe the exit.
The wizard now starts with empty coordinates and offers an actual textured
FIG8 global surface picker. CPU raster and picking share the same projection
and nearest-surface depth. Aspect ratio is preserved. Existing edge markers
are displayed; selected point changes on click/drag, background retains it.
The existing-route command edits only #position through shared travel Undo.
No game resources or world-map-layouts.json were changed.

Validation:
- Fresh build-release-37-global Release and runtime components succeeded.
- build-tests-37-global/results.txt: 25 QtTests, zero failures. New regression
  preserves CRLF/comments/links while changing position, restores snapshot,
  rejects invalid index, nonfinite and duplicate position without mutation.
- build-release-37-global/checks.json: native hidden UI passed, including
  slope-height interpolation, overlapping top surface and outside clicks.
- build-release-37-global/zone1.json: read-only real JMV map.txt/resources,
  global model selection and modal cancellation passed. No map was saved.
- global-map.png inspected: actual Gipat texture and existing edge markers
  are visible; UV orientation matches geographical landmarks.

Limitations: actual game placement after user save/restart remains untested.
The picker supports original allod texture bindings (Gipat, Ingos, Suslanger);
custom allods need an explicit resource binding. Real visual check used Gipat.
Unavailable private-engine skills were not used; implementation relies on the
existing public project FIG8/MMP readers and installed resources read-only.

Packaging/install evidence will be appended after manifest verification.
