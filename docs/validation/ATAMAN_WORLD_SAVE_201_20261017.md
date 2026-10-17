# Ataman201 — EI-WORLD-SAVE-201 — 2026-10-17

## Incident and preservation first

Owner reported Save refusal in199: `DXT1 atlas tile patch requires matching complete MMP headers` followed by misleading `OK: no findings`. The failing path is CView::saveWorldTerrainMaps (shared/world brush).199's previous native regression explicitly disabled world terrain and did not cover it.

Before changes, the running owner session was left open. Computer-use clicked Settings > Autosave Now at10:48. Log confirms a successful deduplicated active MPR generation with SHA256 f014583b0893aef3222952b07366975f847cbdbad6f074b88421c1e0457e2a2b; MOB generation335e122d741009d1d86e31a63c222101774249eb4fd09c121075a583a4f07082 was created at10:46. Both payloads, paired atlas and JSON were pinned under private `build-save-recovery-201-20261017/rescue`; readback hashes match metadata. RES SHA2565b012fc80fdc9fcfb122de5a1ce4fd60a9d0c3680bbd9f320339e4364004cc44. No live map/resource replacement, editor shutdown or user undo occurred during diagnosis.

This protects the active zone3obr snapshot at10:48, not unspecified later edits or other dirty world-session maps. Owner asked whether other maps are dirty before any future shutdown.

## Confirmed cause and fix

Recovery atlas contains23 pages1024x1024 and11 pages2048x2048. World Save rebuilt each modified page using `images.first().width()`, so a2048 page was encoded as1024. The complete-header guard correctly rejected it. Preserve that guard; compose each page using its own first native tile size. Carry preserved DXT tile metadata into working-package generation as in ordinary Save, and suppress an empty validation report instead of appending `OK: no findings` to failures. Refusal identifies the page.

No changes to atlas format, water/transition selection or base-HD matching; no132–176 kernel restored.

## Focused validation

Private directory `build-save-recovery-201-20261017`:
- `baseline.out`: production199 actual mouse world-brush stroke and exact shared Undo/Redo pass, then real saveWorldTerrainMaps reproduces the exact owner error. No destination publication.
- `fixed.out`: initial harness omitted MainWindow's publication-preflight callback; save reached that gate and refused. This is not counted as PASS. Harness now approves only its exact private RES path.
- `fixed2.out`:34 PASS /exit0. Real mouse strokes with two materials, exact world Undo/Redo, two shared saves, reopened sector bytes, all original MMP headers/sizes preserved, all unmodified native tile pixels (including water/transitions) exact.
- World Save intentionally clears shared-brush history as its existing confirmation states. Regression checks this checkpoint and new Undo/Redo after reopening; does not claim history survives publication.
- Fresh Release201 and both runtime components built. `final.out`:43 PASS /exit0, including paired pinned recovery MPR/RES opening, exact terrain/atlas, validated MOB recovery and byte-exact MOB roundtrip. The rescue corpus contains only DejaVu zone3obr. Private assets are excluded from Git and package.

## Delivery

Version201 reserved; concurrent200 patrol work is outside this fix. Fresh snapshot build initially based on accepted199 plus this fix; no uncommitted patrol edits silently included. Final source identity and installation receipt to be recorded after checks. Installed editor remains open until rescue completeness is established and owner closes it under the handoff contract.

User physically stopped Computer Use with Escape during a later window inventory. No further UI input in this turn; no installation or live restoration performed. Latest disk MOB already equals pinned recovery335e122d (normal Save10:53); MPR remains original83eb85ec. Private recovery-publication preparation is separate from applying it.

## Live rescue completed

Owner confirmed only zone3obr had edits. After the editor was closed, commitMapSaveBatch published the prepared MPR/RES pair with backups and exact source/readback SHA checks. Existing MOB already equalled the pinned335e122d generation and was not rewritten.1237 unrelated RES members remained exact;36 recovery members published. Receipt `build-save-recovery-201-20261017/live-restore-receipt.json` has success=true/readbackExact=true/changedFiles=2. Before images and backup files retained.

Accepted patrol200 source76c2498f is included in final201; integrated fresh Release201/runtime completed; integrated.out43 PASS/exit0. verify-live.out7 PASS/exit0 confirms the restored actual game MPR and atlas resolve and match the pinned rescue exactly (read-only hidden native reopen).
