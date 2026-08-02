# 0.3.0-alpha.26 character body-part validation r4 test

## Changes

- The missing-`hr` warning follows the verified runtime helmet fallback, not a
  zone, NID, prototype, or fixed model/hair/helmet tuple.
- Hairless database templates are excluded because the runtime skips their
  hair branch. The warning additionally requires real native-hair and helmet
  assembly members and no authored helmet-specific hair replacement.
- The concise message is
  `NID ...: missing 'hr' keeps native hair under the helmet.`
- Parts still supports applying one checkbox state to every selected row.
- Validation remains report-only and lossless. It never repairs data or blocks
  Save or Test Current Map, and it preserves unknown tokens and exact arrays.

## Focused test

1. Select the JMV profile and open `zone3obr.mob` with
   `Autozone3obradd.mob`. Confirm NID 800004 receives the concise missing-`hr`
   warning and NID 3470 does not.
2. Open a helmeted character whose database template has no hair. Confirm it
   receives no missing-`hr` warning.
3. Open a different hair-bearing standard wearable whose explicit mask omits
   `hr`. If its selected native hair and helmet members exist and there is no
   helmet-specific hair replacement, confirm it receives the same warning in
   any zone.
4. Open Parts, select multiple rows with Shift/Ctrl, and click one selected
   row's checkbox. Confirm every selected row changes to the same state.
5. Cancel Parts, or Apply only to a copied test MOB. Do not edit an installed
   MOB for this test.

## Known limitations

- Import-local appearances and missing assembly evidence remain unclassified
  rather than guessed.
- The warning proves that native hair remains selected under the helmet. Exact
  visible clipping can still depend on authored geometry and complexion.
- No automatic body-part repair is provided.
