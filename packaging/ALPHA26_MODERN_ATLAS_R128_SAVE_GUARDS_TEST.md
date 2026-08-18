# Alpha 26 modern-atlas R128 save-guards test

This revision removes the obsolete Zone1-specific R125 recovery path. The
normal modern Save/Save As workflow and generic publication safety gates remain.

## Changes

- Removed the MPR/profile hash special case and all hard-coded Zone1 signature
  reconstruction.
- Removed the standalone R125 Zone1 repair script and recovery-only tests.
- Retained the generic requirement that every logical finalized atlas slot has
  one complete semantic signature before any MPR or `textures.res` is changed.
- Retained staged reopen validation and transactional rollback for the MPR,
  editor profile, and `textures.res`.
- Retained unified **Save MPR**, **Save MPR As**, in-session adoption, visible
  publication progress, and the separately named editable-draft action.

## Focused manual test

1. Open the Zone1 MPR successfully healed by R127.
2. Paint a small modern-material patch and choose **File > Save MPR**.
3. Confirm publication completes, the same MPR remains open and clean, and no
   second MPR destination or reload is requested.
4. Paint and save once more to confirm the complete profile remains reusable
   without any map-specific migration.

## Known limitations

- Rotation-equivalent physical deduplication remains disabled by decision.
- Unexpected pre-existing incomplete profiles fail closed; the editor does not
  guess missing material identities.
