# Ataman195 — EI-BASE-HD-CLICK-195 — 2026-10-16

## Defect and correction

Owner recording shows the HD progress window opening and disappearing without applying changes. In194, completion called QProgressDialog::close(), which emitted canceled synchronously and set the same cancellation flag used by the worker. The prepared plan was then discarded and the UI intentionally suppressed the Cancelled message.

The cancellation connection is now disconnected only after the worker finishes and its result is retrieved, before programmatic close. Actual Cancel input while work is running retains its original behavior. Completion/stoppage is logged with the summary. Material matching, water, transitions and save routes are unchanged.

## Verification

Minimal Qt5.12.12 probe reproduced before_close_cancelled=0 / after_close_cancelled=1. The updated production-linked test sends QTest mouse events to the actual landscape conversion button and the progress dialog Cancel button. On accepted194 it passed real Cancel, unchanged atlas/terrain and no Undo entry, then failed the success-dialog assertion exactly as the reported defect (repro-194.out, exit1).

Fresh Release195 and both runtime components built from accepted194 plus this scoped fix. The same test passed152 checks/exit0 against Release195 on a private zone19 copy:38014 changed cells. Receipt records151 assertions plus the final receipt-write assertion. It covers successful button wiring, success message, one Undo entry, cancellation, Undo/Redo before/after Save, two Saves/reopen, original MMP page byte equality and native GPU readback. Live map/resource hashes are guarded by the runner. No interactive editor is launched on the owner's data.

Private evidence: build-base-hd-195-20261016/{repro-194.out,check.out,live-inputs.json,results/receipt.json,release.log}. Previous193 corpus coverage41/41 remains applicable because the matcher and data algorithms are unchanged. Owner visible-window/game acceptance remains separate.