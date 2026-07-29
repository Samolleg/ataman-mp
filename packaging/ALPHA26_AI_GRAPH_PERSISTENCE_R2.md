# AI graph ownership switch R2

## Changelist

- Fixed `Switch MOB to External .grp` failing with `Access is denied` on Windows.
- The switch now preserves the existing sibling `.grp` byte-for-byte. It only validates that graph and removes the primary MOB's embedded `AI_GRAPH`.
- Missing or invalid external graphs refuse the ownership change and leave the MOB untouched. Use `Export Runtime .grp` separately when a new external graph is wanted.
- MOB graph removal retains verified staging, backup, source-race checks, atomic replacement, and fresh reopen validation.

## Focused test

1. Work on a copied map/MOB/GRP set, or confirm the editor backup exists.
2. Open an MPR whose primary MOB has an embedded graph and whose exact same-basename sibling `.grp` already exists.
3. Record the `.grp` hash, then choose `Bake / Export AI Graph...` -> `Switch MOB to External .grp`.
4. Confirm the operation completes, the MOB no longer reports an embedded graph, and the `.grp` hash is unchanged.
5. Reopen the map and run `Test Current Map`; confirm the external graph is the selected runtime source.

If no sibling `.grp` exists, confirm the switch is refused without changing either file. Exporting a new graph remains a separate explicit action.
