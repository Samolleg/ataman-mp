# Ataman 67: half-step three-map junction

Date: 2026-09-25. Task: EI-WORLD-BRUSH-TRIPLE-67, child of
EI-WORLD-BRUSH-ZONE11-ZONE12-SEAM. Base: version66,
93d65390cbbefd1baa9950339ee11be4faf91b2c. Branch:
`codex/terrain-triple-junction-v67`.

## Reproduction and cause

User screenshot: zone11/12/13, BoundaryFill, radius1.5, error
`Ambiguous terrain junction: a rim vertex has two world positions.`
The v66 triple tests used a different orientation of the half-step T junction.
A new synthetic fixture with origins (0,0), (32,16.5), (32,-15.5)
reproduces the exact error on unchanged v66 implementation: 1 FAIL.
Evidence: `build-test-world-tools/triple-baseline.txt`.

Pairwise vertical seam quantization assigned the shared corner a grid-aligned
position, while the horizontal seam preserved the half-step map corner.
The same vertex therefore received two world positions and the transaction
correctly refused the contradictory command.

## Change

Overlap endpoints are mandatory shared knots, including a T-junction corner.
Every incident edge selects the same nearest vertex. On the canonical phase,
the tie follows the inverse canonical mapping; off-phase half-step corners
use the lower vertex. This avoids stealing a vertex already assigned to the
preceding knot. Interior seam knots keep their previous policy.
Existing representability, triangle orientation, provenance, excluded-map and
atomic-transaction checks remain enabled. No user map is repaired on opening.

## Verification

`world_terrain_sculpt_tests`: 34 PASS, 0 FAIL, 0 SKIP. Log:
`build-test-world-tools/triple-full.txt`.

- New synthetic T junction: all six brushes, three falloffs, radii1/1.5/4,
  both orientations, all six target permutations, three moving stamps.
  Sampled rendered polylines on all three arms, local footprint, byte-identical
  output for permutations, full-sector Undo/Redo, serialize/reopen equality.
- Actual zone11/12/13 read-only: origins (0,0), (512,324.5), (512,-59.5),
  brush (512,324.5), radius1.5. Six modes, all three arms, in-memory MPR
  serialization/reopen, Undo and unchanged original on-disk bytes.
  Initial vertical gap0.064859272; final maximum across three arms:
  Raise0.000217666, Lower0.000189858, Flatten0.000206024,
  Smooth0.000392152, BoundaryFill0.000253442, Noise0.000217205.
  Each is below one raw-height quantization step of these maps.
- Existing pair seams, previous triple orientation, four-map corners,
  moving strokes, save failures, consent and geometry atomicity also pass.

## Build and handoff

Source commit: `715968b8c6b4686c3a515753c44ef2e00ddd211f`.
Fresh Release: `build-release-67-triple-junction`, wrapper completed with runtime
components. Staged `--version`: `EI Mapper Endurance Edition 67`.
Sandbox UI-check exit1: ten clipboard copy/paste fields failed; this is not PASS.
Native Windows UI-check exit0, all widget/lifecycle checks PASS. Logs:
`build-test-world-tools/v67-ui.txt`, `v67-ui-native.txt`. The prior clipboard
instability remains open; no unrelated copy/paste fix is claimed.

Fresh `build-staging-67-triple-junction` finalized and independently validated.
Manifest paths/coverage/hashes independently rechecked by the installer.
EXE SHA256: `2AA948ACF39A4A919993446C624BA0E44824C4F37738B38E74CBCE41EA035FE3`.
Manifest SHA256: `9D4F2C811286B9EC0D86067D58CFAC5A4FAD26FAB3C6853A3557C461CE057038`.
Staging was not launched after finalization.

Installed at `C:\GOG Games\maped\ei-mapper-ataman\artifacts\EI-Mapper-Ataman-2-first-test`.
No running target process or reparse points before replacement. 101 managed
files verified, 6 program files replaced, 0 obsolete removed, 14501 unmanaged
user files verified unchanged. Installation log:
`build-release-67-triple-junction/install.log`. Current rollback:
`artifacts/rollback-EI-Mapper-Ataman-v66-to-67-20260925`.
Older rollback directories retained after the earlier auto-review cleanup
rejection; no deletion retry or bypass was performed.

Interactive acceptance on the user's edited maps and game rendering remain
open. Synthetic and read-only in-memory checks do not replace them.
