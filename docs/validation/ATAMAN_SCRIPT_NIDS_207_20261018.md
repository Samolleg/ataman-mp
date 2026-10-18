# Ataman207: script-aware new object IDs

Task EI-SCRIPT-NIDS-207,2026-10-18. Base main2da18d2c / installed206. Scope: protect new object IDs against scenario lookups. No renumbering or rewriting existing objects/scripts. Work personally in main; preserve unrelated ROADMAP work. No owner-window automation. All tests use synthetic private MOB files.

## Diagnosis and implementation

- Installed206 log14:47:07 explicitly allocated1804 to a new zone1.mob object. At14:47:54 deletion preflight found `LoneWolf = GetObjectByID("1804")`, line3143. This proves a scenario-reference collision; it does not by itself prove two physical objects with that ID.
- Reuse the existing lexical analyzer through an all-references overload: GetObject(number), GetObjectByID("number"), case insensitive, comments and quoted prose excluded, overflow rejected. The filtered deletion/repair API remains available.
- Family validation caches script-referenced IDs for every discovered member, including disk-only AddMob dependencies. Suggestions skip that set as well as occupied object/auxiliary/session IDs.
- MainWindow adds live loaded-MOB script reservations, including unsaved text. CMob's local allocation also reserves own-script references. Text-keyed caches avoid reparsing during batch creation.
- Previously seen script references remain reserved through the loaded-MOB/workspace session. The setter captures old and new text even before the first allocation. Otherwise removing a reference, creating an object, then Undo/Redo of the script could recreate the collision.
- Only direct literal lookups can be established statically. Computed expressions/variables are not evaluated; arbitrary numbers in coordinates, flags, comments or prose do not reserve IDs. Existing mistaken object1804 is not automatically repaired.

## Validation

- QtTest parser:7 passed /0 failed /0 skipped. Includes1804, numeric GetObject with comments/decimal zeros, UINT_MAX, overflow, computed expressions, quoted prose and unrelated numbers.
- QtTest family:37 passed /0 failed /0 skipped. Includes forward reference1804 with no object, disk-only script dependency1805, live override, range maximum/exhaustion and existing MOB-family regressions.
- Initial native assembly catalog check:0 failures, including actual create/clone/paste,143-object production batch and Undo/Redo. Final native after strengthening script Undo reservation:103 PASS checks /0 failures /exit0, including creating an object between script Undo and Redo, exact batch IDs through Undo/Redo and standalone allocation after replacement/clearing of script. No legacy kernel or live game data changed.

## Handoff

Fresh207 Release/runtime and final native check passed. Scoped commit, source identity, clean staging and permanent installation pending. User's existing session is preserved; no editor will be killed or loaded binary replaced. No GitHub publication.
