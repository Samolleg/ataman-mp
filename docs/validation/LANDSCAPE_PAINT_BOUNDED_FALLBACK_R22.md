# Bounded semantic-paint fallback validation R22

## Scope

R22 replaces the route-expanding junction solver with a predictable fixed-atlas
contract. Changed-MPR saving remains disabled.

## Painting contract

1. Every tile covered by the brush core is painted with the selected uniform
   authored material tile.
2. Only the immediate one-cell ring is considered for automatic blending.
   An exact authored transition is preferred. If it is absent, Endurance may
   use a constrained nearest transition that keeps every corner touching the
   painted core, introduces no terrain family absent from that boundary tile,
   and differs from the requested transition by at most one corner.
3. If neither transition exists, the boundary tile is left unchanged. The
   result is a visible hard edge rather than spilled paint, an unrelated
   material, or a refused core stroke.

Repeating the same stroke is a no-op. The painter never expands beyond the
brush core plus its immediate ring.

## Manual complement

`Manual atlas tile mode (no blending)` paints the selected authored atlas tile
and rotation directly through the canonical undoable tile transaction. The
selected slot must belong to the active land or overlay catalog, preventing an
overlay tile from being applied to land or vice versa. The mouse wheel changes
rotation only while manual mode is active.

This mode is intended for precise correction of a hard edge or for authored
arrangements that cannot be inferred from a fixed atlas.

## Automated evidence

- Core coverage with an atlas containing only uniform tiles: five radius-one
  circle cells changed; no boundary spill; warning instead of refusal.
- Constrained-nearest fixture: only the center and four cardinal boundary
  cells changed; only the two families already present were emitted.
- Repeating either applied stroke produces no command and no undo entry.
- Existing manual tile transaction checks continue to cover locality,
  namespace encoding, undo, and redo.
- Full document suite: 47 passed, 0 failed, 2 expected private-fixture skips.
- Qt 5.12.12 / MinGW 7.3 release application build passed.

## Manual check

On a copied map, verify automatic painting for an authored two-family blend and
an unsupported multi-family junction. The former should blend within one ring;
the latter must cover the brush core and may show a hard edge, but must not
change unrelated nearby materials. Then enable manual mode, choose the desired
atlas tile, rotate it with the wheel, paint the correction, and verify one-step
undo/redo for each held-mouse stroke.

Changed-MPR Save and Save As remain disabled.
