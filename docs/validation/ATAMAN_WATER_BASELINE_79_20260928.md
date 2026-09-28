# Ataman79: water animation after terrain baseline adoption

Task: EI-WATER-ANIMATION-BASELINE-78. Date: 2026-09-28.
Branch: codex/water-animation-baseline-v79.
Base: 2cc7bab27a53316752d67baa98f73d69acb2003c (Ataman78).

## Confirmed cause

The owner's video concerns zone18, visible beside the active zone19. The zone18
MP before publication used animation start16/count32. Publication compacted the
frames to0..31. The backup preceding the13:02:11 save correctly uses start0.
The current4251868-byte MPR instead returned to start16, sampling land/stone
slots32..47 during water animation. This malformed MP predates78.

CView::adoptCompleteTerrainBaseline adopted the new animation into CLandscape,
but left m_mprAnimatedTiles stale. The next world save takes animation metadata
for the active map from this cache, thereby serializing the previous range.
Materials had the same missing adoption boundary. Ordinary baseline adoption
already synchronized both caches.

## Change and regression

Full adoption now updates cached MP materials/animations, renderer material and
animation data, material labels and settings UI before subsequent edits/saves.
The catalog rebuild consequently derives its protected animation slots from the
accepted range. No automatic legacy-map repair was added.

The native --ui-widget-check now constructs synthetic MPR/RES in QTemporaryDir,
loads the renderer, seeds the old16+32 editor cache, invokes the real complete
baseline adoption with a committed0+32 MP and then serializes/reopens its next
MP transaction. It verifies0+32 persisted and the renderer uses start0.
Before the fix the production adoption succeeds but the repeat-save check FAILS;
after the fix the identical case PASSES. Earlier fixture setup failures are not
counted as reproductions (the synthetic profile initially lacked valid slots).

- build-tests-79-water/before.err: reproduced stale serialized animation.
- build-tests-79-water/after.err and after.out: regression PASS; full UI PASS.
- build-tests-79-water/atlas/release/results.txt:131 PASS,0 FAIL,0 SKIP,
  including animation compaction, page bounds, remapping and save/reopen.
- Final fresh Release/package verification is recorded below when complete.

## Authorized precise zone18 restoration

Owner said "приступай" after the proposed code fix and separately backed-up
animation repair. No editor/game process was running during replacement.
Only byte4238758 changed16→0. MPR length remains4251868. Thus every SEC member,
all geometry, overlay placement, archive layout and remaining MP bytes are exact.
All32 destination frame types are water(type6); current overlays refer only to
slots0..31. Neither a whole-map rollback nor overlay remapping was performed.

Before SHA256:8226aa923d1d675afcc413ee93a9d3bb3c3169ae0b7a520b0d00f5e307aaa0fa
After SHA256:d03fa3093743df8863e0deccd0c54b3ca2a876e960fe0b90412a2e7c36656445
Unchanged zone18.mob:CF3CA9F86771D53E1848EDC8D7DC81290582663F94FE00687EB6DD70E2304D49
Unchanged HDPack_Terrain.res:F7B5C6F6B46D916F329FB43A688C448554C11377114E4C9A7CF38D7781F8718F

Backup directory:
C:/Users/samol/AppData/Local/EI Mapper Endurance/backups/water-animation-zone18-20260928-v79/
Files: zone18.mpr.before-water-animation.backup and zone18.mpr.atomic-replace.backup,
both verified against the before hash. The initial .NET Replace call with a null
backup argument failed before changing the source; retry with an explicit backup
path succeeded. Result: build-tests-79-water/zone18-repair-result.json.

## Limits and remaining acceptance

No interactive editor/game was launched for the owner. Visually inspect water
in zone18 both active and neighboring zone19, save again, reopen and inspect.
The automated test proves adoption/serialization, not the original video's
full gameplay/animation rendering. Earlier publication OS failures and other
open terrain/patrol/world-brush tasks remain open.

## Final build and handoff

- Fresh build: build-release-79-water-final; build-79-final.log. No build errors.
- Stage: build-staging-79-water-baseline, version command reports79.
- Final native --ui-widget-check: exit0, terrainBaselineAnimationWorks=1.
- Repaired zone18 --validate --json: exit0,257 members, byteIdentical=true.
- Finalizer and independent validate_distribution: PASS. No post-finalize runs.
- Raw/staged/installed EXE SHA256:
  F9B68DABCFFD38CA0DF99AC96DD29F1A80FE509C4E12355CEF26144BF5EED8C2
- Installed to C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test.
  All127 managed files verified,6 replaced,15194 unmanaged user files retain hashes.
- Verified rollback78 retained in artifacts/rollback-EI-Mapper-Ataman-v78-to-79-20260928.
  Earlier owned77-to-78 rollback removed only after hash/containment verification,
  as required by the handoff contract. No other installation or Endurance changed.
- Evidence: build-tests-79-water/{final-ui.out,final-ui.err,version.out,
  zone18-validate.out,finalize.log,validate-package.log,install-result.txt,
  unmanaged-result.txt}. No game assets or build outputs are committed.

Source identity: the scoped Ataman79 commit containing this report, on the branch
named above; its full SHA is supplied in the final handoff. Unrelated dirty52
validation notes, translated QM and pre-existing untracked files are excluded.
