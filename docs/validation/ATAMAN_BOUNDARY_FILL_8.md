# EI-FLATTEN-008 — rejected version-7 behavior, 2026-09-17

The owner's new 14.83-second recording shows Boundary Fill, radius 10, strength
1, constant falloff, world brush with zone1/zone2/zone3obr. The internal cliff
survives. Version 7 mistakenly treated staggered map seams as fixed contours.
Its test explicitly expected those internal edges to stay fixed; that criterion
did not match the owner's requested result. Version 7 is not accepted.

## Reproduction and correction

Two synthetic maps, origin (0,0)/(32,0.5), raw heights 20000/40000, maxZ 100,
circle (32,16), radius 10. The new regression test against the unmodified
version-7 world sculpt source fails: the command is empty and the cliff stays.
Evidence: `build-boundary8-analysis/baseline-results.txt`.

For a circle covered by permitted maps, the corrected world brush samples the
displaced terrain triangles along the circumference, freezes their vertices,
and reconstructs one continuous target with mean-value coordinates. No interior
height or internal seam supplies a target-height constraint. Actual map vertices
sample this common surface in world units, with existing strength/falloff and
per-map raw-height conversion. The contour is immutable during a stamp and a
stationary full-strength repeat is a no-op.

The source triangle positions use the editor's terrainVertexPosition contract.
This does not alter XY offsets, raw file serialization, or navigation sampling.
Sampling uses at least 64 contour points, spaced at most about 0.25 map units.
Only explicitly enabled editable maps supply heights. If the circle extends
beyond available map coverage, the existing clipped-domain fill is retained;
no heights are invented from unapproved maps. Local terrain and overlays retain
their version-7 discrete contour solver. Original Flatten/Smooth are unchanged.

## Tests

`build-boundary8-analysis/results.txt`: 19 passed, 0 failed, 1 skipped (optional
installed-game-map test). No installed game maps were copied or modified.

- Original reproduction now passes: both interior seam sides move toward the
  common target; points farther from the seam move too. The same points stay
  unchanged under ordinary Smooth, demonstrating the functional distinction.
- Outer circle/outside vertices retained; undo restores exact sector bytes.
- Interior seam gap in the stepped synthetic case remains below 0.0031 world
  units; fixed-height 747 is not an input to the target. Held-brush repeat no-op.
- A sloping contour spanning staggered maps is preserved; interior peaks return
  to that slope. Target enumeration reversal yields the same values.
- Existing local/overlay, mixed-height-scale, disabled-map, world undo/redo,
  save/reopen and recovery regressions pass. One existing QTemporaryDir cleanup
  warning appeared; it is not an edit to user data.

## Build and installation

Built source commit: `1397d5365453c1fa5ce693eca65330176596cede`, branch
`codex/boundary-fill-seams-v8`. Fresh version-8 shadow build with the retained
read-only Qt 5.12.12/MinGW 7.3 kit, then incremental rebuild after shared-anchor
review. Release and x86 runtime components PASS. Exact final build log:
`build-boundary8-final.log`.

- Executable version, database creation, resource navigation PASS; scenarios
  47 passed, 0 failed. New numerical/model regression: 19 passed, 1 optional skip.
- Fresh owned staging `build-staging-8-boundary`: deployed dependencies,
  isolated-PATH version 8, runtime/exe equality, finalizer PASS. One current
  changelist verified independently; all 100 manifest digests and file coverage
  verified independently by the installation guard.
- Same permanent installation `artifacts/EI-Mapper-Ataman-2-first-test` updated
  with application closed. Six managed files replaced; none removed. All 375
  unlisted user files retained with identical hashes. All installed managed
  hashes verified (`build-release-8-boundary/install-result.txt`).
- Executable SHA-256 in build/stage/installation:
  `E80A42672259BB083EE2E9DA8EA61642617D7DD07A5C7305A16CB14EBCC64ECC`.
- Version-7 changed files retained in `build-rollback-boundary8`. Prior owned
  rollback `build-rollback-boundary7` hash-verified and removed after success.

Real viewport behavior on the user's edited maps still requires acceptance;
synthetic tests are not a claim of that acceptance. General `--ui-widget-check`
again timed out at 120 seconds (existing EI-BUILD-001-UI); only the owned headless
test process was terminated. This check is not PASS.
