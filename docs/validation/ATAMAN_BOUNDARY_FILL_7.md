# EI-FLATTEN-007 — Ataman 7, 2026-09-17

## Scope and behavior

Separate `Fill from boundary` / `По границе` sculpt mode. Previous Flatten and
its persisted raw-height target remain unchanged. The new mode has no target
height field. The discrete circular contour is the outermost lattice row inside
the radius; these vertices remain byte-identical. The interior solves a discrete
Laplace equation with that fixed contour, then applies existing strength/falloff.
The contour is sampled from the immutable state before each brush stamp.

Local terrain, existing overlay surfaces, and world terrain use the same solver.
Coincident world vertices share a solution in world units (each map retains its
maxZ and rounds to its own uint16 scale). Missing data, overlay holes and map
edges are fixed contours. Staggered/noncoincident map edges remain fixed as well;
this mode does not move the old seam or reconcile vertices outside the circle.
It preserves existing lattice geometry; it does not change XY offsets.

Each drag uses the existing undo transaction; no new save/serialization path.
For a radius too small to contain any interior lattice vertices, no edit occurs.
No installed game maps were edited or copied for these tests.

## Automated evidence before release build

- `build-tests-7-boundary/results.txt`: 16 passed, 0 failed, 1 skipped.
  Optional installed-game-map calculation skipped; synthetic fixture tests ran.
- Affine slope plus nonplanar harmonic saddle is unchanged. Internal pits/peaks
  reconstruct exactly from the contour. Half-strength and zero-strength checked;
  undo restores the full sector payload, and a repeated completed fill is a no-op.
- World brush: coincident seam, mixed maxZ, disabled map, enumeration independence,
  independence from fixed-height values 747/12345, undo/redo. Staggered edge fixed.
- Overlay fill restores the exact sector payload before the test's hole; undo
  restores the hole. Radius 64 numerical case: >10,000 interior vertices, ~100 ms
  on this machine; this is a synthetic solver timing, not viewport frame timing.
- Existing world-terrain editing/save/reopen/recovery tests also passed.
- `build-tests-7-widgets/results.txt`: 3 passed, 0 failed. Mode selection, hidden
  target, unchanged old Flatten UI, and persisted selection. Settings isolated
  into a temporary INI directory. Initial test with default settings could not
  restore selection; isolated settings corrected the test environment. Offscreen
  Qt emitted its existing missing-font-directory warnings.
- Russian translation regenerated with Qt 5.12.12 lrelease; diff check passed.

## Delivery status

Release build, staged-package verification and permanent installation pending.
Manual viewport acceptance on the user's original map remains pending. The
pre-existing general `--ui-widget-check` timeout is not a passed check.
