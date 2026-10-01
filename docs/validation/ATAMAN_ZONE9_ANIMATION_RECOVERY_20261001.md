# Zone9: historical animation addresses after atlas publication

Task EI-ZONE9-ATLAS-89, 2026-10-01. Branch codex/zone9-animation-recovery.
Base HEAD9efc8a17a39fe33829d285e406c05b2c518226b9; current application88,
tested code2aa8369d1133995ba6c52d3d0fac753e1f2a7ce8. No new application version:
this is repair of historical map data; the current adoption code already contains
the fix from79. No program binary, Endurance or unrelated working changes modified.

## Reproduction and cause

Video20261001-0640-17.5837589.mp4 shows squares of land cycling textures on zone9.
Extraction/contact sheet: build-tests-89-zone9. Workflow and runtime logs resolve
all18 atlas pages; missing resources are not the cause.

Current source: C:/GOG Games/PZ/MODS/DejaVu/Maps/zone9.mpr,1513715 bytes,
141 members, namespace zone9_10db87221e5b. MP animations retained130+32/175+8
after compaction. Those slots now contain mixed land types0/1/2, explaining
animated land patches. Actual frames0..31 have water type6;32..39 swamp type14.
Existing backup history proves the same corrupted file was present on26 September
and again29 September, before the adoption fix79. Installing newer code does not
automatically repair persisted data.

All40 proposed destination frames were compared in order with retained original
zone9 MMP frames in HDPack_Terrain.res. Decoded RGBA mean channel differences
after reencoding: maximum0.822754 for water,0.255433 for swamp (scale0..255).
This confirms artwork/order, not merely a guess based on terrain types. Raw DXT
blocks differ; the initial exploratory row selection was corrected to7-row per
decodeMmpDxt1AtlasTile before these final comparisons. No false raw match claimed.
Evidence: history.json, repair-plan.json, inspect_zone9.py in the ignored local
evidence directory. Resource payloads are not committed.

## Native candidate and regressions

A separate native helper links actual Release88 document/terrain objects. Reads
the live map, changes starts in memory with appendMapParametersEdit/applyTransaction,
freshly reopens, loads CTerrainEditModel and compares every unaffected archive
member. PASS, exit0, exact planned SHA and two-byte diff. Initial helper link
failed for missing moc_log.o; after adding the required generated logger object
it passed. The failed attempt is not counted as PASS.

Fresh hidden --ui-widget-check on the same Release88: exit0,
terrainBaselineAnimationWorks=1, production baseline/repeat-save PASS.
This verifies the existing code repair and related UI regressions. No interactive
editor/game was launched for the owner and gameplay is not declared PASS.
Evidence: native-repair-check.log and ui-regression/check.out/check.err.

## Authorized exact restoration

The owner explicitly approved the exact live-map repair and copying with backup,
and confirmed editor/game closed. Process absence was independently checked.
Only bytes1506658 (130→0) and1506662 (175→32) changed. Animation counts32/8,
every other MP byte, every SEC/unknown member, archive layout, length and geometry
remain byte-identical. No whole-map rollback, atlas regeneration or MOB edit.

Before SHA256:
01494aedea445e9f0c32ef0a120b0d3808b2dfcb64d1099ad460f10dc563a397.
After SHA256:
b036e850c11166535a516b4ff7bda74e2ae043d2dca0f7eaab0bfa85277bcdba.
Unchanged HDPack_Terrain.res:
8e196b2dcdc0c490ec21102471bd2b5fb7a766d7c2cd4139c2badeb5349b4ae4.
Unchanged zone9.mob:
59b51ef00afff7ef79e497b3b779c9d3791eccf02f5f4fdaf92fd4d0f2970022.

Verified backup folder:
C:/Users/samol/AppData/Local/EI Mapper Endurance/backups/zone9-animation-recovery-20261001/.
zone9.mpr.before-animation-repair.backup and zone9.mpr.atomic-replace.backup
both match the original hash; checked-candidate retained separately.
Replacement used File.Replace with explicit backup, pinned MPR/RES hashes and
final protected-file checks. No open editor/runtime process was killed.

Independent final checks: exact two-byte diff, both original backups, starts0/32
with counts32/8, final MPR --validate --json exit0,141 members,byteIdentical=true,
no errors/warnings. See repair-result.json,
verify_final.py, final-validate.out/.err. Fresh parser validation and synthetic
regression are not visual game acceptance.

Status: exact file restoration applied and validated; owner should reopen zone9,
test the same location and inspect land/water/swamp animation. Prior unrelated
terrain, patrol, brush and transition goals remain open.
