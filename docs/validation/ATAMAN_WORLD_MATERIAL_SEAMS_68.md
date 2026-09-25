# Ataman 68: shared material boundary repair

Date: 2026-09-25. Task: EI-WORLD-MATERIAL-SEAMS.
Branch: `codex/world-material-seams-v68`.
Base: `e52d1f0387a3094a518516ae11e5e06fa8039faa`.
Owner authorized implementation with «делай». No game resources were edited.

## Reproduction and correction

The old repair samples nominal XY material vertices, rejects unequal endpoint
samples on offset edges, and modifies only the active map's field. A deterministic
test reproduces a stale seam corner despite a uniform physical neighboring tile.
The new planner uses actual displaced perimeter segments and global semantic
materials. Uniform physical tile signatures outrank recovered corner evidence.
Conflicting or insufficient evidence preserves the original corner and is reported.
All maps are planned from immutable snapshots, including the deformed T junction.
Neighbor edits are restricted to incident cells of shared boundary vertices.
The active map retains its existing full-map repair scope.

Both land and overlay repair stage complete physical atlas/sector changes, retain
runtime terrain types, protect existing artwork slots and commit as one world
history entry. Failure before commit leaves the repair unapplied. Preview failure
rolls the cohort back. Existing world save/undo machinery is reused.
Native diagnostic loading can explicitly disable recovery-file creation; ordinary
map opening keeps its default backup behavior.

## Evidence

- `build-test-world-tools/material-full.txt`: 36 PASS, 0 FAIL, 0 SKIP.
  Added legacy reproduction, uniform physical evidence, reversed input order,
  excluded map, deformed triple geometry and conflicting material tests.
  Existing six terrain brush and world transaction regressions retained.
- Real map fixture changed since v67: already healed BoundaryFill can legitimately
  produce no command. Its test now permits that only for BoundaryFill when the
  pre-existing measured gap is below height quantization; geometry checks remain.
  Initial old assertion failed (35 PASS / 1 FAIL); this was not counted as PASS.
- `build-test-world-tools/material-doc-tests.txt`: 11 PASS, 0 FAIL, 0 SKIP,
  including physical atlas publication, original artwork, semantic blend fallback,
  ground/overlay resolution and embedded RES profile round trips.
- `build-test-world-tools/material-native-final.txt`: hidden native OpenGL run,
  exit0. Actual zone11/12/13 loaded read-only; 22 physical cells repaired (zone11:12,
  zone12:0, zone13:10), with semantic state changes in three maps. Requested
  material corners match physical tile signatures; runtime types, original pixels
  and neighbor interiors preserved. Whole-cohort Undo/Redo, physical and semantic
  idempotence, MPR/atlas serialization and reopening in memory PASS.
  Hashes confirm original MPR/MOB game files unchanged. 31 ambiguous boundary
  points remain explicitly reported. The diagnostic does not save user maps.
- An initial diagnostic attempted the normal backup path and exited1 under the
  sandbox. It was not counted as PASS; the final read-only diagnostic disables
  that side effect explicitly.

## Acceptance boundaries

Physical signatures and bytes were checked, not just material-field IDs. This
does not prove pixel-perfect continuity of mixed texture masks on offset grids.
No custom world-space mask synthesis is implemented. Conflicting uniform materials
are not resolved by arbitrary map order. User visual acceptance of the screenshot
case, interactive save/reopen and game rendering remain open. Older patrol,
water, basecam and unrelated transition tasks are not closed by these tests.

## Build and handoff

Fresh shadow: `build-release-68-material-seams`.
Owned private stage: `build-staging-68-material-seams`.
Final package and installation evidence will be appended after verification.

Release build PASS. Staged --version: EI Mapper Endurance Edition 68.
Sandbox UI-check exit1 (clipboard instability), not PASS. Native Windows
UI-check exit0: widget and secondary-window lifecycle PASS. Logs:
`build-test-world-tools/v68-ui.txt` and `v68-ui-native.txt`.

Source commit: `2556957444256868cc3f695d079fc2536f425633`.
Finalizer and distribution validator PASS; installer independently verified all
manifest paths, coverage and hashes. Staging was not run after finalization.
EXE SHA256: `40DC533F177289A1F135F2DDE5722264489F9EF437AAB8C0C770664201EBEB41`.
Manifest SHA256: `22D5BBF6193F2732460EBB236C4AA39E873EA4052DC6542BE0EE6FC84766BF30`.

Installed at `C:\GOG Games\maped\ei-mapper-ataman\artifacts\EI-Mapper-Ataman-2-first-test`.
No running target process or reparse points before update. 101 managed files
verified; 14877 user files hash-verified unchanged; 6 program files replaced,
0 obsolete files removed. Log: `build-release-68-material-seams/install.log`.
Rollback: `artifacts/rollback-EI-Mapper-Ataman-v67-to-68-20260925`.
Older rollback generations retained because earlier automatic approval review
rejected cleanup; no deletion retry or bypass was performed in this task.