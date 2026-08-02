# 0.3.0-alpha.26 character body-part validation r2 test

## Changes

- Missing `hr` is no longer treated as a general helmet error. Only the
  confirmed JMV zone3 profile/prototype/model/mask fingerprint is reported.
- Body-part findings now show only the NID and a concise problem description;
  the MOB path and object section remain available through finding navigation.
- Clicking a checkbox while multiple Parts rows are selected now applies the
  new checked state to every selected part and preserves the selection.
- Validation remains report-only: it does not repair data, block Save, or block
  Test Current Map. Unknown tokens and exact serialized arrays stay lossless.

## Focused test

1. Select the JMV profile and open `zone3obr.mob` with
   `Autozone3obradd.mob`. Confirm only NID 800004 receives the concise confirmed
   zone3 missing-`hr` warning.
2. Open one of the previously noisy helmeted characters without `hr`. Confirm
   it no longer receives a missing-hair warning unless it is the confirmed
   zone3 fingerprint.
3. Open Parts, select two or more rows with Shift/Ctrl, then click one selected
   row's checkbox. Confirm every selected row changes to the same state and the
   rows remain selected.
4. Cancel Parts to discard the checkbox experiment, or Apply it to a copied
   test MOB only. Do not edit an installed MOB for this test.
5. Open `zone12.mob` and confirm NID 6257 still reports the concise
   `rh3.pike00` / `rh3.tube00` overlap.

## Known limitations

- The missing-hair rule is intentionally conservative. Another broken case
  needs a controlled working/broken pair before receiving a new fingerprint.
- Missing core anatomy remains a clipping-risk warning, not a proven crash
  rule. No automatic body-part repair is provided.
