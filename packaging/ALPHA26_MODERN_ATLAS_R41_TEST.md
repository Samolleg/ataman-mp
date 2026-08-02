# Modern terrain atlas R41 tester changelist

R41 updates every modern-terrain visual collection through the final R75
semantic review and replaces the repetitive per-material mask questionnaire
with one decision per shared mask selection class.

## Changed

- Blend-mask questions are grouped by target visual style, support scenario,
  corner assignment, and exact mask identity. Stock area/tile numbers are shown
  only as authored-source provenance.
- Each shared-mask question shows the mask and authored source once, followed by
  up to four requested-family, exact generated-outcome, and support-family
  examples.
- The 11 answers started in R39 migrate to three consistent shared decisions;
  no conflicting answer was inferred or discarded.
- The material catalog incorporates final semantic checkpoint R75. The former
  `Ground / Mountainous` corner is now `Ground / Trampled`; the empty family is
  removed. Reviewed `Stone / Grayish` transition-only corners remain unchanged.
- Final material stripes and every-material radius-3 circles were regenerated
  from installed JMV assets. Established zone-set comparisons are retained. No
  EI-HD asset is included.

## Included visual collections

- **Final material stripes:** 461 active physical identities, zero unresolved
  cells.
- **Every-material radius-3 circles:** 596 active physical identities, zero
  unresolved cells.
- Zone 1, 2, 5_1, 11, 16, 17, and 18 comparison collections are retained from
  the established reviewed set and resolve with zero unresolved cells.

## Focused manual checks

1. Open `questionnaire/blend-mask-r41-mask-questionnaire.html` in a browser.
   Confirm a question names the style and corner assignment, shows one shared
   mask/source pair, and shows several different material-pair outcomes.
2. Continue the three prefilled rejected decisions, then submit or export the
   review. Reload once and confirm answers persist.
3. Open `final-material-stripes.collection` and
   `all-material-circles.collection` from `collections`. Inspect material names,
   mixed edges, and representative generated blends.
4. Right-click a terrain tile and release. Confirm its information tooltip
   remains visible without holding the button and does not blink while held
   within one cell.

## Review note

Automated checks prove exact corner resolution, complete questionnaire image
bindings, persistence/export behavior, and deterministic packed identity.
Artistic mask shape and texture quality remain the purpose of human review.
