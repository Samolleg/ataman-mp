# Alpha 26 modern-atlas R132 reopened-draft topology fix test

This revision fixes the real reopened Zone3Obr draft that R131 incorrectly
refused with `terrain.virtual_identity_topology [3,0]` during **Save MPR As**.

## Changes

- Reopened editable drafts recover a deterministic working material field even
  when untouched stock cells do not form one globally exact shared-corner
  topology.
- Publication preserves every untouched cell's exact physical tile and packed
  rotation. It resolves only cells whose semantic corners were actually
  changed, so legacy terrain meaning and artwork are not silently normalized.
- Ordinary identity conversion remains strict and still refuses genuinely
  ambiguous/incomplete semantic input.
- The exact supplied Zone3Obr draft, adjacent atlas, and stored profile now have
  a repeatable read-only integration smoke that finalizes in memory, builds and
  validates the published semantic profile, and reopens the resulting MPR.

## Focused manual test

1. Open the intact `zone3obr test.mpr` editable draft with its adjacent atlas
   and stored semantic profile.
2. Choose **Save MPR As** and select a disposable destination MPR.
3. Complete the modern publication review. The save must not report
   `terrain.virtual_identity_topology [3,0]`.
4. Confirm the published MPR opens automatically, retains the same visible
   untouched terrain, and is no longer dirty immediately after publication.
5. Paint one small Ground area, save normally, reopen it, and confirm it remains
   editable and visually unchanged outside the painted area.

## Known limitations

- This build does not repair a destination MPR that an older build already
  corrupted or overwrote. Use the intact draft and a disposable destination.
- Rotation-equivalent physical deduplication remains disabled by decision.
- This build does not change the current semantic catalog or approved masks.
