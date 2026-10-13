# Ataman162 — progress after HD confirmation

Source main `574b280a4cb6cfedfa9fe22c5391cfc057b0e721`. The owner reported an apparently frozen editor after accepting HD rebuilding, including optional variant distribution.

## Change

The preparation dialog previously ended before the expensive synchronous RES/MPR publication. A completion dialog now remains until publication, adoption and optional layout finish. Archive reading, guarded publication/cleanup, atlas binding verification and layout selection run in workers; renderer, undo and document adoption remain on the GUI thread. Immutable terrain inputs are captured by value. Existing pair checks and rollback remain in place.

The dialog shows the current step, elapsed time and an approximate remaining time for measurable stages. Work without a measurable denominator uses an indeterminate bar and explicitly says the estimate is being calculated. It does not invent an exact whole-operation countdown. Cancellation remains available during preparation but is disabled once publication starts.

## Evidence

- Production Release built successfully. EXE SHA256 `2F79AE14DCA5715054922566EBBDDC6AF2E49C05D7F12D6CE8D256F26CB377B2`.
- `publicationDialogResponsiveAndLocked`: 3 PASS /0 FAIL including lifecycle,4077ms. Timer continues during worker progress, Esc cannot dismiss the committing dialog, the Cancel button is disabled, measurable ETA and indeterminate phases are exercised. The native widget capture `build-tests-162-progress/publication-progress.png` was visually inspected: readable step/elapsed/estimate text and animated bar layout.
- Initial native fixture attempts are not acceptance: the first was rejected by the harness's historical private-directory guard; the second used Base zone2 against a DejaVu HDPack profile and was rejected before confirmation (24 PASS /7 dependent FAIL). No game files were changed. The fixture was corrected to a matching private copy of DejaVu zone2, SHA256 `3DF1EFB94A9C2C251C9F7C97DA7C6C2278A5EB70A015DF6ACEF26ED2A426CEE1`.
- The current DejaVu map was already HD (24 PASS /6 dependent FAIL), so it did not exercise publication. The final fixture uses verified pre-conversion backups: MPR SHA256 `86392b9408830981c9334ae43de8f4b3243846fba2cb31f97c7385a376bfe802`, RES SHA256 `afbe5eaa5547664b58afac4b18f4d9a7455374e004de3f9643e262ada67995a8`.
- Full native Windows OpenGL scenario: **34 PASS /0 FAIL**, exit0. HD publication/adoption, optional distribution, Undo/Redo, single-map shared Save and fresh reopen all pass, including native animation pixels/ranges. Completion timer observed899 ticks; maximum GUI gap1238ms, below3000ms. Native after/reopened terrain images were captured; the after image was visually inspected. This is a responsiveness/save change, not a new texture-art acceptance.
- Final native `--ui-widget-check`: exit0. Clipboard backend remains in-memory with0 native attempts.
- Clean package finalizer passed. Guarded161→162 installation completed in `artifacts/EI-Mapper-Ataman-2-first-test`:142 managed files,4 replaced,1 added. All24038 unlisted owner files, including the HD library, retained byte-for-byte; digest `CE1C1F14558B134FD2E730FA0CD40CFFFFFE2A36DA57272E0DECED2DE0B3E823`. Program hashes match the finalized manifest. Packaged report predates installation; this repository report records the final receipt without modifying finalized package bytes.
- Deleted6432986079 bytes of owned temporary source/build/staging, private map/archive/backup copies and the positively identified previous rollback160. Retained verified rollback161 and small logs, images and receipts. No live game files were modified. Remaining manual check: the owner's interactive observation on their next HD conversion; automated native checks do not replace it.

Logs and compact receipts are in `build-tests-162-progress`. Native diagnostic windows are hidden and use the in-memory clipboard backend. The owner's interactive acceptance remains separate from these automated native UI/terrain checks.
