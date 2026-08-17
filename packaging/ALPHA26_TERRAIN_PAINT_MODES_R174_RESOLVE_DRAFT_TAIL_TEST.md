# Alpha 26 terrain paint modes R174: Resolve and draft-tail test

This focused build fixes the two Zone 2 regressions reported against R173.

## Changes

- A Manual tile painted before any Modern operation now supplies its decoded
  four-corner intent to the first **Resolve missing blends** command. The first
  Resolve therefore behaves like later Resolves instead of letting surrounding
  legacy cells erase the newly painted tile.
- Saving an editable modern draft now publishes generated tiles that reuse the
  empty logical tail of an already existing atlas page. The MPR can no longer
  reference those slots while the paired RES still contains transparent
  padding.
- Reused-tail publication preserves the base pixels of authored neighbouring
  slots and rebuilds shared far-distance mip blocks from the complete page.
- Draft adoption validates the reopened MPR, RES pages, atlas profile, hashes,
  and optional semantic field before changing the live session. An omitted
  semantic member is no longer treated as a late adoption failure.
- Adoption failures now identify their exact phase and attempt to restore the
  previous renderer preview.

## Focused test

1. Open a clean Zone 2 MPR.
2. In Manual mode, paint atlas slot 68 into cell `(38,13)`.
3. Press **Shift+R** once. The tile must stay and its blended boundary must
   expand outward on this first Resolve.
4. Undo Resolve and Manual paint, then repeat steps 2-3. The result must be the
   same as the first attempt.
5. Save an editable modern draft to a new MPR/RES pair. It must finish without
   a `reopen required` adoption error, and the current landscape must not show
   cyan holes.
6. Reopen the new draft. It must contain no cyan holes, and further Modern or
   Manual painting must remain usable.

The previously supplied `zone2almostgood` pair is retained only as regression
evidence. It already contains 16 referenced transparent slots and is not a
valid baseline for this retest; create a fresh draft from clean Zone 2.

