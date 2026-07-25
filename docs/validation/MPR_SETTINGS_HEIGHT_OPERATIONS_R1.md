# MPR Settings height operations R1

## Scope

This increment begins the consolidated **MPR Settings** workflow without
enabling structural resize or `maxZ` mutation.

Included:

- map source, embedded namespace, sector/tile/vertex dimensions, atlas/material
  counts, `maxZ`, and raw/world height-range summary;
- explicit documentation of fixed vertex counts, clamp versus `maxZ`, vertical
  scaling versus requantization, and structural resize;
- undoable whole-map raw-height offset, flatten, clamp, and normalize/remap;
- preview of changed canonical vertices, affected sectors, and before/after
  changed-value ranges;
- fail-closed invalid ranges, flat normalization, non-finite offsets, and raw
  saturation;
- the existing atlas tile window retained through **Open Atlas Tiles...** and
  through Landscape Tools manual-tile selection.

Not enabled:

- changing `maxZ`;
- crop/expand or any sector/member addition/removal;
- liquid-material mutation;
- atlas tile-type mutation.

## Automated evidence

- `endurance_document_tests`: 59 passed, 0 failed, 3 expected private-fixture
  skips;
- global operation tests preserve a two-sector `65 x 33` canonical lattice,
  emit one seam delta, update both owning SEC payloads, and pass undo/redo;
- invalid clamp/normalize ranges, flat normalization, non-finite offset, and
  saturation refusal are covered synthetically;
- Qt 5.12.12 / MinGW 7.3 release build: passed;
- non-interactive `--ui-widget-check`: passed.

## Manual test cases

Use a copied MPR or disposable test map only.

1. Open **MPR Settings...** and confirm source, namespace, sector/tile/vertex
   dimensions, `maxZ`, and raw/world ranges match the loaded MPR.
2. Open **Open Atlas Tiles...** and confirm the existing manual tile workflow
   remains available. Confirm Landscape Tools' manual-tile button still opens
   the atlas window directly.
3. Apply a small whole-map Offset. Confirm the preview reports the complete
   vertex and sector scope, the terrain updates, and one Undo/Redo reverses and
   restores it.
4. Clamp to limits inside the current range. Confirm only out-of-range terrain
   changes and repeating the same clamp becomes a no-op.
5. Normalize to a visibly narrower range. Confirm dimensions and vertex count
   do not change. Undo before continuing.
6. Flatten the whole map only on a disposable copy, confirm it is one undoable
   action, then undo it.
7. Request an offset that would exceed 0-65535. Confirm Apply remains disabled
   and explains that an explicit clamp or `maxZ` policy is required.
8. Save a deliberate edit through normal MPR Save As or direct Save. Confirm
   staged validation and backup behavior remain unchanged.
9. Leave **MPR Settings** open, apply an operation, and use the editor's normal
   Undo and Redo actions without closing the panel. Undock and redock it, close
   it with the title-bar button, then reopen it through **MPR Settings...**.
   Confirm there is no context-help button and the panel remains usable.
10. Open **Atlas Tiles...** and select **Materials**. Confirm liquid material
    records and animated-tile records can be inspected. Mutation controls must
    remain disabled in this increment.

No test requires or authorizes mutation of the live game installation.
