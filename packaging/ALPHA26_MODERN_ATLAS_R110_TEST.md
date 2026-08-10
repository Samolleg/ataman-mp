# Modern terrain atlas R110 tester changelist

This build refreshes every shipped terrain test collection from the safe R110
semantic checkpoint and makes exact authored-artwork compaction preserve the
full physical terrain identity.

## Changed

- Exact authored tiles are compacted only when decoded RGBA pixels, reviewed
  corner signature, and runtime terrain type all agree. Source-slot aliases are
  retained in diagnostics and portable manifests.
- Identical artwork with distinct authored runtime types remains distinct. This
  preserves the verified Zone16 slots 3/254 Stone/Ground aliases.
- Pinned inactive-overlay tiles are decoded through the full cross-layer
  catalog before duplicate review; active paint resolution remains restricted
  to the active layer.
- Same-pixel, same-type tiles with conflicting corner signatures still fail
  closed. That gate exposed and corrected Zone18 slots 244/248/249; all three
  visibly mixed aliases now use Ground / Red at NW and Ground / Desert algae
  at NE/SE/SW.
- The reviewed semantic updates through the safe R110 checkpoint are included,
  including the R105 reported authored blends, R106 family pruning, R107/R108
  ground corrections, and the explicit R109 Bz7g slot143 correction.
- Preset SHA-256:
  `EF9A8A92F1B9A6DA6B0E1C3B7CCA1DE90C1039F99056DDF88336C11ACDF80960`.

Quarter-turn-equivalent artwork compaction remains disabled by explicit user
decision. Only byte-exact decoded RGBA duplicates are eligible.

## Included visual collections

- Final material stripes: 81 materials, 617 active slots, 387 generated exact
  tiles, 230 authored tiles reused, 7,680 packed cells, zero unresolved.
- Every-material radius-3 circles: 81 materials, 754 active slots, 549 generated
  exact tiles, 205 authored tiles reused, 7,680 packed cells, zero unresolved.
- Zone comparisons: Zone 1, 2, 5_1, 11, 16, 17, and 18; every map has zero
  unresolved cells and passed packed-grid, physical-identity, pixel-hash, and
  runtime-type integrity checks.

The separately generated 81-material full-family stamp and 1,024-through-16,384
slot runtime probes passed automated validation but are not shipped in this
tester folder to avoid an unnecessary 500+ MiB of additional test data.

## Focused manual checks

1. Open `collections/final-material-stripes.collection`. Inspect every stripe
   edge and the junction row for an authored or generated blend; a uniform tile
   must never substitute for a non-uniform requested signature.
2. Open `collections/all-material-circles.collection`. Inspect circle interiors
   for spatially varied authored uniform slots/rotations and boundaries for
   corner-correct blends.
3. Repaint an area with the same material. A new stroke may reroll valid uniform
   artwork, but undo/redo and save/reopen must reproduce the exact packed tiles.
4. Right-click uniform and transition areas repeatedly. The editor must not
   crash; the persistent tile information tooltip must not flicker.
5. Inspect the seven `collections/zone-set-zone*.collection` comparisons,
   especially Zone16 and Zone18, for unchanged gameplay terrain behavior and
   visually correct authored aliases.
6. If a wrong material appears, mark the exact cells and send the exported
   paint-issue JSON. Finalized physical slot numbers alone are not source
   provenance.

## Review boundary

No open questionnaire defaults were inferred. Installed stock Jamais Vu assets
are included only in these isolated visual-test maps. EI-HD material was used
for research only and is not included.
