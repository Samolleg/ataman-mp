# Ataman 101 — EI-PAINT-OWNER-101

Base main9daad40ba301373b2d1c48ed10c9723588137fa7, installed100. Owner requested implementation after EI-ZONE9-PAINT-DIAG-100. No game-file repair or publication.

## Fix

The baseline-candidate suppression now uses the same catalog selector as final painting, constrained to valid source slots of the required runtime type and the generated transition's route owner. An opposite-owner or legacy ownerless generated tile cannot suppress the required new transition. Route-neutral authored artwork remains eligible. Existing Y-mirror correction remains intact.

## Evidence

- New data-driven full-stroke regression: both mirror policies, both painting directions, opposite/legacy-zero baseline owners. Baseline100:2 setup/cleanup PASS,8 FAIL with virtual_release_unresolved. Fixed101 full atlas suite:144 PASS,0 FAIL,0 SKIP. Tests verify exact corner identities/owner, unchanged baseline pixels, whole-stroke Undo restoring SEC bytes and virtual field.
- World terrain suite:39 PASS,0 FAIL,0 SKIP with installed DejaVu maps read only. Initial invocation used the wrong executable name and did not execute tests; corrected world_terrain_sculpt_tests result is world-final.txt.
- Fresh release wrapper build101 including runtime components: exit0. --version101 PASS.
- --ui-widget-check exit1: same ten clipboard behavior failures as100; no additional zero-valued output keys. The output also contains unrelated group/text zero counters in both runs; these are not extra failed checks. No overall UI PASS is claimed.
- Real zone9 hidden integration probe linked from release101 objects, replacing only neighbor_performance_check with the diagnostic harness: Radial1842988273 at51.67,129.22 now PASS (formerly three unresolved cells), whole-stroke Undo PASS. Gravel greenish1272309196 same point PASS+Undo. Source MPR/MOB hashes unchanged. No experimental selector or diagnostic atlas objects included in this probe or the release.
- Logs under build-tests-101-paint-owner: baseline.txt, atlas-final.txt, world-final.txt, release-build.log, ui-widget-check-native, real-radial/out.txt, real-gravel/out.txt. Baseline and fixed atlas/world executable outputs use the existing local Qt test build directories; release101 was built in a fresh shadow directory.
- Foreign translations QM restored byte-identically; unrelated roadmap/report52/untracked86 remain excluded from scoped staging.

## Delivery and limits

Source commit main a89f3bf328929e68347d7729df5b8d02f606c017. Fresh staging version/raw hashes/finalizer/independent validator PASS. Installed101 in artifacts/EI-Mapper-Ataman-2-first-test with editor/runtime absent.127 managed hashes PASS;6 changed files plus manifest;15032 unlisted user files have identical before/after hashes. EXE SHA256359C454AE7DE0A6D2A509CDC8529F3FF6003F9869DFD9672C90CA9694BC5D738. Verified7-file rollback100 retained at artifacts/rollback-EI-Mapper-Ataman-v100-to-101-20261003; prior99 rollback removed only after verification. No finalizer or interactive GUI launch on the permanent target. Evidence: stage.log/install.log and user-files-{before,after}.json.

The exact unsaved single-cell refusal from the screenshot and eight-map session have not been reconstructed. Parent painting acceptance remains open for the owner: original location, alternate materials, shared-map boundary, Undo/Redo, save/reopen. Automated evidence establishes the reproduced root-cause correction, not runtime/gameplay acceptance.
