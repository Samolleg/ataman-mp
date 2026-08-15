# Alpha 26 terrain paint modes and RES profile test

This revision integrates Modern, Classic Auto, and Manual terrain painting and
makes the atlas RES the durable owner of its exact editor semantic profile. It
also carries the reviewed semantic atlas checkpoint through R148 batch 1
(`ee5d1139`, preset SHA-256 `1D980F2D...B6223EA89`).

## Changes

- The Landscape terrain palette now has three modes plus one shared text
  filter:
  - **Modern** reuses exact authored blends and adds a generated exact blend
    only when the requested corner tuple is missing.
  - **Classic Auto** uses only exact tiles already present in the active atlas.
    If one required tuple is missing, the entire stroke is rolled back and the
    atlas remains byte-unchanged.
  - **Manual** lists every addressable physical atlas slot and paints the
    selected slot/rotation directly for small corrections.
- Classic Auto and Manual refresh immediately when Modern adds a working slot,
  and rebuild from the exact published atlas after save/reopen.
- New editable drafts and publications embed the exact semantic profile in the
  same RES as the MMP pages. They no longer require a separate editor JSON for
  distribution; legacy sidecars remain read-only compatibility inputs.
- Every modern draft/publication records a content-addressed exact recovery
  snapshot under the normal Endurance backup root. Identical page/profile
  payloads are stored once. If an MPR's matching RES is displaced, opening the
  exact MPR offers to restore a validated map-local RES without inference.
- The mode tab, layer tab, shared filter, and manual slot list are covered by
  the headless widget contract.
- Modern and Classic Auto now use the sibling's latest committed, answered
  material-family and corner assignments. Unanswered R148 batch 2/3 review
  data is deliberately excluded.

## Focused manual test

1. Open a supported map and verify the Landscape paint palette shows
   **Modern**, **Classic Auto**, and **Manual**. Enter part of a material name
   or slot number in the filter and confirm all three palettes filter.
2. In **Modern**, paint a material combination that requires a new exact blend.
   Switch to **Classic Auto** and verify its palette updates without reopening.
3. In **Classic Auto**, paint where every tuple exists and confirm the atlas
   slot count does not grow. Then try a missing tuple: the whole stroke must be
   refused with no partial terrain or atlas change.
4. In **Manual**, select a physical slot, paint one small correction, rotate it
   with Shift+Wheel, and verify undo/redo restores the exact packed slot and
   rotation.
5. Save an editable draft and reopen it. Confirm only the MPR and map-local RES
   are needed and all three modes retain the same designations.
6. Publish with **Save MPR** or **Save MPR As...**, reopen, and confirm the exact
   profile is read from the chosen RES. Temporarily move that RES aside, reopen
   the unchanged MPR, and accept the exact recovery prompt; the restored
   `<map>-atlas.res` must reopen with the same terrain and palettes.
7. On two or three familiar stock zones, compare the visible Modern materials
   with Classic Auto, paint several existing authored transitions, and confirm
   the current family names/corners and brush thumbnails are coherent.

## Known limitations

- Automatic semantic inference for unknown third-party atlas identities remains
  out of scope; Modern and Classic Auto still require a supported or embedded
  exact profile. Manual mode remains available for physical-slot edits.
- Recovery snapshots are deliberately exact and are not auto-pruned. Their
  large MMP payloads are content-deduplicated, but users may remove old backup
  history manually when no longer needed.
- Rotation-equivalent physical deduplication remains disabled by decision.
