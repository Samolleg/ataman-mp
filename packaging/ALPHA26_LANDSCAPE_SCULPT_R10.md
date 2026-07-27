# EI Mapper Endurance 0.3.0-alpha.26 — landscape sculpt R10

## Changelist

- Local raise/lower sculpting now saturates individual height samples at the
  current MPR range instead of refusing the entire brush stamp when one sample
  reaches the range boundary.
- Overlay sculpting follows the connected overlay sheet under the brush and
  preserves unrelated sheets that happen to share a SEC lattice coordinate.
- Overlay sculpt preview no longer copies a complete SEC payload for every
  changed vertex, removing a major source of avoidable brush latency.
- Terrain sculpt outlines are gold; overlay sculpt outlines are cyan.
- The approved minimap capture, comparison, export, catalog-registration, and
  Test Current Map staging workflow is integrated into this landscape build.

## Manual checks

1. On `zone1test10.mpr`, lower terrain near approximately
   `(6.86, 120.48, 0.48)`. The brush should continue smoothly until local
   samples reach the floor without a whole-stroke MaxZ refusal or an open seam.
2. Sculpt an existing overlay where sheets meet. Only the sheet under the
   cursor should move; adjacent/disconnected sheets must remain intact.
3. Compare land and overlay sculpt responsiveness and confirm the outline
   changes from gold to cyan when switching targets.
4. Add a new overlay surface with a small and then a larger brush. Record any
   remaining pause or incomplete first stamp with the workflow log; Add Surface
   still has a renderer upload path that needs real viewport timing evidence.
5. Smoke-test minimap generation, comparison, export, and Test Current Map
   staging from the current map.

## Scope note

This build does not enable atlas patch generation or provide reviewed semantic
atlas profiles for every game zone. Zone 1 remains the established semantic
painting baseline.
