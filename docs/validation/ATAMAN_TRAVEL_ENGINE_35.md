# Ataman 35 — EI-TRAVEL-ENGINE-035

2026-09-19. Base ccbbfd0ae537143917b6757471c199597b611820,
branch codex/travel-engine-semantics-v35. User authorized the correction after
reading the separately researched game semantics. No private engine source or
game assets copied into this repository. Live game resources were not modified.

## Cause and correction

The game applies #deploy to both arrival and exit; later #remove overrides exit.
The old writer appended a missing deploy after existing remove, and the reader
incorrectly treated the rectangles as independent. The report from the user's
game research task established this native behavior; no game runtime test is
claimed by this mapper build.

Reader now records assignment order and effective exit geometry/angle. Active
and neighbor overlays use that geometry (including rotation). Geometry edits
write deploy before remove; unrelated edits and no-op preserve the old ordering.
An explicit panel repair applies the authored separate remove rectangle, through
normal Undo/save. Fifth angle parameters survive edits and UI metadata collection.

#exit labels remain byte-preserved metadata. Game identity, selectors, catalog,
and link validation use one-based ordinal IDs. Structured reorder is refused;
deleting an unreferenced record remaps incoming links to later records within
the same document transaction. Referenced deletion is refused with an explanation.
Direct game-to-game links remain supported; edge is optional global routing.

## Verification

- Fresh version-35 Release wrapper PASS; final incremental rebuild after review PASS.
- Map table: 18 QtTest cases PASS. Covers remove-before-deploy effective behavior,
  no-op bytes, unrelated edit preservation, explicit repair, fifth values,
  adding deploy to remove-only, following-zone preservation, duplicate labels,
  ordinal deletion migration and referenced-deletion refusal.
- RES/travel panel: 23 cases PASS, including repair button, angle preservation,
  save/reopen, rollback/conflicts, paired route, and rotation/inverse helper.
- Catalog: 9 cases PASS. One initial expectation retained the old duplicate label
  route key; updated it to the game's second ordinal and reran PASS.
- Focused RES test first failed to start (0xC0000602) with an incomplete Qt plugin
  environment; explicit matching Qt plugin path and Windows platform passed.
- Native hidden UI PASS (travelEditorProductionRouteWorks=1). Two restricted runs
  failed clipboard checks; repeat with Windows clipboard access passed. These
  initial failures are not counted as passes.
- Native hidden zone1 / OpenGL / neighbors PASS, zero integration failures;
  live MPR/MOB byte-identical. Initial wrapper had duplicate BOM and failed its
  first environment assignment; corrected wrapper and reran successfully.
- Evidence: build-tests-35-{table,res,catalog}/results.txt and
  build-release-35-semantics/{checks.json,ui-widget-check.stdout.txt,zone1.json,
  zone1.stdout.txt}. Package/installation evidence will be appended after handoff.

## Remaining acceptance

User must explicitly repair old affected records and save, then fully restart
the game. Verify arrival, orange-area departure, reverse direction, and actual
quest/party eligibility. Automated editor checks do not establish in-game
acceptance. Parent EI-TRAVEL-EDITOR remains open until that verification.

## Handoff

Source 13c04f0866ef0fd970bf5a88359a474befb3eb1f, version 35.
Fresh build-staging-35-semantics: staged --version PASS, finalizer and independent
101-file hash/path/coverage verification PASS; not launched after finalization.
EXE SHA256 05271d2a808d5c07206e909bf1d08c3d50119fcf572b9202e87c05352bba4446.
Permanent installation artifacts/EI-Mapper-Ataman-2-first-test updated:
7 managed replacements, no removals, 9075 user files preserved and verified.
Verified version-34 rollback retained in build-rollback-semantics35; positively
identified older version-33 rollback removed only after successful installation.
No archive/map auto-repair performed. Manual game acceptance remains open.
