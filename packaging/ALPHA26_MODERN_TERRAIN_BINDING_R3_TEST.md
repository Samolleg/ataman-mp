# Alpha 26 — Modern terrain binding R3 test

This build fixes the critical post-merge Modern paint / Resolve Missing Blends
regression.

## Changed

- Global authored blend artwork is loaded only from a complete terrain atlas
  whose decoded SHA-256 matches an approved semantic preset binding. A
  same-named MMP page from another or modified RES can no longer be assigned
  approved material classification or provenance.
- Uniform material anchors used by brush thumbnails and canonical blend
  generation now use the same exact-atlas binding. A modified same-named page
  can no longer supply misclassified base artwork to Modern painting.
- Palette construction immediately reuses already-loaded current-map artwork
  and defers every foreign atlas verification until that material is actually
  needed. This restores the fast map-visible path and avoids retaining decoded
  full atlases in memory.
- Lazy sources are additive only: foreign artwork cannot replace an
  authoritative current-map uniform with the same logical material id.
- Resolve Missing Blends stages uniform pixels, their provenance, and their
  caches inside its isolated transaction. Refused and no-op repairs publish
  none of that staged state.
- Publication now refuses an unrealized material tuple and asks for Resolve
  Missing Blends instead of entering an under-sourced generation path.
- Exact global atlases are cached with their archive path, size, modification
  time, namespace, shape, and decoded atlas identity. A changed resource fails
  closed and falls back to canonical generation instead of reusing stale or
  mismatched pixels.
- Imported or generated rotations no longer displace a current-map authored
  exact variant of the same signature and runtime terrain type. Matching-type
  imports remain available when the current map has only a different runtime
  type, and multiple imported physical variants remain available when no
  suitable current-map variant exists.

## Focused manual check

1. Open the same Zone1 map used for the pre/post-merge diagnostics.
2. Paint two or more Modern strokes, including the material pair that
   previously introduced BaseGipat/Zone1 global substitutions.
3. Run **Resolve Missing Blends**.
4. Export a paint diagnostic. Confirm unrelated material classifications do
   not appear and existing current-map exact tiles remain
   `authored-current-map` when they already satisfy the signature/runtime type.
5. Undo/redo the paint and Resolve operations, save, reopen, and visually
   confirm the same terrain identities and artwork remain.

## Expected behavior

An installed atlas revision that is not one of the reviewed preset bindings is
deliberately not used as global authored artwork. If no approved configured
archive supplies an exact transition, Modern paint may generate the canonical
blend instead.

Foreign material cards that have not been used yet show a two-letter fallback
instead of synchronously loading every game-wide atlas thumbnail. The first use
of a material from a new foreign atlas may pause once while the editor verifies
the complete exact atlas binding; subsequent uses reuse that verified binding.
