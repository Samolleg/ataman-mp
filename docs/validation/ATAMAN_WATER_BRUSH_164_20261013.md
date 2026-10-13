# Ataman164 — sampled water artwork and world-brush updates

Owner scenario: on zone1 in163, RMB on water followed by LMB over another water tile did not transfer its appearance; brush movement also stalled. The owner confirmed that the intended action is copying the selected water tile's appearance, not creating a new surface.

## Cause and change

- Modern RMB sampling retained only the nearest semantic material. Numerous different water images share Water / Still, so painting that family cannot reproduce the sampled physical image.164 retains the sampled packed tile (including rotation) for Modern existing-overlay painting. A palette material selection clears the sample. Ground/material painting keeps its existing policy; water is not upscaled.
- The world-brush path republished the whole atlas, every sector and the inspector selection after each stamp. Active-map stamps now upload only changed atlas images and changed sector payloads, and preserve unchanged palette/inspector data. Undo, reopen and rollback retain the full restoration path. Paint-only updates do not rebuild the terrain shadow field.
- Programmatic restoration of the inspector's selection could emit onSelect, switch the material and cancel a stroke. Selection restoration now blocks this signal. Physical tiles from the current map are used directly instead of reimporting them and possibly refusing their type/pixels.
- RMB water sampling skips whole-family prewarming. It uses the existing map tile, creates no new atlas variants and retains native artwork/animation frames.

## Evidence in progress

All native checks use hidden Windows/OpenGL contexts, in-memory clipboard isolation, a UI lock and private copies under build-tests-164-water. Game resources are read-only inputs. No interactive owner acceptance is claimed.

- Original163 targeted overlay matrix:100 PASS /3 FAIL; manual overlay import refused and its dependent drag/Undo assertions failed. Input events reached202645us including queued work. This is diagnosis, not a passing release check.
- First164 water probe confirmed exact RMB/LMB packed value, visible native pixels, Undo/Redo and no new atlas slots. Its following Lava drag exposed the programmatic-selection cancellation; the owned hidden process was stopped after it blocked on a diagnostic dialog. This was fixed, not counted as PASS.
- Next probe: water drag and Lava drag passed. The pre-save vs reopened raw-slot comparison failed because Save legitimately compacts/reindexes the atlas. The oracle was corrected to compare committed post-save bytes plus native water artwork and rotation.
- Final original-scenario, related overlay matrix, local-brush and package results will be recorded below before handoff.

## Delivery

Pending native checks and installation. Permanent target: artifacts/EI-Mapper-Ataman-2-first-test.
