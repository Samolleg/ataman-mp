# Ataman88 distribution, 2026-10-01

Task EI-DISTRIBUTION-88-20261001; branch codex/distribution-v88-20261001.
Exact build HEAD0d723b78100c0fe1fdc98c8d0c75b52f39357de8; application code
2aa8369d1133995ba6c52d3d0fac753e1f2a7ce8. No source-code/version changes.
Foreign ROADMAP distribution86 paragraph, validation52 notes, translated QM and
untracked files were preserved. QM was backed up and restored after lrelease:
EDA31955A912758ACB5A7BDE043BBF2EB5F8A2606FA32DC52AFA4156CA74ED9F.

## Fresh build and functional checks

Fresh shadow build build-release-88-distribution-20261001 via the release wrapper:
exit0, editor and current32-bit runtime components produced. Qt/compiler runtime
deployed before first EXE invocation, windeployqt exit0. Existing compiler warnings
(including strict-aliasing in types.h) remain; no build errors. Evidence under
build-tests-88-distribution-20261001/release-build.log and deploy-qt.log.

Hidden, separate synthetic-data checks with native Windows Qt plugin:
- --version: exit0, EI Mapper Endurance Edition88.
- --profile-model-catalog-check: exit0, no functional failures, screenshot PASS,
  including large texture-list/repeated-selection performance regression.
- --model-gallery-check, --database-creation-check: exit0.
- --scenario-layer-check: exit0,50 PASS/0 FAIL.
- First --ui-widget-check: exit1, selectedComponentCopy=0 and
  numericComponentPaste=0. Other checks, including terrainBaselineAnimationWorks
  and the production atlas repeat-save regression, passed. Not counted as PASS.
- Unchanged fresh EXE repeated in a new runner directory: exit0, entire UI PASS.
  Previous Release88 repeated afterward: exit0, entire UI PASS. No test expectation,
  test source, application code or clipboard policy was altered. The initial
  intermittent copy/paste failure remains recorded; its external cause is not
  established by the successful repeats. No timeouts/skips in these checks.

Evidence: checks-final.json, ui-repeat.json and per-run check.out/.err.
The initial repeat-script generation had a shell-quoting SyntaxError before any
test launch; replaced with an explicit script. It is not counted as a test run.
No interactive editor/game was launched for the owner; gameplay/manual acceptance
of earlier features and restored zone9 remains separate from packaging.

## Clean package and permanent installation

Fresh owned staging build-staging-88-distribution-20261001 was assembled only
from verified managed runtime files plus fresh editor/runtime components and
current notes. Staged --version88 and raw/staged hashes verified before finalizing.
Finalizer and independent validate_distribution PASS,127 manifest records.
Only one current changelist; no options/logs/saves/cache, developer artifacts,
profile JSON or game assets included. No ZIP requested or created.

The finalized staging folder was moved to the previously nonexistent clean target
after source/destination containment checks. No EXE was run after finalization.
Final validator PASS at:
C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-88-distribution.

Permanent installation updated at the agreed unchanged path:
C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test.
All processes from that folder were checked absent immediately before replacement.
Exactly4 managed files replaced after verified original backups; manifest replaced
last. All127 managed hashes verified afterward. Full before/after inventory proves
all13896 files outside the manifest byte-identical, including settings/user data.
Finalizer was never invoked on this permanent installation.

Raw/staged/clean-distribution/installed editor SHA256:
2EFEB25D2AFCB5FD0303F8CA30DB5177D03BAA1DACDD29388FD5BA224CFC2ACA.
Current verified rollback:
artifacts/rollback-EI-Mapper-Ataman-v88-rebuild-20261001.
Previous owned87→88 rollback deleted only after verifying its six original
files against its manifest, seven-file inventory, version87 and absence of links, and verifying
all five files of the new rollback. Other old distribution folders untouched.

Protected game data checked before/after distribution/install:
- profile model-catalog.json:
  5d61f4bf6ab689a492f3ebad21cfd6c98592f47360424b067a7727cf62f61d22.
- repaired zone9.mpr:
  b036e850c11166535a516b4ff7bda74e2ae043d2dca0f7eaab0bfa85277bcdba.
Both unchanged. No new game repairs made in this task.

Evidence: stage.log, stage-version.out, finalize.log, validate-package.log,
validate-final-distribution.log, install.log, protected-user-files.json and
finish-package.ps1. Source and private data are not included in the clean package.

Status: clean runnable distribution ready and permanent installation updated;
initial transient clipboard refusals are an explicit functional-test limitation.
Owner/recipient first-run setup and actual editing/gameplay remain manual gates.
