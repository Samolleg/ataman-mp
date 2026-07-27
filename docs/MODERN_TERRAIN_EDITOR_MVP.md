# Modern terrain editor MVP

This MVP separates terrain authoring from the finite physical atlas. The
authoritative edit state is a material identity on every terrain vertex. A
brush card represents exactly one identity; support/context variants are not
paint tools.

## Stroke contract

- Dragging accumulates mandatory touched vertices and previews the selected
  material as an unblended authored uniform tile.
- Releasing the pointer resolves affected cells against exact authored
  four-corner signatures. A missing signature never removes or changes a
  requested vertex; it remains virtual until finalization generates it.
- One drag is one undo transaction, including both the virtual vertex changes
  and the final physical preview references.

## Exact generation and packing

Finalization scans the complete vertex field and reuses exact authored packed
tiles first. Missing 1/3, 2/2, three-family, and four-family signatures are
generated deterministically from uniform material sources. Grid/preview Y
orientation is explicit. Quarter-turn-equivalent signatures and identical
pixels are deduplicated, and page mipmaps use premultiplied downsampling.
DXT1 binary alpha is preserved; graded alpha is refused rather than flattened.

Only map-wide used slots plus slots referenced by the preserved opposite layer
survive dense repacking. Every SEC reference is remapped. The MVP refuses a
result above 512 slots and never substitutes a different material. The packed
format can address more, but enabling 9- or 16-page output requires controlled
shipped-runtime and Endurance-renderer validation first.

## Portable isolation

Finalization creates a deterministic map-specific namespace, a renamed and
freshly reopened MPR, external MMP pages in a freshly reopened RES archive, and
an audit manifest. It never overwrites the loaded MPR or a shared atlas such as
Zone2/Gz2G. Existing output folders are also never silently replaced.

Current MVP limitation: land and overlay virtual edits must be finalized in
separate sessions. Inactive overlay regions without a complete recoverable
vertex baseline are refused.

## Reference evidence

Read-only static inspection of the installed `EvilIslandsMapEditor.exe` shows a
similar high-level separation among a material field/catalog, tile recipes,
composition, and atlas baking. Its installed HD tileset descriptions also
confirm layered base/mask recipes and explicit rotations. Those observations
support the architectural split only; no private or third-party implementation
is copied, and the installed editor is not presented as an Endurance identity
source.
