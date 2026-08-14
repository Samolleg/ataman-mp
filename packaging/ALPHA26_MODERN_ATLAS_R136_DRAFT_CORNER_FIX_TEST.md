# Alpha 26 modern-atlas R136 draft corner fix test

This revision fixes modern Material painting after the editable-draft to
finalized-MPR workflow. Draft profiles now preserve the same shared-corner
material field as the source and finalized map.

## Changes

- Working profile serialization now converts terrain-grid corners to the
  preview-corner order declared by profile schema v8. Reopening a draft no
  longer vertically reflects every non-symmetric authored or generated tile.
- The correction is independent of whether the source catalog came directly
  from an installed atlas or from an already reopened portable modern profile.
- The regression uses a deliberately non-symmetric tile and covers both
  source-to-draft and reopened-profile-to-redraft boundaries. The previous
  symmetric fixture could not expose the reflection.
- No installed MPR, RES, semantic profile, or game file is modified by this
  build.

## Focused manual test

1. Start from a known-good stock or correctly published Zone2 MPR. Do not use
   the Zone2/Zone3Obr/Zone4 draft-final pairs previously produced by R135.
2. Use **Save editable modern draft as...** to create `zone2 test.mpr` and its
   sibling atlas RES.
3. Open that draft, then use **Save MPR As...** to publish a new finalized test
   MPR/identity. Reopen the published MPR.
4. Paint several materials across existing Zone2 material boundaries. Uniform
   interiors and blends must match the same operation performed by a one-step
   finalization; no vertically swapped or unrelated neighbor materials should
   appear.
5. Repeat draft, reopen, paint, and publish once more to exercise the second
   serialization boundary.

## Known limitations

- Profiles already written with the R135 corner-order defect are not rewritten
  automatically. Their final pixels do not provide enough evidence to identify
  the intended shared-corner field without guessing; recreate those test pairs
  from a known-good source using R136.
- Rotation-equivalent physical deduplication remains disabled by decision.
