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

## Handoff

Source: 03fe7a747ab4729cba4895fe48e90073c772d6c6.
Installed: C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test.
Finalized new build-staging-31-route; --version reports Edition 31.
Independent manifest coverage/path/reparse/hash checks: 101 managed files PASS.
EXE SHA256: d830e2828e2f4852adb72b38716128500fc3f45e8624ca7d0b17e0df331a4ab9.
Installed hashes PASS; replaced 7 program files, removed 0; preserved 7660 user files.
Only verified version 30 rollback retained in build-rollback-route31.
Logs: build-release-31-route/{checks.json,ui-widget-check.stdout.txt,stage.log,
independent-package-check.json,install-result.txt}. Finalized package not run.
Manual game acceptance of arrival/return still pending; live map.txt not repaired.
