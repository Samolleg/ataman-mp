# Ataman 33 — travel drawing and close recovery

Base ec064e9a69f96a4ecf58f25b0a3ec932dd24394f; branch codex/travel-draw-close-v33.
Reproduced in code/test: enabling an absent rectangle exposes zero dimensions;
flush rejects drawing before gesture start and rejects close before Discard prompt.
Fix: start drawing absent selected area, skip only replacement geometry during
redraw preflight, leave original rectangle unchanged until gesture commit.
Close invalid-field path now offers Discard/Cancel, with explicit scope wording.

22 focused tests PASS (build-travel26-tests/results-v33.txt): invalid draft redraw,
missing-area checkbox/add button, no empty-area mutation, original selection tests.
Fresh Release build PASS (build-draw33.log).
Hidden native UI PASS: drawing/cancel cursor state, route save/reopen/Undo/Redo;
invalid-width close Cancel refuses, Discard resolves and restores valid panel.
UI log: build-release-33-draw/ui-widget-check.stdout.txt.

User video was readable after escalation, but native thumbnail extraction failed
(WinRT collection projection); its full gesture sequence was not visually verified.
Exact in-scene user reproduction and gameplay remain manual acceptance gates.
No live game data modified. Existing parent travel gameplay acceptance remains open.

Handoff source: 2aaf382ef83aaafc228d26a5b5793571789986a8.
Installed C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test.
Fresh staging build-staging-33-draw finalized, --version Edition 33 before finalization.
Independent hash/coverage/path/reparse checks PASS: 101 managed files.
EXE SHA256 abab2dfe5ad9d7efd30f1cbf2badfec55a4e507c4fd32f122a0c60cc740d7557.
Install PASS: 7 replacements, 0 removals, 8121 user files unchanged.
Verified rollback version 32 retained; older owned generation removed.
Logs: build-release-33-draw/{checks.json,stage.log,independent-package-check.json,
install-result.txt}. Finalized staging not launched afterwards.
