# Modern terrain editor MVP

This MVP separates terrain authoring from the finite physical atlas. The
authoritative edit state is a material identity on every terrain vertex. A
brush card represents exactly one identity; support/context variants are not
paint tools.

## Stroke contract

- Dragging accumulates mandatory touched vertices and previews the selected
  material as an unblended authored uniform tile.
- Releasing the pointer resolves affected cells against exact authored
  four-corner signatures. Missing signatures are generated immediately,
  installed in the bounded live working atlas, and cached by canonical
  rotation. A missing signature never removes or changes a requested vertex.
- One drag is one undo transaction, including both the virtual vertex changes
  and the final physical preview references.

## Exact generation and packing

Stroke release and finalization both reuse exact authored packed tiles first.
Missing 1/3 and 2/2 signatures are generated deterministically by transferring
a full-resolution ownership mask reconstructed from a reviewed stock JMV
transition onto the requested uniform material sources. Ranking is exact
material pair first, then the sibling-reviewed material-school pair and exact
topology. Three- and four-family junctions compose reviewed stock one-corner
silhouettes. There is no procedural-noise fallback: an absent reviewed mask is
reported instead of silently inventing artwork. Every generated tile records
the stock namespace/slot provenance used for visual review.

Finalization repacks the live cache from the authoritative field. Grid/preview Y
orientation is explicit. Quarter-turn-equivalent signatures and identical
pixels are deduplicated, and page mipmaps use premultiplied downsampling.
DXT1 binary alpha is preserved; graded alpha is refused rather than flattened.

Every physical atlas slot also carries the signed MP terrain-class entry used
by ambience and AI traversal generation. Editor material IDs remain separate
from these runtime IDs (0..15). Generated transitions use equal corner
coverage: a majority runtime class wins, while an exact tie uses the class of
the material being painted. That decision is stored with the generated
identity and is independent of rotation. Pixel deduplication therefore keys on
both encoded pixels and runtime class; visually identical slots remain
separate when their gameplay class differs. Unknown classes refuse generation
instead of silently becoming Grass, and the live working package carries the
same class vector that finalization writes.

Grouped visual variations are admitted to one paint material only when their
reviewed MP runtime class agrees. A conflicting source variation is excluded
with a validation warning, preventing randomized strokes from silently
changing ambience or traversal behavior.

Only map-wide used slots plus slots referenced by the preserved opposite layer
survive dense repacking. Every SEC reference is remapped. Painting remains
independent of physical capacity. Finalization currently accepts up to 1,024
slots (16 pages), the ceiling proven by controlled shipped-runtime tests, and
never substitutes a different material. The packed format can address more,
but a higher finalization ceiling requires a new controlled runtime gate.

## Portable isolation

Finalization creates a deterministic map-specific namespace, a renamed and
freshly reopened MPR, external MMP pages in a freshly reopened RES archive, and
an audit manifest. It never overwrites the loaded MPR or a shared atlas such as
Zone2/Gz2G. Existing output folders are also never silently replaced.

Current MVP limitation: land and overlay virtual edits must be finalized in
separate sessions. Inactive overlay regions without a complete recoverable
vertex baseline are refused.

## Automated acceptance matrix

The headless matrix covers every ordered pair of four independent uniform
materials with compact-square, horizontal, vertical, and both diagonal paint
shapes, plus three- and four-family junctions. Its oracle compares every saved
cell's exact four semantic corners with the authoritative intended vertex
field, checks shared-edge continuity and mandatory touched vertices, repeats
finalization for stable hashes, and loads every generated MPR again. Dedicated
runtime-class regressions cover majority resolution, painted-material ties,
rotation independence, typed pixel deduplication, unknown-class refusal, and
working-preview propagation. Terrain strokes also schedule regeneration of an
already-generated AI graph preview; a loaded authored graph remains a static
user-selected override.

The distributable visual workload is a real-JMV Zone2 stripe collection. It
stamps every reviewed uniform family, forces missing multi-family junctions,
writes the exact generated MPR/RES pair, and emits a native-resolution contact
sheet plus per-tile stock provenance. Synthetic unit fixtures remain structural
tests only and are not presented as human visual evidence.

## Reference evidence

Read-only static inspection of the installed `EvilIslandsMapEditor.exe` shows a
similar high-level separation among a material field/catalog, tile recipes,
composition, and atlas baking. Its installed HD tileset descriptions also
confirm layered base/mask recipes and explicit rotations. Those observations
support the architectural split only; no private or third-party implementation
is copied, and the installed editor is not presented as an Endurance identity
source.
