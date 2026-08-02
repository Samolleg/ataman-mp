# 0.3.0-alpha.26 character body-part validation r5 test

## Changes

- The missing-`hr` warning now says
  `NID ...: missing 'hr' part may cause hair to be visible with an active helmet.`
  It no longer uses the internal-looking phrase "native hair" or implies that
  visible clipping is certain.
- Parts now preserves a multi-row selection when the table collapses selection
  before delivering a click to its embedded checkbox. Clicking a checkbox in
  the selected rows applies the new state to every selected row.
- Validation remains report-only and lossless. It never repairs data or blocks
  Save or Test Current Map, and it preserves unknown tokens and exact arrays.

## Focused test

1. Open Parts and select at least two rows with Shift or Ctrl.
2. Click the checkbox in one of those selected rows. Confirm every selected row
   changes to the same state and remains selected.
3. Click a checkbox in a row outside the selection. Confirm only that row is
   selected and changed.
4. In JMV, open the zone3 character family and confirm the warning for NID
   800004 uses the new concise "may cause hair" text.
5. Cancel Parts, or Apply only to a copied test MOB. Do not edit an installed
   MOB for this test.

## Known limitations

- Import-local appearances and missing assembly evidence remain unclassified
  rather than guessed.
- The warning identifies a verified hair/helmet fallback, but exact visible
  clipping can still depend on authored geometry and complexion.
- No automatic body-part repair is provided.
