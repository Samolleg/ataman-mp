# Modern terrain atlas R100 tester changelist

This build replaces the rejected mathematical three-/four-material junction
masks with the same reviewed authored mask families already approved for
ordinary two-material blends. It retains the authoritative R98 semantic
catalog while the separate R100 semantic questionnaire remains under review.

## Changed

- Three- and four-material junctions no longer bypass the R44-R77 reviewed
  mask registry. Each material component now uses its own approved authored
  Grass, Soil, Stone, Gravel, Paving, Sand, Snow, Ice, or Water ownership
  shape for the exact requested corner assignment.
- Mask orientation is derived only from the requested NW/NE/SE/SW ownership.
  The junction path does not use an orientation score, material-ID ordering,
  or independent arbitrary rotations.
- The generated compositor retains a universal one-pixel endpoint profile so
  neighboring tiles share byte-identical edges. The connection regression
  exercises vertical and horizontal joins, three-material adjacent/diagonal
  junctions, four-material junctions, every relevant approved family, and all
  quarter-turns, following the seam-validation discipline described by the
  public EI-HD `create_mask.md` workflow.
- The rejected R99 evidence set was regenerated from the fixed runtime: all 49
  component applications across the same 16 reported junction cells use one
  of 14 approved authored mask groups; mathematical fallbacks are zero.
- Genuine authored uniform variants and exact authored blend rotations retain
  deterministic per-stroke selection. Undo/redo and save/reopen preserve the
  captured physical slot and packed rotation exactly.
- The embedded R98 catalog SHA-256 is
  `7C74A820A82AA8599152B627F605C75154765AD99700453ECB0438FD7CE6ED6F`.
  Installed stock Jamais Vu assets are included only in isolated test maps;
  EI-HD research assets are not included.

## Included visual collections

- **Final material stripes:** 75 materials, 601 active physical slots, 7,680
  packed cells, zero unresolved cells.
- **Every-material radius-3 circles:** 75 materials, 729 active physical slots,
  7,680 packed cells, zero unresolved cells.
- **Zone comparisons:** Zone 1, 2, 5_1, 11, 16, 17, and 18; every map resolves
  with zero unresolved cells.

Every manifest records physical source identity/provenance, runtime terrain
type, canonical four-corner signature, pixel SHA-256, and the complete packed
cell grid. A finalized physical slot number alone is not source provenance.

## Focused manual checks

1. Open `collections/final-material-stripes.collection` and inspect every
   three-/four-material meeting. None should use the former identical
   mathematical wedge/diamond shapes; each component should resemble its
   approved authored family.
2. Open `collections/all-material-circles.collection` and inspect junctions at
   overlapping circle boundaries. Confirm exact corner ownership, natural
   authored silhouettes, and continuous tile-to-tile connections.
3. Spot-check Grass, Paving, Snow, Soil, Stone, and Water junctions corresponding
   to the rejected R99 set. There must be no arbitrary quarter-turns or missing
   blend replaced by a uniform tile.
4. Open the seven `zone-set-*.collection` files and compare family usage with
   the stripe/circle maps. Record wrong-family artwork separately from mask
   defects so it can be reconciled by exact source provenance.
5. Paint and repaint a broad uniform area. Physical variants and rotations
   should vary spatially; undo/redo and save/reopen must keep the exact result.

## Review boundary

The R99 mathematical junction masks are blanket-rejected and are not retained
as selectable alternatives. Automated checks prove routing, rotations,
connections, semantic corner resolution, and package integrity; aesthetic and
family classification review remains a human visual task.
