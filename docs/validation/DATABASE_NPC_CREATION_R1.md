# EI-DB-NPC-PLACEMENT-001 — fresh database NPC creation

Baseline: `972b4a30342b17dcd7e46e58233a556c309c463a`, branch
`contrib/Samolleg/mob-scenario-layer`. Local implementation and tester handoff;
no installed map/resource writes, publication, or automatic repair of old actors.

## Original failure and first incorrect data

The user selected Jun Male Hero in Add from database. Preview found its JMV
skin, but Place rejected the raw `unhufeskin_41` identifier. Selecting the x4
skin manually passed that gate. The later screenshot showed the placed NID 14
with critical `mob.semantic.wearable_all_parts_enabled`.

The catalog authored Base-style skin identifiers for JMV wearable models and
left the new unit's Object parts empty. The appearance resolver independently
assembled a database preview, hiding the defective persisted mask. Empty masks
enable every assembly alternative. The validator is retained unchanged.

Before implementation, the descriptor regression failed with actual
`unhufeskin_41` versus expected `unhufeskin_41_x4`. The real hidden picker test
also failed at Place for Jun Male Hero without any manual property edits.

## Change

- Fresh JMV wearable descriptors use the same numbered x4 skin convention as
  the existing JMV appearance resolver; Base naming stays unchanged.
- New wearable units receive explicit core anatomy. Legacy human models use
  the `hr` alias; other supported wearable models use the database's numbered
  hair member when present. Integral creatures retain their own full assembly.
- The mask is authored into Object parts before preview/placement. Existing
  serialized masks and database-import policy are not normalized.

The mask policy follows the repository's existing audited evidence in
[Character body-part validation](CHARACTER_BODYPART_VALIDATION.md). No private
source or installed game assets were copied into source or the package.

## Validation

- Release wrapper with retained Qt 5.12.12 / MinGW 7.3: pass.
- Five focused profile database cases plus QtTest init/cleanup: 7 passed.
  Covers JMV skin 41, Base naming, 16-part human anatomy, numbered orc hair,
  hairless core anatomy, integral models, levers, and active provider loading.
- Actual Add from database UI against read-only installed JMV resources:
  Jun Male Hero, Jun Female Deva M1, AncientCurseM25, ArmoredTurtle110 pass on
  Desktop OpenGL and ANGLE. Each exercises untouched picker properties,
  cancellation, placement, one history command, save/reopen with database
  identity, zero family critical findings, undo, and byte-identical redo.
- Base Desktop OpenGL: Human Gipath Brigand Boots F1 passes the same workflow.
- Existing synthetic database creation check: creatures, ordinary items and
  levers pass creation, history, save/reopen and provenance rejection.
- Russian scenario integration on Desktop OpenGL: 51 passed, 0 failed.
- Jun Male Hero preview was inspected; texture and explicit anatomy are visible.
- The final executable repeats all four JMV Desktop checks successfully.
- Staging and permanent `EI-Mapper-Endurance-0.5.0-alpha-db-npc-r1-test` folders
  pass the distribution finalizer; all 92 delivered files match. Executable SHA-256:
  `289c2cac8ae200de65c4109e31e3c7d58d7846e37037137f2a54b81fc1435d08`.
  The packaged editor was not launched; older tester folders are preserved.

Reproduce the focused UI branch with native Windows Qt, `EI_TEST_NPC_CREATION_ONLY=1`,
`EI_TEST_PREVIEW_DATABASE`, `EI_TEST_RESOURCE_ROOTS` (semicolon-separated),
`EI_TEST_PREVIEW_OUTPUT`, and `--database-preview-check --desktop-opengl` or
`--angle-opengl`. Set `EI_TEST_PREVIEW_PROFILE=base-game` for Base. Settings and
maps use scratch directories. JMV's bounded tables.lua data is read through the
normal provider helper; it is not executed. The synthetic MOB's absent ID range
can produce an unrelated warning; no critical finding is suppressed.

## Remaining acceptance and uncertainty audit

User acceptance (2026-09-14): the user reports "everything works" and requests
a PR for the delivered build. The exact game/backend coverage was not specified;
the automated evidence and remaining technical limits below are unchanged.

The original user map and game runtime were not launched or modified. A newly
added NPC on the user's map and its in-game appearance remain manual acceptance.
Old NID 14 keeps its old mask until the user recreates or edits it. The earlier
terrain-ray message was passed by the user's later successful placement; its
exact camera state was not reproduced and ray behavior was not changed.

The inherited full `--ui-widget-check` gap remains: offscreen/minimal failures
and native hang were previously recorded for R3. Focused native widget tests
above passed; they do not establish full UI coverage. The complete creature
catalog has not been exercised in the game. No other material implementation
uncertainty was identified.
