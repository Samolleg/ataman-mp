# EI-SAVE-009 — ordinary MPR Save, 2026-09-17

## Reproduction and requested result

Owner recording shows ordinary save after terrain painting requesting an atlas
identity, then a RES archive. The workflow log confirms publish-current-target
for zone3obr, not an explicit identity-only operation. Loaded pages originate
from HDPack_Terrain.res; the dialog's textures.res wording was not the actual
source binding. The original policy always required an explicit archive choice.

The owner authorized fixing ordinary Save. Work starts from
`597347c151924f62c6e09bb6578da46c67b6ea21`, branch `codex/simple-mpr-save-v9`.
No live game MPR/RES was modified or copied during implementation/tests.

## Changes

- A semantic painting field alone no longer forces texture-atlas publication.
  Authored physical tiles use the existing ordinary MPR transaction; generated
  or imported working artwork still requires publication, including new artwork
  in an existing page slot. Persisted draft and explicit Save As behavior retained.
- Ordinary save of the current non-draft map auto-selects the generated content
  identity and the already-bound RES within configured resource roots. New pages
  are appended without authorizing replacement of differing existing members.
  Original HD-pack pages and other maps' names remain unchanged.
- No identity prompt, known-archive picker, package review, backup question or
  success modal on this ordinary path. Backups remain enabled; reopen validation,
  source-race checks, atomic file commits and RES rollback on MPR failure remain.
- Unknown/out-of-profile source binding and explicit exports retain destination
  selection. Errors and an owned game holding an archive open remain actionable
  interruptions rather than being suppressed.
- The menu retains the familiar Save MPR label. Semantic-state RES updates in
  the plain MPR path now use the existing internal-publication watcher lifecycle,
  so the application's own update is not presented as an external resource edit.

An important distinction: avoiding texture repacking does not mean forbidding
all companion metadata writes. The existing semantic-state store can update its
map-bound metadata member in RES even when the image pages are unchanged. Pure
geometry saves without that state retain the ordinary MPR-only path.

## Validation

- `build-tests-9-save/results.txt`: 14 passed, 0 failed. Save routing, MPR backups,
  source race/failure preservation, physical tile painting, exact generated
  transitions, active-stroke publication, atlas/semantic reopen and map binding.
- `build-tests-9-res/results.txt`: 17 passed, 0 failed. Includes a new synthetic
  HDPack_Terrain.res normal-save test: auto-binding, new member append, unrelated
  payload preservation, byte-identical backup, conflicting member refusal with
  unchanged archive, and retained choice for Save As/unknown binding. Existing
  RES source-race and atomic-failure tests also pass.
- GUI routing inspected in source. No interactive editor or live-game save run;
  the exact end-to-end user path remains a manual acceptance check.

Build, package verification and permanent-install update pending.
