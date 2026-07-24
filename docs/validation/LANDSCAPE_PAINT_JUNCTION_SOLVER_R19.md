# Semantic-paint junction solver validation R19

## Scope

R19 replaces the R18 all-or-nothing multi-family boundary fallback with a
bounded local corner-constraint solver. Changed-MPR saving remains disabled.

## User evidence

- Diagnostic: `zone1-paint-diagnostic.json`
- SHA-256: `018c3c469a3b76618a094811155a38ba9055d15d46c244d105596c0ccc3bcd77`
- Source MPR SHA-256:
  `a1ed4ac4f39e4da49bb29b3c80366db747be9f3a318726eaa2db9d1c1b150aae`
- The source hash exactly matches the copied private Zone1 fixture.

The version-3 current snapshot contained 17 shared-edge disagreements across
the reported arrangements:

- Grass/irregular with Ground/common and Grass/dark, surrounded by
  Grass/common;
- Stone/lime with Grass/dark and Grass/irregular;
- Grass/dark between and slightly below two Ground/common circles, surrounded
  by Grass/common.

## Atlas constraint

Zone1 authors many pair and three-family tiles, but it does not author every
four-family corner signature. A correct painter must arrange compatible atlas
tiles across a small neighborhood, often using Grass/common as an intermediary,
rather than requesting an impossible four-family tile or dropping all external
boundary constraints.

## Correction

- Painted core corners remain fixed to the selected material.
- Corners shared with the untouched outer halo remain fixed to their current
  terrain families.
- Remaining local corners are solved together from the namespace's actual
  terrain-grid signatures.
- Candidate layouts minimize changes from the preferred shortest material
  route and use deterministic signature ordering.
- Genuine multi-family junctions receive one additional transition ring. The
  size-one painted core is unchanged; the transition halo may extend up to
  three cells when intermediary atlas families are required.
- The search is bounded at 100,000 nodes and normally bypassed entirely when
  the preferred patch is already authored.
- A failed solve on a coherent boundary is refused instead of emitting a
  partially broken junction. Existing contradictory authored boundary corners
  retain the established pairwise compatibility behavior.

## Automated evidence

- The copied private Zone1 regression recreates all three reported material
  arrangements at the captured coordinates and verifies both shared corners
  on every east and south edge of every changed cell.
- Existing pairwise, adjacent-stroke, R18 multi-family, atlas-orientation,
  undo/redo, locality, namespace, and failed-save regressions remain enabled.
- Full document suite: 48 passed, 0 failed, 1 unrelated optional fixture skip.
- Qt 5.12.12 / MinGW 7.3 release build passed.
- Offscreen widget and secondary-window lifecycle checks passed.

## Remaining manual check

Recreate the three reported arrangements on a fresh copied Zone1 map. Inspect
the larger transition neighborhood, not only the painted center. Every shared
edge must remain continuous, and undo/redo must restore the identical layout.

Changed-MPR Save and Save As remain disabled.
