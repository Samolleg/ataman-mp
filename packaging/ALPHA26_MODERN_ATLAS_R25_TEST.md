# Modern atlas r25 tester changelist

This revision fixes semantic paint strokes being refused on unmodified Jamais
Vu maps when the first requested missing tuple had no previously imported stock
mask.

## Changed

- Every opened map now indexes its own authored two-family transition
  silhouettes before the first stroke.
- If the map lacks enough examples of the required 1/3, adjacent 2/2, or
  diagonal 2/2 topology, the editor lazily loads up to three compatible stock
  JMV masks from the global transition library.
- Material-school matches remain preferred, but a style mismatch can no longer
  make a mandatory painted vertex fail. No procedural-noise fallback or
  identity substitution was restored.
- Exact current-map and exact global JMV tiles still take precedence over
  generated tiles.

## Focused manual checks

1. Open an unmodified default Zone1 map and paint several uniform materials
   across unrelated existing terrain. The first stroke must apply immediately.
2. Repeat on default Zone2, including Grass/Common across ice-, sand-, stone-,
   and soil-like terrain. No stroke may report `stock_blend_mask_missing`.
3. Confirm every touched vertex retains the selected material and that the
   generated-tile counter increases only for genuinely absent exact tuples.
4. Repaint the same boundaries and confirm the generated count stabilizes as
   cached exact tiles are reused.

## Known limitation

Mask selection and multi-family artistic quality still require visual review.
This revision addresses refusal and first-stroke availability; it does not claim
that every selected stock silhouette is artistically final.
