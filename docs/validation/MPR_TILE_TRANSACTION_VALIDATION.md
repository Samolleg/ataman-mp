# MPR tile transaction validation

This increment establishes the non-UI terrain texture-paint transaction used by
later brush and live-preview work. Changed-MPR saving remains disabled.

## Editing contract

- Land tiles and existing overlay tiles are edited in the same canonical SEC
  payloads used by height sculpting, so combined edits compose before staging.
- A tile command records global coordinates, layer, original packed value, and
  replacement packed value for deterministic undo/redo and provenance checks.
- New selections must resolve inside the MPR's embedded atlas namespace and its
  read-only MP tile-category array.
- MP tile categories are never edited. They are atlas metadata, not a user
  painting control.
- Painting an inactive overlay cell fails closed. Overlay creation/removal needs
  its own structural command and material decision.
- Staging replaces only affected SEC members and preserves every other byte.

## Automatic transition contract

MPR tile categories alone do not encode edge and corner composition. Automatic
blending therefore consumes an explicit transition catalog whose entries carry
an oriented four-corner signature, packed atlas tile/rotation, and positive
distribution weight. Atlas analysis will populate this catalog in a later
increment.

When several entries match exactly, selection uses a stable hash of map-local
tile coordinates, signature, and a saved distribution seed. This provides the
approved natural distribution while keeping repaint, undo/redo, and tests fully
deterministic.

## Synthetic validation

The synthetic document suite verifies:

- packed atlas slot and rotation round trips;
- circular point painting on land and active overlays;
- refusal to paint inactive overlays or out-of-namespace atlas slots;
- undo/redo and stale/duplicate command rejection;
- exact two-byte SEC locality for each painted tile;
- fresh reopen and semantic diff locality;
- composition of height, land-tile, and overlay-tile edits in one SEC;
- transition-signature rotation and deterministic weighted distribution.

The full suite passes 41 tests with no failures or skips.

## Private copied-map validation

`mpr_tile_transaction_corpus` performs an in-memory one-quarter-turn land-tile
edit, stages the SEC replacement, freshly reopens it, restores the two edited
bytes for an exact payload comparison, and checks semantic diff locality. All
40 copied inputs passed (8 Base and 32 JMV). It never writes a candidate map or
touches the live installation.

Manual testing begins after the renderer preview and brush UI consume this
model. There is no meaningful manual action for this increment.
