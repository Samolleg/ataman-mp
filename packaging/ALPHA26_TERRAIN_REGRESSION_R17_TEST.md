# Terrain regression r17 tester changelist

This revision removes the connected-route Shift+R experiment that regressed
ordinary terrain repair, fixes incomplete square Modern stamps, and removes the
per-stamp atlas presentation transition implicated in one-frame flashes.

## Changed

- A Modern square Tile stamp now resolves every displayed core cell in the
  same undoable transaction, including cells whose semantic corners already
  matched the selected material but whose packed artwork was still stale.
- Shift+R again uses the earlier bounded missing-blend pass for both Ground and
  Overlay. It no longer scans connected route-ownership components, preserves
  normal candidates behind route-ambiguity warnings, or applies route-owner
  overrides.
- Same-layout atlas changes update the existing preview texture in place and
  retain its texture identity. They no longer allocate/copy/swap an offscreen
  atlas texture or force a re-entrant viewport repaint for every stamp.
- Failed in-place uploads restore the previous changed-slot pixels and mipmaps
  before the edit is rejected.

## Deliberately unchanged

- Lower-level route-owner metadata remains readable for compatibility, but
  Shift+R does not consume it.
- Exact runtime terrain-type selection, Modern atlas staging, undo/redo,
  publication, reload, and finalization behavior remain active.
- A stamp that expands the atlas to another physical page still uses the
  separate atomic regrid path; the stable in-place route applies after the
  working layout exists.

## Focused manual checks

1. In Zone1, stamp two 7x7 square Lava brushes into water. Every displayed
   square cell should be painted immediately; Shift+R must not be needed to
   fill missing cells.
2. Press Shift+R once. It should finish near the earlier bounded-resolve speed,
   must not emit `terrain.resolve_route_ownership_preserved`, and must not
   radically expand or erase either stamp. A second Shift+R should be a no-op.
3. Repeat Shift+R on a Ground area that previously resolved correctly. Ground
   repairs should work without multi-material route warnings.
4. With Object shadows enabled, make a first generated-transition stamp and a
   second stamp. Repeat with Object shadows disabled. Neither stamp should
   produce a one-frame flash.
5. In `workflow.log`, same-layout stamp uploads should report
   `route:in-place`, a stable texture ID, and `framebufferTouched:false`.

## Known manual boundary

The no-flash result must be confirmed on the affected GPU/compositor. A first
stamp that adds a new atlas page exercises the atomic regrid path rather than
the subsequent stable in-place route; report separately if only that first
page-expanding stamp flashes.
