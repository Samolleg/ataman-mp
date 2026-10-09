# Material atlas — Ataman 128

Synchronized 2026-10-10, EI-PAINT-DOCS-SYNC-20261010. Current behavior and
source anchors are recorded in
[the synchronization report](validation/ATAMAN_PAINT_DOCS_SYNC_20261010.md).
The questionnaire sections retain the historical research/presentation contract;
they are not a claim that a questionnaire was regenerated or tested today.

Ataman reads existing atlas images and builds a material catalog for Ground,
Underwater and Overlay usage and exact tile-corner identities. Modern painting
can also import authored stock tiles and generate missing blends; the former
statement that the editor never generates imagery is obsolete.

Zone 1 is the authored behavioral baseline. Every reviewed installed namespace
has an exact-bound built-in default with the same names, assignments, layers,
and thumbnails used by the paint palette. Other namespaces use the same parser
and catalog; they do not require a per-zone painting algorithm. Legacy authored
slot tables are not runtime fallbacks.

Built-in defaults use two separate semantic passes. The exact-corner pass keeps
every component required to reconstruct every atlas slot, including rare
components that occur only inside transitions. The editor-material pass exposes
a component in the paint palette only when the same atlas contains a uniform
four-corner tile or a rotatable 1-on-3 vertex stamp for it. A component that
fails that second test is labeled **transition support** in Material Atlas. It
is not an unexplained internal brush: it remains visible solely because exact
corner editing and slot reconstruction still depend on it.

## Painting

Landscape Tools provides **Modern (Z)**, **Classic / Classic Auto (X)** and
**Manual (C)** painting, with selectable **Ground** and **Overlay** layers.
Underwater materials appear in the Ground palette. Allowed-layer flags can
permit one family in both layers; incompatible corner combinations are refused.

Modern reuses exact authored tiles and creates missing exact blends from uniform
sources. Automatic transitions are enabled by default; disabling them leaves
resolution to the explicit Resolve Missing Blends action. Publication refuses
unresolved changed tuples. Modern drag preview is an unblended fill over the
accumulated vertex footprint; release resolves it as one undo transaction.

Classic never expands the atlas. **Safe** is enabled by default: unsupported
vertices/boundaries remain unchanged. With Safe disabled (less restrictive
behavior sometimes called **Smart**), unresolved boundaries may use a local
uniform hard edge. The current enum/UI does not expose Smart as a fourth mode.
Manual writes the selected physical tile and rotation directly.

Each material is a pressed thumbnail button. Uniform families use a valid
uniform thumbnail; supported 1-on-3 contextual cards use a vertex-centred motif.
Components without paint support remain **transition support** for exact corner
review. Hovering exposes the card name and assigned-slot count. Coverage counts
assigned physical slots once even when transitions belong to several families.

The historical description of representable subsets/hard edges applies to
authored-atlas Classic behavior, not to Modern generation in general. Modern
resolves exact signatures and reports unavailable sources/masks rather than
silently substituting another requested identity. Generation has reviewed-mask
and explicit canonical mathematical paths; it is not limited to the original
MVP stock-mask transfer algorithm.

Ground + Overlay can be finalized together. Dense packing retains live references
and animation phases. The current editor limit is **16,384 slots** (256 pages),
with a warning above **4,096**. Corrected historical runtime evidence passed
through 4,096, failed at 8,192 and did not attempt 16,384 in the ordered rerun:
[capacity report](validation/EXPANDED_TERRAIN_ATLAS_RUNTIME.md).
Export capacity and runtime proof are different boundaries.

R151 removed rotated-equivalent authored-slot merging: packing retains authored
image orientation and requires identical images/semantics/classes/route owner for
a merge. Generated signatures still use quarter-turn canonicalization, and
typed pixel caching still exists. It is inaccurate to describe all current
deduplication as disabled.

## Visual editing

Every corner control uses the tile's physical two-by-two layout:

```text
NW  NE
SW  SE
```

The serialized atlas signature remains `NW, NE, SE, SW`; display order and
serialization order are intentionally different. A questionnaire must never
place SE at lower-left or SW at lower-right. A questioned tile offers both
**This tile is uniform** and independent four-corner assignment on the same
card, with no separate Reviewed checkbox.

The Material Atlas window has three material groups on the left and an
unassigned tile pool on the right. Expand a material to see every assigned tile.
A transition tile appears under each material referenced by its corners, but it
counts only once in total coverage.

- Drag an unassigned tile onto a material to assign all four corners uniformly.
- Drag an assigned tile from one material to another to replace that material
  wherever it appears on the tile.
- Drag an assigned tile to the unassigned pool to exclude it from painting.
- Select a tile, then drag a material row onto its **NW**, **NE**, **SE**, or
  **SW** corner to build a two-, three-, or four-material transition.
- Click a corner to choose a same-layer material from a list when drag-and-drop
  is inconvenient.
- Rename, create, classify, or delete unreferenced materials from the material
  side. Palette names update from this single profile identity.
- Select a uniform member tile and choose **Use selected tile as thumbnail** to
  override the deterministic default.

Review previews and current schema-10 profiles use one literal visual contract:
NW is the material visible at the upper-left of the displayed tile, and likewise for
NE, SE, and SW. The paint compiler reflects that signature once into terrain
grid space and applies the renderer's one fixed packed-rotation convention.
Atlas content cannot redefine the meaning of the MPR rotation bits.
Selecting a tile never recolours its texture. The thumbnail member is marked with a bold
outlined **T** in the expanded material list; selecting only a material shows
that thumbnail without selecting or editing a tile.

All four corners must share an allowed Ground or Overlay section. A family
can permit both; Ground-only and Overlay-only families cannot be combined in
one tile. Incompatible assignments are refused without changing the previous
assignment. Unassigned inferred tiles can first be given a compatible base
assignment and then refined corner by corner.

There are no family and slot “Needs review” checkboxes. Slot ambiguity is
resolved by assigning or excluding the tile. If the parser could not prove a
material's identity or classification, that material shows one explicit
**Confirm material** action with the reason. Tile moves and corner edits update
all dependent memberships automatically.

## Research questionnaires

Every semantic-atlas review questionnaire is delivered as a standalone local
HTML document that opens in an ordinary browser. Inline conversation views are
optional convenience copies and are never the only review surface. The browser
document saves answers locally as they are entered and always exposes the full
plain-text response for manual selection and pasting. Copy, download, and direct
submit controls are additional conveniences; none may be required to recover
the answers. Every suggested family is accompanied by a titled full-tile
uniform reference from the relevant atlas whenever such an authored anchor
exists. A tile whose identity is under review is excluded from every reference
set in that questionnaire, including references shown for other questions.
Questions visually default to **Keep current** and do not expose a separate
**Not answered** state, but an untouched default is omitted from the exported
answer set. A changed selection or a comment is the explicit review action;
there is no separate **Reviewed** or **Explicitly reviewed** checkbox. Notes
without a family change export as a plain **Comment** row.

A suspected non-uniform tile is never deferred to a later questionnaire merely
to discover its corners. Its first review card provides both paths in one place:

- **This tile is uniform**, with a material-family selector; or
- **Assign corners individually**, with independent NW, NE, SE, and SW family
  selectors.

The same card shows the complete target tile and titled uniform comparison
tiles for the current family and the strongest trustworthy alternatives. If the
inverse audit supplies evidence, the page labels it as an uncalibrated ranking
with source atlas, slot, rotation, residual, and runner-up margin. It must not be
presented as a probability or silently applied. HD and original artwork remain
separate evidence classes. Any target, connected lead, or equivalent tile queued
in the current batch is globally untrusted for that batch and cannot be reused
as a comparison reference.

### Locked questionnaire presentation

The compact R81/R82 review layout is the permanent semantic-atlas questionnaire
contract. It must not be redesigned without direct user authorization:

- Every target and reference in one comparison row uses identical displayed
  dimensions **and identical texture/pixel scale**. A target corner is compared
  with the equivalent corner crop from a reference, never with a whole uniform
  tile magnified or reduced into the same CSS box.
- Every concrete family offered by a selector has a titled reference visible on
  that same card. Only **Different family/name** may omit a reference.
- Family references are global across every reviewed atlas. When the user asks
  to see a named family, show its best trustworthy HD and original anchors even
  when they come from another zone; do not restrict references to the target
  atlas unless the user explicitly requests a same-atlas comparison.
- Global lookup supplements rather than replaces local evidence. Show every
  trustworthy uniform variant from the target atlas first, then useful
  cross-atlas anchors; catalog iteration order must never hide a local variant.
- The card order is target whole tile, horizontal comparison row, controls, then
  optional expanded evidence.
- Corners display as NW/NE above SW/SE and serialize as NW, NE, SE, SW.
- HD and original evidence stay separate but use equivalent rows and scales.
- There is no review checkbox or **Not answered** option. A changed selection or
  comment is explicit; untouched defaults are omitted from the export.
- CSS, layout, and control behavior remain locked unless the user directly asks
  for a presentation change.

## Saving and profile identity

The built-in default is immutable and is not a local override. Opening it and
choosing **Apply + Close** without editing creates no file and leaves the default
active. Editing the default, applying a generated draft, or applying an already
loaded local profile writes an explicit local override to the visible portable
`terrain-atlas-profiles` folder beside the editor executable and refreshes the
paint palette. Approval is derived from whether its
active assignments are structurally valid; it is not a separate authoring
action. Unassigned unusual tiles remain excluded. **Save copy...** chooses
another location and **Load profile...** opens such a copy as an explicit local
override.

**Reset to default** removes only the managed local override for the current
namespace and exact atlas, then immediately restores the shipped preset. Profile
copies saved elsewhere are never deleted. The action is disabled for a custom
atlas that has no exact built-in preset.

Landscape Tools labels the active source beside **Material atlas...** as
**Default atlas**, **Generated draft**, or **Local override: filename**. The
dialog repeats that source and shows the full local path. Only an exact-bound
profile with explicit user-authored provenance may override a built-in default;
generated/default cache files from older builds are ignored. AppData profiles
from older builds are not migrated or loaded. Distribution packages contain no
local override profiles.

Current profile schema **10** records tile-preview corner space, paint
selectability, allowed layers, runtime tile types, texture count, and per-slot
route owner. Schema 7 is a historical format, not the current writer version.
Pixel-inferred legacy profiles already contain visual corners and keep them unchanged. Known authored
profiles are reflected once from their former paint-grid representation while
preserving names, thumbnails, and user edits. A profile
is accepted only when all three bindings match the loaded atlas:

- internal MPR namespace;
- SHA-256 of the decoded atlas pixels;
- atlas slot count.

“Profile does not match this atlas” therefore means the chosen profile belongs
to another namespace or atlas revision, not that its outer filename differs.
No profile silently transfers semantic meaning across those boundaries.

## MPR save and published atlas identity

Save MPR can update the loaded file in place; Save MPR As selects another target.
Neither a new MPR name nor separate Ground/Overlay sessions are mandatory.
The save policy publishes new artwork/working-extension references or explicit
identity changes to the configured **HDPack_Terrain.res** (publication target
since Ataman 52). Ordinary semantic/height-only saves do not republish the RES.
Existing archive members are preserved; replacement uses staged validation,
backup and guarded rollback. Reusing an atlas identity can affect other maps.

Since Ataman 122 the exact semantic field is embedded in MPR as
**ei_mapper_terrain_semantics.json**, with Ground and Overlay state and
namespace/tile/atlas/profile bindings. MPR semantics take precedence over the
legacy RES member; old RES state is read when MPR semantics are absent.
These semantic fields differ from review-profile copies in terrain-atlas-profiles.
See [current MVP save contract](MODERN_TERRAIN_EDITOR_MVP.md).
