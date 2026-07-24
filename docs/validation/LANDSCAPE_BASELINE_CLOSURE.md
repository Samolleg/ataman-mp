# Landscape Editing Baseline Closure

Status: closed on 2026-07-24 after the complete manual smoke matrix passed.

This baseline closes the first safely saveable landscape-editing increment. It
does not claim completion of every 0.3.x landscape feature.

## Included baseline

- canonical sector-vertex terrain ownership with seam conflict refusal;
- undoable raise, lower, flatten, and smooth sculpt strokes;
- heightmap import with explicit height bounds and orientation controls;
- automatic atlas-derived land and overlay material painting;
- bounded local blend selection that never introduces an unrelated material;
- manual tile/rotation painting for exact repair and unsupported atlas cases;
- land-family access in overlay painting where authored overlays require it;
- terrain-aligned sculpt/paint outlines and terrain tile-grid visualization;
- batched stroke undo/redo and optimized live preview updates;
- copied-map paint diagnostics and terrain/MOB height-risk reporting;
- lossless MPR archive ownership, localized SEC/tile transactions, semantic
  diff, fresh candidate reopen, backup, source-race refusal, and atomic commit;
- changed-MPR **Save As** to a separate validated test copy;
- unchanged-MPR no-op preservation;
- map catalog editing, raw span preservation, copied-RES staged saves, and
  standalone/in-editor catalog access.

## Intentional safety boundary

A dirty MPR cannot replace its currently open source path. The supported
baseline workflow writes a separate validated test copy, leaves the source
dirty and untouched, then requires reopening the copy before runtime testing.
This prevents the legacy rebuilding serializer or an unreviewed terrain edit
from replacing an authored map.

## Deferred beyond this baseline

- direct replacement/save-back of the currently open dirty MPR;
- AI-graph rebuild and visibility/navigation preview;
- atlas authoring or automatic RES blend-patch generation;
- a built-in atlas conversion/review wizard and atlas viewer;
- minimap capture, texture conversion, and RES insertion;
- liquid-material authoring and expanded liquid-surface tooling;
- ramp sculpt UI and additional modern-editor sculpt modes;
- automatic integration between catalog saves, map-list refresh, and loaded
  workspace rebinding (see `docs/MAP_CATALOG_EDITOR_INTEGRATION_DRAFT.md`).

## Automated closure evidence

- `endurance_document_tests`: 56 passed, 0 failed, 3 private-fixture skips;
- `map_table_document_tests`: 17 passed, 0 failed;
- `res_archive_document_tests`: 9 passed, 0 failed;
- MPR private-corpus validation runner: compiled successfully;
- release build through the linked-worktree/Unicode-safe wrapper: passed;
- wrapper identity regression: normal repository, Unicode linked worktree, and
  ASCII mapping passed;
- copied JMV `texts.res` catalog validation: passed without mutation.

The private fixture skips require copied local artifacts and must not be
replaced by committed proprietary maps.

## Final manual smoke matrix

Use copied MPR/MOB/RES files only.

1. Open a map pair and a collection in separate sessions. Confirm landscape
   tools follow reloads and the brush outline remains live.
2. Perform one continuous sculpt stroke, undo, redo, then switch tools and
   confirm camera and wheel modifiers retain their documented behavior.
3. Paint land automatically, paint an overlay, and place one manual tile.
   Undo/redo each stroke and confirm no unrelated family appears.
4. Save the dirty MPR to a separate test filename. Confirm semantic review,
   fresh reopen validation, backup behavior for an existing destination, and
   that the open source remains dirty and byte-unchanged.
5. Reopen the test MPR with its MOB family and run **Test Current Map**. Confirm
   edited heights/textures appear and normal traversal remains possible.
6. Decline an unsaved-MPR prompt during map switching. Confirm the current map
   and active landscape tool remain usable. Then accept/discard deliberately
   and confirm only one MPR prompt occurs.
7. In the catalog editor, save a copied default `texts.res` containing repeated
   edge exit IDs. Reopen it and confirm the routes, comments, filter, and window
   layout are preserved.
8. Exit normally with no editor-owned test process or unexpected save prompt
   remaining.

## Closure decision

All eight manual checks passed on 2026-07-24. The supplied copied catalog-save
artifact reopened successfully, remained unchanged during validation, and was
byte-identical to its sibling copied baseline:

- size: `1,317,804` bytes;
- SHA-256: `95FD33A0C43003E17622CCF899BEADDB4501EEABC0035285EAC9BEE03B048322`;
- derived-RES untouched-member validation: passed.

This scope is now the closed baseline landscape-editing foundation. Merge and
push remain separate explicit operations; deferred features must not be added
to the baseline merge as incidental fixes.
