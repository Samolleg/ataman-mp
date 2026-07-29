# AI graph bake and export R1

## Changelist

- Added **Bake / Export...** to the viewport AI graph widget and
  **Tools > Bake / Export AI Graph...** beside Export Minimap.
- The shared dialog can export the selected loaded or regenerated graph to the
  exact active-MPR sibling `.grp`, bake it into the primary MOB, or switch the
  primary MOB from embedded to external graph ownership.
- Changed graph files and MOBs are staged, freshly reopened, runtime-validated,
  backed up, race-checked, and atomically replaced. The dialog warns when an
  embedded graph shadows the external file or when one MOB is shared by several
  map registrations.

## Zone 6 manual checks

Use copied/test-mod map files rather than irreplaceable originals.

1. Load `zone6.mpr` with its primary MOB, regenerate a graph, and open
   **Bake / Export...**. Choose **Switch MOB to External .grp** and confirm the
   dialog targets `zone6.grp` and warns that the MOB is shared.
2. Load `zone6_2.mpr` with the same primary MOB, create a deliberately different
   regenerated state, export it, and confirm the target is `zone6_2.grp`.
3. Test Current Map from each active MPR and confirm navigation follows the two
   different graph states.
4. Turn **Loaded preview override** on and export once; confirm the loaded graph,
   rather than the regenerated preview, is the selected source.
5. Bake a graph into the shared primary MOB and confirm a later `.grp` export is
   reported as shadowed. Switch back to external ownership and confirm the
   embedded graph is removed only after the `.grp` is safely committed.

## Known boundary

Automated tests cover exact naming, embedded-graph replacement/removal, no-op
identity, backup, validation, and failed atomic replacement. Distinct Zone 6
navigation behavior remains a private game-side manual validation.
