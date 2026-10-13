# Ataman164 — sampled water artwork and world-brush updates

Owner scenario: on zone1 in163, RMB on water followed by LMB over another water tile did not transfer its appearance; brush movement also stalled. The owner confirmed that the intended action is copying the selected water tile's appearance, not creating a new surface.

## Cause and change

- Modern RMB sampling retained only the nearest semantic material. Numerous different water images share Water / Still, so painting that family cannot reproduce the sampled physical image.164 retains the sampled packed tile (including rotation) for Modern existing-overlay painting. A palette material selection clears the sample. Ground/material painting keeps its existing policy; water is not upscaled.
- The world-brush path republished the whole atlas, every sector and the inspector selection after each stamp. Active-map stamps now upload only changed atlas images and changed sector payloads, and preserve unchanged palette/inspector data. Undo, reopen and rollback retain the full restoration path. Paint-only updates do not rebuild the terrain shadow field.
- Programmatic restoration of the inspector's selection could emit onSelect, switch the material and cancel a stroke. Selection restoration now blocks this signal. Physical tiles from the current map are used directly instead of reimporting them and possibly refusing their type/pixels.
- RMB water sampling skips whole-family prewarming. It uses the existing map tile, creates no new atlas variants and retains native artwork/animation frames.

## Evidence

All native checks use hidden Windows/OpenGL contexts, in-memory clipboard isolation, a UI lock and private copies under build-tests-164-water. Game resources are read-only inputs. No interactive owner acceptance is claimed.

- Original163 targeted overlay matrix:100 PASS /3 FAIL; manual overlay import refused and its dependent drag/Undo assertions failed. Input events reached202645us including queued work. This is diagnosis, not a passing release check.
- First164 water probe confirmed exact RMB/LMB packed value, Undo/Redo and no new atlas slots. Its following Lava drag exposed the programmatic-selection cancellation; the owned hidden process was stopped after it blocked on a diagnostic dialog. This was fixed, not counted as PASS.
- Next probe: water drag and Lava drag passed. The pre-save vs reopened raw-slot comparison failed because Save legitimately compacts/reindexes the atlas. The oracle was corrected to compare committed post-save bytes plus native water artwork and rotation.
- Intermediate visual probe:61 PASS /2 FAIL. Map captures included the floating brush preview, which contaminated Undo pixel comparison. Map/minimap capture now excludes the brush cursor; only the final clean captures below are visual evidence.
- Final native original scenario (`build-tests-164-water/visual-final.stdout.log`):63 PASS /0 FAIL, exit0. Exact water sample and rotation, multi-stamp drag with shared brush, release, Undo/Redo with exact rendered pixels, change to Lava, Save and reopen pass. Clean before/after/drag screenshots in `visual-final/` were visually inspected.
- Local brush without shared maps (`local.stdout.log`):50 PASS /0 FAIL. Related overlay matrix (`matrix.stdout.log`):103 PASS /0 FAIL, including add/remove surfaces, Water/Lava/Swamp, tile/vertex modes, manual overlay, drag, layer preservation and Undo/Redo.
- Native widget/lifecycle check (`ui.stdout.log`):PASS, exit0. In-memory clipboard isolation:0 native attempts, clipboard unchanged. Release build and --version:164.
- Water remains128x128; animation frames, saved artwork and rotation are retained. Save can compact atlas indices and change QImage storage format, so reopen compares post-save indices and normalized RGBA pixels rather than pre-save slots or storage format.
- Measured sampled-water drag event maximum:10.3ms shared /8.8ms local. Related matrix stamp maximum182ms in163 versus122ms in164 (including queued work203ms versus139ms). These are specific native runs, not a guarantee for every map or cold operation; first cold hover still took259ms including queued work.
- Source main b35b5947ea51ce13e820838945a793ca5356ce72. EXE SHA256:1181747438B8E79467BE41D999AAE25A09F129CADD9ECA5D64CEA770404B43FD.

## Delivery

Installed164 into artifacts/EI-Mapper-Ataman-2-first-test. Clean package finalizer PASS;144 managed files,4 replacements and1 addition. All24135 user files preserved with identical digest E37D3EA84CE85C4CFE6CB28BC3F7BB3C78A53963375C17A7437B6628F89EF40C. Retained verified rollback163. Own temporary game copies/build/staging and previous rollback162 removed:9727475290 bytes. Receipts: build-tests-164-water/install-receipt.json and cleanup-receipt.json. The installed evidence copy records pre-install validation; this repository report also records delivery. Interactive owner acceptance is separate from the completed native visual checks.
