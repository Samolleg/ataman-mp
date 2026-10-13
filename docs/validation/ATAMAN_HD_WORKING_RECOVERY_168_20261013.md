# Ataman168 — HD recovery follows working Save

Base main05ecb4f2e13dfc9c024352d34eb2598615317ae0 / installed167. Owner requested the fix on167. Game MPR/RES are read-only inputs; native mutation targets a copied zone1/HDPack under build-tests-168-hd-recovery.

## Change

Working-atlas Save reconciles the HD receipt alongside pages, profile and semantic state in its existing paired transaction. Old source mappings/provenance and source ordering are retained, unused padding and appended physical slots get explicit rows, counts/page sizes follow the saved layout. Repeat HD also reconciles pre-existing working profiles such as the owner's1024-slot zone1 with a960-slot receipt. Published profiles retain their strict original contract; malformed namespace, mappings, source order or old dimensions remain rejected. No blanket receipt removal or quality-check bypass.

The first native reproduction passed layout reconciliation but found a second bug:32 proven empty slots on the native animation page were incorrectly counted as SD enlargement. The residual upscale pass now excludes proven empty animation padding; native frames and water remain unchanged.

## Evidence

- Recovery unit contract:11 PASS /0 FAIL /0 SKIP. Tests preserve existing upscaled-SD evidence (not reclassified as trueHD), source aliases, output mappings and sourceOrder; growth/reused padding succeed, six malformed cases fail safely.
- Separate production-rebuild padding test:3 PASS /0 FAIL. The initial synthetic fixture assumed raster rows matched physical MMP slot order; corrected fixture declares every actually decoded nonempty cell as animation, leaving truly empty padding. No production quality condition was weakened.
- Intermediate native runs: original layout accepted, then false HD-quality rejection8/1 identified32 empty padding cells. Subsequent brush checks passed but private Save confirmation used QDialog.done rather than pressing QMessageBox's Yes button; it cancelled without writing. Harness now clicks the actual Yes button only for its owned paths. These probes are not release PASS.
- Final native old receipt → HD command → Qt water/Lava painting → working draft Save/reopen → HD command:78 PASS /0 FAIL, exit0 (`accepted-native.stdout.log`). The Save being checked is the production working-draft path that caused the stale receipt, not a different plain-MPR route. Native water remains128×128 with identical artwork/rotation after Save/reopen; exact Undo/Redo checks pass.
- Visually inspected actual OpenGL PNGs `accepted-native/sampled-circle-preview.png`, `sampled-water-drag-after.png`, `water-to-lava-after.png`: circular sampled-water preview, visible transferred water pattern and painted Lava strokes render without missing artwork. These are automated native scene captures, not owner interactive acceptance.
- Final executable UI widget/lifecycle check PASS, exit0 (`ui.stdout.log`, `ui.exit.txt`); clipboard isolation used in-memory backend with0 native attempts.

Delivered168 to `artifacts/EI-Mapper-Ataman-2-first-test`. Source main `5863591acd3a116e44587658f09a85b3db242df9`, based directly on167 `05ecb4f2e13dfc9c024352d34eb2598615317ae0`. Tested/staged/installed EXE SHA256 `0AAFB02316C13A479B1CD0DEA1886F721B870165C6DC3E4BDABC49AED2485293`; both --version probes report168. First raw-build version probe lacked Qt plugin search path and was stopped as an owned probe; adding the same Qt plugin path used by native tests resolved it. Fresh staging finalizer/manifest validation PASS; finalized staging was not run.

Guarded installation receipt `build-tests-168-hd-recovery/install-receipt.json`:148 managed files verified,4 replacements/1 addition,22175 unlisted user files preserved byte-for-byte. Host process guards confirmed the installed mapper closed. Rollback167 EXE/version independently verified and retained in `artifacts/rollback167-EI-HD-WORKING-RECOVERY-168-20261013`. Game source MPR/RES were not modified. Package validation report is the pre-install snapshot; this repository report records completion.

Authorized cleanup removed3401397821 bytes of this task's private fixture/backups/build/staging/unit binaries and the superseded identified rollback166. Retained compact native/unit/UI/version/finalizer logs, actual scene PNGs, install/cleanup receipts and rollback167. ROADMAP's task status updated in the working file; unrelated audit/paint roadmap additions remain outside this task's scoped commit. Interactive owner acceptance is distinct from these automated native/render checks.
