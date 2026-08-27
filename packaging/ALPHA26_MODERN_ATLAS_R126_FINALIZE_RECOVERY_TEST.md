# Alpha 26 modern-atlas R126 finalization recovery test

This revision fixes two independent failures exposed by republishing an
already-finalized modern terrain map.

## Changes

- Finalized maps whose last physical atlas page is only partly occupied now
  adopt into the live editor session. A successful publish leaves the map
  clean and ready for more editing instead of showing `reopen required`.
- Finalization now decodes retained working-atlas tiles through the complete
  working catalog and preserves their canonical four-corner material identity.
  A profile missing any logical tile signature is refused before either the
  MPR or resource archive is changed.
- Imported and generated compacted tiles restore one agreed canonical
  signature together with their physical provenance.
- New global families in a finalized profile retain their reviewed runtime
  terrain type after reopen.
- A hash-gated recovery artifact is supplied separately for the one Zone1
  profile produced by R125 before these checks existed. It does not modify the
  MPR or resource archive.

## Focused manual test

1. Use the supplied repaired Zone1 semantic profile, or let Codex install it
   after confirming the exact hash-gated replacement.
2. Open the already-finalized `zone1.mpr`; verify modern terrain painting is
   available and no cell-decoding error appears.
3. Paint a small ground patch, publish to the current MPR and selected terrain
   RES, and choose either backup option.
4. Verify publication reports success, the current map stays open, the dirty
   marker clears, and no additional save/reload prompt appears.
5. Paint a second patch and publish again to prove the finalized map remains
   editable.

## Deliberately unchanged

- The broader File Save / Save As / Publish interaction is still under design
  review. This build fixes correctness and session adoption without silently
  turning ordinary Ctrl+S into a shared-resource publication.
- Rotation-equivalent physical deduplication remains disabled.
