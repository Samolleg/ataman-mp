# Modern atlas r10 tester notes

This build presents one terrain brush per reviewed **Family / Subfamily**.
Atlas aliases and same-subfamily variations are grouped behind that brush;
different subfamilies such as `Grass / Common` and `Grass / Dark` remain
separate. Mixed transition tiles are never direct brushes. The visually mixed
`bz5g` Ground / Dry anchor and its aliases are explicitly transition-only.

The Material palette scrolls vertically and its cards fill the available width
of Landscape Tools. Missing exact 1/3, 2/2, 3/1, and four-family tuples are
generated without dropping requested stroke vertices. Inactive overlay sentinel
values are no longer mistaken for atlas slot 16383 during finalization.

`modern-atlas-real-map-tests` contains disposable visual tests made from stock
Jamais Vu terrain data: Zone 1, 2, 5_1, 11, 16, and 17 maps plus a full-family
map with one non-overlapping 3x3 stamp per reviewed ground material and dedicated
four-family junctions. Keep every `.mpr` beside its matching `-atlas.res`.

## Focused manual test

1. Open the `.mpr` in `modern-atlas-real-map-tests/full-game-family-stamp/`.
   Keep its adjacent `-atlas.res` in place and inspect all 71 separated material
   stamps for wrong-theme or mixed imagery.
2. Confirm the palette contains one card per Family / Subfamily, scrolls, and
   expands across the Landscape Tools pane. In particular, verify Common and
   Dark grass are separate cards rather than repeated atlas variants.
3. On a fresh Zone 2 copy, paint several materials across unrelated existing
   terrain. Every touched vertex must retain the selected material on release,
   render immediately, and resolve with zero unresolved blends.
4. Repeatedly add generated blends while panning over untouched terrain. The
   original atlas must remain intact and the whole map must never switch to one
   repeated texture.
5. Finalize an isolated copy, reopen it, and confirm the preserved overlay no
   longer reports the false `slot 16383 was not pinned` error.

## Expected scope and limitations

- The included full-family visual map uses 312 active slots: 34 authored slots
  reused plus 278 exact generated tiles across five texture pages. Its manifests
  report zero unresolved cells.
- The editor working atlas can paint beyond 512 slots; 512 is not a stroke-time
  refusal threshold. Shipped-game runtime proof above eight pages remains a
  separate controlled 9/16-page test before declaring that range production-safe.
- Stock installed Jamais Vu imagery is used directly in these isolated tests.
  Separate EI-HD imagery is not included and remains research-only.
- Generated transition aesthetics still require visual review. Report any
  visibly mixed brush card or harsh/incorrect blend with its tooltip text.
