# Ataman 78 — partial-save checkpoint (2026-09-28)

Task: EI-SAVE-CHECKPOINT-78 / EI-TRAVEL-PARTIAL-SAVE-77.
Base: af5343fab5e7b7f2ccd2e31e1827c04d338c90c7, branch codex/save-checkpoint-v78.

The world-terrain batch includes map.txt/MOB companions. It can commit before a
second atlas publication. Previously only overall success called acceptSaved;
a later failure therefore left the travel baseline older than its own disk write.
A temporary synthetic RES reproduced the exact outside-editor error on retry.

Successful verified batches now notify their companion owner immediately and
once. Travel adopts the exact committed snapshot; MOB adopts committed bytes.
Renderer adoption and later publication cannot undo that checkpoint. A cancelled
or failed second phase is not reported as whole-save success. Resource reindexing
uses actual companion commit status. Partial success is explicitly reported.
Atomic replacement failures retain Qt error code and errorString separately from
source comparison failure; backups, rollback and external-change guards remain.

Validation:
- Baseline synthetic reproduction: FAIL with map.txt changed outside this editor.
- RES/travel suite: 28 PASS, 0 FAIL, 0 SKIP. Covers rollback before checkpoint,
  successful first stage/failed second stage/retry, later edits staying dirty,
  reopen, actual external change refusal, Windows file sharing conflict and retry.
- Full release78 wrapper and both x86 runtime components: PASS.
- Staged --version: EI Mapper Endurance Edition 78.
- Native --ui-widget-check: PASS exit0, including travelEditorProductionRouteWorks.
- Finalizer and independent distribution validator: PASS.
- Fresh source/staged/installed EXE SHA256:
  5C07F111B31CC01A427FCACC5B37F028E39ABD6A34C6E653B87DE8DC18C4FB3B.
- Installed artifacts/EI-Mapper-Ataman-2-first-test: 127 managed files verified,
  6 replaced, 14856 unmanaged file hashes unchanged. No user process was running.
  Verified rollback: artifacts/rollback-EI-Mapper-Ataman-v77-to-78-20260928.
  Prior hash-owned rollback76 removed after successful verification per contract.
- Early sandbox test launches stalled; those owned processes were stopped by exact
  path. One rebuild failed because a test EXE remained locked. Neither counts as
  PASS. Fully deployed test runtime executed outside sandbox passed afterwards.

Evidence: build-tests-78-save/{release/baseline.txt,release/results.txt,ui.txt,
ui.err,version.txt,finalize.txt,validate.txt,install.txt,user-files-before.json};
build-78.log. Fresh staging: build-staging-78-save-checkpoint; no execution after
finalization. Game/mod data were neither copied for edits nor modified.

Limits: the specific OS/source failure in the original zone18 session was not
recorded by77 and is not retroactively proven. A deliberately held Windows file
reproduces replacement refusal but does not identify the original blocker.
No claim that78 fixes every HDPack publication refusal. End-to-end manual save
of the owner's world and gameplay remain untested; earlier tasks stay open.