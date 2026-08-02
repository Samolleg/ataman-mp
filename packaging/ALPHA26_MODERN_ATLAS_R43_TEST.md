# Modern terrain atlas R43 tester changelist

R43 removes the single-tile repetition in automatic material painting while
preserving exact corner identity, rotation reuse, undo/redo, and saved maps.
It also carries the final R75 semantic catalog and the current R42 grouped mask
questionnaire.

## Changed

- Every authored physical tile with the exact audited four-corner signature is
  eligible for painting. This includes all genuinely uniform variants and all
  authored blend variants at their valid quarter-turn rotations; palette
  thumbnails no longer determine paint authority.
- A new stroke receives one fresh seed. Stable hashing of that seed, atlas,
  layer, signature, and global cell coordinates distributes physical variants
  and rotations across the stroke without preview flicker or traversal-order
  dependence.
- Painting the same semantic material again deliberately rerolls its visual
  arrangement. Undo and redo replay captured packed values exactly instead of
  resolving again.
- MPR save/reopen and map-atlas finalization preserve the selected physical
  artwork and rotation exactly. Existing authored blends are reused, including
  rotated matches, before canonical generation is considered.
- Final material stripes and every-material radius-3 circles were regenerated
  from installed JMV assets with the final R75 material catalog. Established
  Zone 1, 2, 5_1, 11, 16, 17, and 18 comparisons are retained. No EI-HD asset
  is included.

## Included visual collections

- **Final material stripes:** 72 paintable materials, 654 active physical
  identities, zero unresolved cells.
- **Every-material radius-3 circles:** 72 paintable materials, 765 active
  physical identities, zero unresolved cells.
- All retained zone-set comparison collections resolve with zero unresolved
  cells.
- `questionnaire/blend-mask-r42-mask-questionnaire.html` remains the current
  grouped family/topology mask review, with concrete generated outcomes.

## Focused manual checks

1. Open the final stripes or circle collection and paint a broad area with a
   material that has many uniform tiles, such as Zone 14 `Ice / Common`.
   Confirm several physical motifs and quarter-turns appear instead of one
   repeated crack pattern.
2. Paint the same material over that area again. Confirm the arrangement
   rerolls while every cell remains the selected material.
3. Undo and redo the reroll. Confirm each action restores the exact previous or
   rerolled arrangement. Finalize/save to a new map pair, reopen it, and confirm
   the arrangement is unchanged.
4. Paint a corner blend whose authored orientation exists only after a
   quarter-turn. Confirm the authored art is rotated and reused rather than a
   new blend being generated, with no shifted corner ownership.
5. Open the R42 mask questionnaire in a browser and continue the grouped visual
   review. Artistic mask shape and texture quality remain pending human review.
6. Right-click a terrain tile and release. Confirm its information tooltip
   remains visible without holding the button and does not blink while held
   within one cell.

## Review note

Automated checks prove semantic invariance, deterministic per-stroke spatial
selection, exact undo/redo and serialized packed-grid persistence, valid runtime
types, and complete questionnaire image bindings. Artistic mask acceptance is
still a human-review gate; the R42 questionnaire is included for that purpose.
