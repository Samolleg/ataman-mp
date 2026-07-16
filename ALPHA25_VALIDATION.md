# Alpha.25 shadow-transaction validation

Use copied MOB files only. Dirty MOB output remains disabled: this build evaluates
and logs an in-memory candidate, then refuses the save without creating or replacing
the selected destination.

Restart Endurance before the test so `logs/workflow.log` contains one clean session.
Use one MOB and one isolated edit per run. Attempt **Save As** after the edit and
choose a disposable destination name.

## Leaf-only cases

Test any useful subset, restarting or undoing fully between cases:

1. Move one existing object and confirm its placement.
2. Change one existing object's texture or model and commit with Enter/dropdown click.
3. Change one ordinary object or unit property.
4. Change one ASCII/CP1251-compatible character in the embedded MOB script.
5. Change one MOB world setting or diplomacy value.
6. Move several selected existing objects in one confirmed gesture.

Expected log sequence:

- `Shadow MOB transaction validated` with edit count, candidate size, and SHA-256.
- One `Shadow MOB leaf edit` entry per changed raw leaf, containing its section
  path, source-section hash, replacement size, and replacement hash.
- `Refusing dirty MOB save...` after the diagnostic candidate has been discarded.

No output MOB should be created. An unexpected `not applicable` result is useful
evidence; retain the complete log and note the exact UI action.

## Expected structural rejection

As a separate control, paste/create an object or add a patrol point, confirm it,
and attempt Save As.

Expected: `Shadow MOB transaction not applicable` with
`mob.projection_transaction.structure`. The save remains refused. Structural
operations require the later raw-subtree transaction layer.

## Return evidence

Send the complete current `logs/workflow.log`, the MOB filename, and a one-line
description of the isolated action. Do not send game resources unless separately
requested. Logs may contain local installation paths and should remain private.

Automated companion evidence is recorded in
[MOB_SHADOW_TRANSACTION_VALIDATION.md](MOB_SHADOW_TRANSACTION_VALIDATION.md):
254/254 installed Base/JMV MOBs passed in-memory transaction derivation and exact
reapplication, alongside the document and UI regression suites.
