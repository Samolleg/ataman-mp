# AI graph persistence R3

## Changelist

- Existing external `.grp` files with legacy reused component labels now produce a non-blocking warning instead of preventing the MOB ownership switch.
- `Switch MOB to External .grp` still preserves the existing sibling graph byte-for-byte and only removes the primary MOB's embedded graph.
- Regenerated graphs now deterministically rebuild their component labels before export, bake, or Test Current staging. Users no longer receive a closed technical error for a condition Endurance can repair itself.
- Truncated graphs, wrong dimensions, invalid records, and structurally invalid connections remain blocking.

## Focused test

1. Open the copied Zone 6 MPR and primary MOB with the restored same-basename `zone6.grp` beside the MPR.
2. Choose `Bake / Export AI Graph...` -> `Switch MOB to External .grp`.
3. Confirm the switch succeeds. A legacy reachability warning may be shown, but it must not block the operation or rewrite `zone6.grp`.
4. Reopen the map and confirm the graph widget identifies the external `.grp` when the primary MOB has no embedded graph.
5. With loaded preview override enabled, run `Test Current Map` and verify the selected external graph is staged.
6. Regenerate the graph, disable loaded override, and run `Test Current Map` again. Component-label reuse must not produce a blocking dialog.

Work only with copied map assets or verified backups.
