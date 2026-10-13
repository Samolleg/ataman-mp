# Ataman166 — first paint stamp and sampled-water circle

Owner video20261013-0942-35.6771555.mp4 shows165, OverlayPaint/Modern/Vertex/Circle/size1. Sampling water changes the displayed footprint into a cell rectangle while Circle remains selected. The installed log also reports0 stamps for every attempt, including after hydration completed.

## Diagnosis

- The sampled-water path deliberately paints exact physical tiles and uses a cell-centered footprint. Its renderer always drew tile grid rectangles for cell-centered brushes, regardless of the selected Circle shape.
- Local first-stamp handling deferred a click whenever the current pointer footprint differed from the last hover. A release arriving in a different cell could defer again, ending the gesture without applying any stamp. Native regression varies hover/press/release coordinates rather than keeping the synthetic cursor stationary.
- Shared brush stamps also bypassed local stamp counters, so the0-stamp summary could describe successful world painting. This misleading diagnostic is addressed alongside the input failure.

## Evidence

Current HD zone1, private copied MPR/HDPack, installed local library read-only. Native Windows/OpenGL event path, hidden test windows and UI lock. Game originals preserved.

Installed165 existing stationary-pointer cases: shared63/0 and local62/0, including rendered before/after/Undo/Redo and Save/reopen. These do not establish that the owner's rapid gesture works. Native changed-coordinate fail-before (`abrupt-before.stdout.log`):39 PASS /4 FAIL. First stamp lost, sampled destination unchanged, no Undo transaction/native pixel change for that click. Later stable drag passed, identifying first-stamp deferral rather than missing water files. Final corrected gesture and delivery pending.

Intermediate corrected probe:46 PASS /1 FAIL. The fast release now painted a cell on the next drag's route, so that independent drag saw too few previously unpainted centers. The harness restores the first click before testing drag and checks the active Undo index when branching from Redo. A hidden grabFrameBuffer produced a blank image; it is excluded from visual evidence. Final capture renders the production outline into the known native framebuffer. One intermediate compile failed because the diagnostic block landed in the erase function; placement was corrected, final build PASS.

Final native local scenario (`final.stdout.log`):68 PASS /0 FAIL, exit0. Abrupt press/release transfers sampled water; drag, exact rendered Undo/Redo, ordinary Save/reopen and native128 water pixels/rotation pass. Shared-brush regression (`world-after.stdout.log`):66 PASS /0 FAIL, exit0; summary reports2/6 stamps and nonzero changed tiles. No second Save there. Native production circle outline in world-after/sampled-circle-preview.png and final painted pixels visually inspected. Earlier outline captures without clean overlay GL state are excluded. Widget lifecycle PASS. Clean package finalizer PASS. Installed166 into artifacts/EI-Mapper-Ataman-2-first-test after host-level process check:146 managed files,4 replacements,1 addition. All21800 user files retained with identical digest C1FB0647A79D9AD171C31BEA06E1017BF1F77CBA5790252192A4FB3272B66FAD. Retained verified rollback165. Removed6438047029 bytes of owned temporary copies/build/staging and previous rollback164. Receipts: build-tests-166-paint/install-receipt.json and cleanup-receipt.json. The installed report records validation before delivery; this repository copy also records installation. Interactive owner acceptance remains distinct from native visual inspection.
Source main adc5b71008c0738645e00eebd36252a6176292c5. EXE SHA256 CCAAF01207A0DD6B4E99DAD86645A78A5E01493DE5343A53A54AF68A076EA659.
