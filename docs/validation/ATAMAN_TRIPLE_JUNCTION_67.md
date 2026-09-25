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

Release/package/native UI validation and installation are pending.
Interactive acceptance on the user's edited maps and game rendering remain
open. Synthetic and read-only in-memory checks do not replace them.
