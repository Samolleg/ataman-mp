# Alpha 26 terrain paint modes R149 Classic Auto fix test

This revision repairs the Classic Auto implementation and polishes the shared
terrain-paint palette. It retains the reviewed R148 batch-one semantic catalog
(`ee5d1139`, preset SHA-256
`1D980F2DD24ADE17DD2AD3A0F37E80C0885840FB550CDC937B45FF6B6223EA89`).

## Changes

- Classic Auto now resolves the complete gesture through the authored-atlas
  whole-stroke solver. It no longer routes through Modern's virtual preview
  pipeline, and a same-semantic repaint can reselect exact physical
  variants/rotations instead of being discarded before selection.
- Existing authored transitions, including valid quarter-turn reuse, are
  preferred before any fallback. Classic Auto has fallback disabled: if even
  one required exact blend is absent, the whole stroke is refused and the
  physical atlas remains unchanged.
- When Modern's semantic field is already active, an accepted Classic Auto
  stroke updates that field and the exact physical tiles in one undoable
  command. Undo/redo therefore cannot make the two paint modes disagree.
- Working-atlas growth continues to refresh both dependent palettes
  immediately: Manual shows every newly addressable physical slot and Classic
  Auto exposes families once their exact uniform artwork is present.
- The shared filter is separator-insensitive and tokenized. Queries such as
  `grass dark`, `stone weathered`, or `ground gravel greenish` match the same
  names with slashes or punctuation in all three modes.
- Manual is now an icon-only tile grid. Slot, runtime type, and semantic-corner
  information are shown in the tooltip, and the selected tile is redrawn in
  the viewport immediately without requiring another mouse movement.

## Focused manual test

1. Open a supported map, select **Classic Auto**, and repaint an area whose
   authored blends already exist. Confirm the stroke paints and a later stroke
   over the same semantic material can vary valid artwork without changing the
   material boundaries.
2. In Classic Auto, attempt a combination missing one exact blend. Confirm the
   entire stroke is refused with no partial terrain change and no atlas growth.
3. Paint a new exact transition in **Modern**, then switch modes without
   saving or reopening. Confirm the new physical slot appears in **Manual** and
   its now-available family remains usable in **Classic Auto**.
4. Switch between Modern and Classic Auto on the same edited area, then undo
   and redo. Confirm both modes retain the same material identities and exact
   packed tile arrangement.
5. Type `grass dark` and several other space-separated queries into the shared
   filter. Confirm matching cards/tiles remain visible regardless of `/`, `-`,
   or other name separators.
6. In **Manual**, select several tiles in the icon grid. Confirm each exact tile
   appears under the cursor in the viewport before painting; hover icons to see
   slot/runtime/semantic details.

## Known limitations

- Automatic semantic inference for unknown third-party atlas identities remains
  out of scope. Manual physical-slot painting remains available.
- Rotation-equivalent physical deduplication remains disabled by decision.
