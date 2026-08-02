# 0.3.0-alpha.26 character body-part validation r3 test

## Changes

- The missing-`hr` warning now follows the resolved JMV visual combination:
  `unhuma`, hair 01, and helmet geometry 06. It is not tied to zone3, a
  prototype name, a NID, the exact 15-part mask, or a nearby control object.
- Hairless templates and other helmet/hair combinations remain unreported;
  the full installed JMV corpus still produces only the confirmed NID 800004
  warning.
- The concise message is `NID ...: missing 'hr' for hair 01 with helmet 06.`
- Parts still supports applying one checkbox state to every selected row.
- Validation remains report-only and lossless. It never repairs data or blocks
  Save or Test Current Map, and it preserves unknown tokens and exact arrays.

## Focused test

1. Select the JMV profile and open `zone3obr.mob` with
   `Autozone3obradd.mob`. Confirm NID 800004 receives the concise missing-`hr`
   warning and NID 3470 does not.
2. Open previously noisy helmeted characters without `hr`. Confirm they do not
   receive this warning unless their resolved combination is `unhuma`, hair 01,
   and helmet geometry 06.
3. Open Parts, select multiple rows with Shift/Ctrl, and click one selected
   row's checkbox. Confirm every selected row changes to the same state.
4. Cancel Parts, or Apply only to a copied test MOB. Do not edit an installed
   MOB for this test.
5. Open `zone12.mob` and confirm NID 6257 still reports the concise
   `rh3.pike00` / `rh3.tube00` overlap.

## Known limitations

- Other missing-hair combinations remain intentionally unreported until their
  resolved model/hair/helmet dependency is independently verified.
- Missing core anatomy remains a clipping-risk warning, not a proven crash
  rule. No automatic body-part repair is provided.
