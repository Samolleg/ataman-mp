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

Fresh207 Release/runtime and final native check passed. Scoped commit, source identity, clean staging and permanent installation completed. User's existing session is preserved; no editor will be killed or loaded binary replaced. No GitHub publication.

## Verified installation

- Source main `7f95ffb3de7589191889a80b237f143784ca7ea9`;658 compiler/resource inputs match built/committed source. Parser7 + family37 QtTest, final native103 PASS/exit0. Raw/staged207/finalizer PASS.
- 130 installed managed files verified. Owner files unchanged: `16745:2D49E105FC9DE58C6AED0FC8D841D4568D7D6D7DA28399E3B0F523C8B9BE676D`.
- EXE `AB69936840CBE39FA991ECEBE991FB6436A6A2425CDEBA87AAF388C7FA9FFE00`; manifest `A31F53AF7BBE1583D1419B8B2FA6089AACD07F4B9FACFA0DC9684B888A36DE0C`.
- Verified reconstructable206 program rollback: `artifacts/rollback206-to207-script-nids-20261018`. Only previous identified205 program backup retired; unrelated/rescue backups retained.
- Private receipts `build-script-nids-207-20261018/{build-receipt,staging-receipt,install-receipt,rollback-retirement}.json`. No owner UI control or live MOB renumbering; existing1804 unchanged. No GitHub publication.
