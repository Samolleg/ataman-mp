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

Source commit: `5a4f06dc8618644d80ccb24d432e135432634c59`, branch
`codex/boundary-fill-v7`. Fresh `build-release-7-boundary` compiled with the
verified retained Qt 5.12.12/MinGW 7.3 x64 kit; x86 bridge and host rebuilt.
The original Endurance checkout/toolchain were used read-only.

- Release and runtime build PASS (`build-boundary7.log`).
- Executable `--version`, database creation and resource navigation PASS;
  scenario integration 47 passed, 0 failed (`build-release-7-boundary/checks.json`).
- Additional focused widget regression: 5 passed, 0 failed, including sculpt
  preview admission and protected world controls (`build-tests-7-widgets/regression.txt`).
- Fresh owned staging `build-staging-7-boundary`; only old manifest-owned program
  files copied, followed by the newly built exe/runtime, VERSION, Readme and one
  version-7 CHANGELOG. windeployqt and isolated-PATH `--version` PASS. Finalizer
  PASS; its first wrapper invocation could not resolve powershell.exe after PATH
  isolation, so the finalizer was invoked using its verified absolute system path.
- Independent checks: 100 manifest entries, one-to-one coverage, all SHA-256
  values, current changelist, executable equality, runtime equality PASS.
- Installed in `artifacts/EI-Mapper-Ataman-2-first-test`, version 7. Application
  was closed. Six managed program files replaced; none removed. All 331 unlisted
  user files retained byte-for-byte. Full installed manifest verified afterwards.
- Current rollback of replaced version-6 files: `build-rollback-boundary7`.
  The previous owned `build-rollback-ui6` was hash-verified and removed only
  after the successful new installation, per the single-generation contract.
- Executable SHA-256 (build, stage and installation):
  `48D5A67E2EB1018A767E5A8A0135EF6E11429D4E14FDBE58486F34119F5C7E49`.

Manual viewport acceptance on the user's original map remains pending. The
general `--ui-widget-check` result is recorded below; a timeout is not PASS.

`--ui-widget-check`: timeout at 120 seconds, terminated only this owned headless
test process. This repeats the version-2/version-6 EI-BUILD-001-UI limitation;
it does not replace the passed focused mode tests or constitute visual acceptance.
