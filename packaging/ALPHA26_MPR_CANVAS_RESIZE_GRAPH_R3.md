# 0.3.0-alpha.26 MPR canvas resize and graph staging R3

## Changelist

- Canvas placement now uses the same north-up orientation in the preview and
  resulting map. Right-drag the original map on the preview grid to set its
  left/top tile offsets.
- Canvas resize shows blocking progress while validating, building, committing,
  and reopening its outputs.
- A separately saved companion MOB now reopens with the resized MPR. Objects
  outside a crop or left on a requested closed-edge taper are omitted.
- MPR-only resize remains available while MOB files are loaded; those MOB files
  are explicitly ignored and remain unchanged.
- Test Current Map now validates and stages the regenerated AI graph for the
  current terrain and objects, even when the source MOB or map has an older
  graph. Source MOB, MPR, and GRP files are not modified by staging.

## Focused manual checks

1. Expand a map vertically, right-drag it to the top-left of the preview, and
   create the resized MPR. Confirm it occupies the top-left in the editor too.
2. Repeat with a companion MOB and closed-edge taper. Confirm the new MOB opens
   automatically and objects on discarded/tapered terrain are absent.
3. With a MOB loaded, leave companion generation disabled and resize only the
   MPR. Confirm the operation proceeds and the loaded/source MOB is unchanged.
4. Regenerate the navigation preview and use Test Current Map. Confirm the
   character can traverse ordinary connected terrain across the map; inspect
   all eight graph profiles if the changed geometry affects clearance.

## Known limitation

Generated graph delivery is temporary for Test Current Map. Persistent external
`.grp` export and explicit embedding into a chosen MOB remain pending.
