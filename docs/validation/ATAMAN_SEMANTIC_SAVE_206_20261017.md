# Ataman206: Save MPR with authoritative empty semantics

Task EI-SEMANTIC-SAVE-206,2026-10-17. Base main541f0604 /205. Scope: preserve current zone1/zone2 first, fix the reported post-save failure, exercise actual File Save MPR on private copies, Undo/Redo and reopen, build and update the permanent installation. No new transition kernel or live terrain repaint.

## Rescue and diagnosis

- Installed205 log22:45:09–19 records backups of zone1.mpr, zone2.mpr and HDPack_Terrain.res, followed by `Terrain baseline adoption failed phase:semantic-stage detail:optional member was invalid`. The disk cohort committed before this failure. The title incorrectly said no maps were saved and included the entire INFO report.
-84 live/backup files pinned with readback hashes under private `build-semantic-save-206-20261017/rescue`; `rescue-receipt.json`. Includes both MPRs, both MOBs, the whole shared RES, existing recovery generations and log.
- The existing warning was dismissed without closing a map; no edit or Undo/Redo was performed. Owner then requested no further UI control; all subsequent work is source/CLI and hidden native tests. The editor produced its own22:49:23 zone2 recovery pair; pinned separately with metadata. Its MPR is byte-identical to the committed MPR; all22 MMP members of its paired atlas are byte-identical to those in the committed shared RES. Receipts `latest-recovery.json`, `rescue-validation.json`. This establishes that the captured zone2 current terrain/artwork is durable; it does not assert anything about later edits in the owner's session.
- zone2 has no embedded semantic member and no current virtual material fields. Its RES retains a version1 semantic field with old atlas hash3b8dea..., whereas the current profile is bound to f10425.... `buildTerrainSemanticFieldMember` returned without writing metadata for two empty fields. The subsequent adoption fell back to that stale RES member and failed exact binding.

## Change

- Serialize and embed both empty fields with exact MPR tile topology, atlas and profile hashes. This empty record is authoritative and shadows legacy RES state; it does not synthesize or infer new material data.
- Recovery staging uses the same explicit empty record. An explicit terrain model can be serialized independently of the active availability flag.
- Shared Save validates the staged embedded field before disk publication. After-commit warnings have a distinct saved-files title; INFO findings are excluded.
- Existing slot identities/history rebase remain in place; Save does not clear the Undo/Redo branch.

## Validation

- Baseline205: real hidden production MainWindow, QTest mouse opens File and clicks Save MPR. Private copies of pinned zone1/zone2 and full RES; private-only publication preflight. Native log reproduces the exact semantic-stage refusal after committing both maps. This expected failure is negative evidence, not PASS.
- Final206: 1251 native PASS / exit0. Fixture retains nonempty zone1 fields and empty zone2 fields, seeds the original stale RES member under the private map key, imports physical artwork without semantic painting, records full world strokes and operates the real menu. Tests cover Cancel, two-map publication, explicit empty record, paired recovery, Undo/Redo, saving Undo and Redo, fresh reopen and exact original MMP payloads (including water/transitions).

## Handoff

Fresh Release206/runtime and final native checks passed. Scoped source commit, input identity, clean staging and guarded install completed. Owner UI/game acceptance remains separate. No GitHub publication.

## Verified installation

- Owner confirmed no edits after22:54 and closed the editor. Guard retained205 until the process finished shutdown; no process was killed.
- Source main `5fca601f20d0a3b26edc790c6e364072a0671b49`; 658 compiler/resource inputs match the built/committed sources. Raw/staged206 and finalizer PASS.
- 130 installed managed files verified; owner files unchanged: `16205:733C676EE4601BD71E73A77BABED6D2B5BE6B03C81EA3578BB0C398EC10ACA72`. The five live zone1/zone2 MPR/MOB/shared RES files also still match the pinned pre-work rescue hashes (`live-preservation.json`); no game data was rewritten by this fix.
- EXE `F93EC3625CC326F1D405283324921B10865670C73FEBFA78202387374BF3E5B4`; manifest `A3EAC8AEC34F6D2289B6372F5D4C3187FD1EDC7A3C63E57374DB22075E87EB59`.
- Reconstructable205 program rollback retained at `artifacts/rollback205-to206-semantic-save-20261017`; only previous verified204 program backup retired. All map rescue data retained.
- Receipts: private `build-semantic-save-206-20261017/{build-receipt,staging-receipt,install-receipt,rollback-retirement,rescue-validation,live-preservation}.json`.
