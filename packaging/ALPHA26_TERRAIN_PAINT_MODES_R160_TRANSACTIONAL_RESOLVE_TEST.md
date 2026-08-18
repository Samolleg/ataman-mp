# EI Mapper Endurance 0.3.0-alpha.26 terrain paint modes R160 transactional resolve test

Focused root-cause follow-up for terrain painting, preview, Resolve Missing
Blends, Manual ownership, and paint diagnostics.

## Changes

- Modern and Classic strokes now use one transactional preview command. A
  cancelled or interrupted stroke is rolled back instead of leaking into the
  next click or creating a partially undoable action.
- Classic Auto preview, release, undo, and redo now share the same exact
  semantic vertex field. Duplicate view-side vertex rewrites were removed.
- Resolve Missing Blends now computes one fixed-point repair plan against a
  staged atlas clone, preserves contextual stamps and runtime terrain types,
  and commits the atlas plus the complete tile command atomically. Failure and
  no-op paths leave the live atlas and MPR untouched.
- Manual tiles carry explicit reversible ownership. Modern/Classic painting or
  Resolve may replace them only when the affected cell is deliberately edited;
  unrelated finalization and save/reopen preserve the exact packed tile.
- Brush outline, live preview, and first committed stamp use the same footprint
  snapshot. A stale cursor/outline match refreshes and defers the stamp instead
  of painting a different centre tile.
- Preview geometry is staged on the CPU and uploaded only while the GL context
  is current. Deferred landscape UI updates are generation-guarded during
  shutdown.
- Paint diagnostic schema 6 records authoritative physical identity and pixel
  hashes for both full exports and issue marks. Re-exporting to the same path is
  an atomic replacement.
- RES name parsing rejects truncated or overflowing name tables.

## Validation

- `terrain_virtual_atlas_tests`: 110 passed, 0 failed.
- `endurance_document_tests`: 112 passed, 0 failed, 6 expected fixture/export skips.
- `res_archive_document_tests`: 16 passed, 0 failed.
- Retained Qt 5.12.12 MinGW 7.3 x64 release wrapper: pass.
- `--ui-widget-check`: explicit PASS with the Qt offscreen platform.
- Package finalizer/distribution validator: required before handoff.
- `git diff --check`: pass.

## Focused manual checks

- Open the Zone 2 repro and run **Resolve missing blends** once. A second run
  should be a no-op, and existing contextual stamps must not move or disappear.
- Compare Modern and Classic Auto vertex previews with the released result.
  Cancel a held stroke with a modal/screenshot action; no ghost paint may appear
  on the next click, and undo/redo must remain one exact action.
- Move slowly across a tile boundary and click. The committed centre must match
  the visible outline. Changing brush size must update the outline immediately.
- Place a Manual tile, make an unrelated Modern edit, save/reopen/finalize, and
  confirm that the exact Manual slot and rotation are retained.
- Export the paint diagnostic twice to the same filename after changing the
  marked cells. The second file must have schema 6, a new modified time, and
  current physical identity/pixel hashes.
