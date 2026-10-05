# Ataman113 — EI-ROOT-BON-113

Base main f042d122bbd7ca50fdce6a8317f2f1018bf6d431, version112 to113. Owner authorized correction of the mapper/game weapon placement discrepancy. No interactive game/editor launch and no game-data writes are part of this fix.

The supplied BON-ROOT-54-60-20261005 investigation establishes that the retail skeleton root uses track position rather than its BON translation; children retain their BON translation. This report was supplied by the owner; private disassembly was not independently accessed here. The prior synthetic marker runtime experiment failed to produce visible markers and is not evidence of placement correctness.

The mapper loaded root BON and accumulated it into every descendant. At constitution(1,1,10), uhwdg03r_root offset is(-0.4178133011,-0.2297830209,-0.0367177390). For the three owner quaternions, removing that translation corrects world position by approximately(+0.340629,+0.333677,+0.036718), (+0.323922,+0.349918,+0.036718), (+0.341211,+0.333082,+0.036718). These are analytical corrections, not a measured runtime frame comparison.

## Implementation

CFigure::applyAssemblyOffset zeros the actual root offset before descendant accumulation. Geometry, part rest offsets, the full animation skeleton, navigation bounds and projection offset consumers all use the resulting effective offsets. Child/grandchild BON remains cumulative. This operates at model loading before meshes are cached, independent of object names or material. Bare FIGs without BON already have zero offsets. Cached assembly payload schema advances to2 so schema1 payloads cannot return the old geometry; production currently retires that legacy disk assembly cache. Texture cache is unchanged. Picking and connection-preview paths consume the same generated part vertices, rather than a separate render-only adjustment.

## Validation

- Fresh113 release build: PASS after correcting a test-only typo (SVertexData.position, not pos). Initial failed build is not counted as PASS. Foreign translations/ei_mapper_ru.qm restored byte-exactly from the verified preexisting snapshot after wrapper-generated translation work.
- Native root/child/grandchild fixture:21 assertions PASS at Z1/5/10, covering root placement, child accumulation, rest poses, skeleton and authored navigation bounds.
- Read-only actual mapweapons.res dagger:7 assertions PASS. Normal static assembly loader equals raw FIG geometry at Z1/5/10, exact BON agrees with supplied report, three rotated world corrections agree. Archive is read in place; no private assets copied into source/package.
- --database-creation-check PASS: creation/persistence plus synthetic root tests.
- --database-preview-check PASS for catalog weapons; full database preview PASS including item-to-lever conversion, pointer placement, cancellation, one-step Undo/Redo/save/reopen, previews and guarded candidates.
- --object-randomization-check and --assembly-catalog-check PASS; batch1000 and history checks remain intact.
- unit_animation_tests:40 passed,0 failed,0 skipped. Includes root track anchoring, child rotation/offsets, attachment propagation, morph sampling and cache round trips. First qmake invocation used an incorrect relative path; corrected before build. An initial shell invocation produced no captured test result and was not counted; explicit waited subprocess with result.txt confirms exit0.
- --ui-widget-check exit1: exactly the same10 clipboard operation zero flags as112. Two unrelated zero-valued output fields (group,text) also match. No new zero flags. This is an inherited limitation, not a full UI PASS.

Evidence: build-tests-113-root-bon/root-bon-check-native/check.out, database-creation-check-native, database-preview-check-native, database-preview-check-full-native, object-randomization-check-native, assembly-catalog-check-native, ui-widget-check-native, ui-regression.json and unit-animation/result.txt. Build/test scripts and logs remain local engineering output.

Runtime acceptance remains open: owner should reopen zone1 in113, compare the three existing daggers with the game and inspect selection plus a multicomponent model. Animation tests establish regressions against the editor contract, not complete character/game parity. Existing user edits to live zone1 were observed during work and were not overwritten. The failed diagnostic artifacts remain installed from the earlier authorized experiment; this program correction does not restore a stale map or remove user registry entries automatically.

Package/permanent-install evidence will be recorded after verification.
