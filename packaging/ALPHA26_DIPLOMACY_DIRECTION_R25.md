# Alpha.26 diplomacy direction r25

## Changed

- The MOB diplomacy window is resizable, uses scrollbars when needed, attempts
  to fit the complete matrix on first open, and restores the user's last size.
- Diplomacy cells remain compact numeric `0` / `1` / `2`; hovering a cell
  explains Ally, Neutral, or War.
- `Apply changes in both directions` makes reciprocal editing explicit. Turn it
  off to edit only the selected row-to-column relationship.
- Self-relations can be edited. Directional and non-Ally self-relations are
  preserved; family validation warns so unusual values can be confirmed rather
  than silently normalizing them.
- One-way diplomacy Undo now restores only the relationship that was changed.

## Manual checks

1. Open a MOB with diplomacy, open **MOB Parameters > Diplomacy**, resize the
   window, close it, and reopen it. Confirm the chosen size returns.
2. Hover values `0`, `1`, and `2`; confirm the tooltip explains Ally, Neutral,
   and War while the cells themselves remain number-only.
3. Clear **Apply changes in both directions**, change A → B, and confirm B → A
   does not change. Undo and redo; confirm the reverse cell stays untouched.
4. Double-click a diagonal cell, then Undo. Confirm the self-relation changes
   and restores.
5. Validate a MOB containing asymmetric or non-Ally self-relations. Confirm the
   warning describes the relationship and no value is changed or blocked.

## Known limitation

- Non-Ally self-relations are supported by the runtime-facing data model but
  were not observed in the sampled installed MOB corpus; Endurance therefore
  warns whenever one is present.
