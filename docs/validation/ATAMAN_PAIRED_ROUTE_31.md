# Ataman 31 — paired route

Base: f1d8ed35be28c86eaf30c28c24c66afa3ad9bc14.
Branch: codex/paired-travel-route-v31.

- 22 focused RES/travel tests PASS, 0 failed/skipped (results-v31.txt).
- Fresh Release build: build-release-31-route, build-route31.log, exit 0.
- Hidden native --ui-widget-check PASS, travelEditorProductionRouteWorks=1.
  Actual dialog/button creation, single Undo/Redo, save/reopen checked.
- Candidate validation rejects invalid rectangles, absent selected IDs and
  occupied edge ID without partial mutation. Both endpoint pairs, #position,
  unknown map.txt fields and unrelated archive members survive save/reopen.
- Live game resources not changed. Runtime arrival/exit acceptance remains open;
  the engine fallback without #remove is not established by these tests.
