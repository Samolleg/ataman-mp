# Material atlas

Endurance reads the existing atlas images exactly as the game does. It does not
edit or generate atlas imagery. It builds a material catalog describing which
existing slots represent Ground, Underwater, or Overlay materials and which
material appears at each tile corner.

Zone 1 is the authored behavioral baseline. Its supplied definition opens with
the same names, assignments, layers, and thumbnails used by the paint palette
and can be saved unchanged. Other namespaces use the same parser and catalog;
they do not require a per-zone painting algorithm.

## Painting

Landscape Tools has separate **Ground** and **Overlay** modes. Ground and
Underwater materials remain visible in their own palette sections. Overlay
materials appear only in Overlay mode, so a ground texture cannot be painted as
an overlay or vice versa.

Each material is a pressed thumbnail button. The palette composes four authored
tiles around one painted vertex, so the preview shows the result of a size-1
brush instead of one isolated atlas slot. Every valid 1-on-3 relationship can
produce a contextual vertex card. This includes a transition-only center family
(for example, **Irregular on Dark**) and the inverse case where the center has
ordinary uniform tiles but its support does not (for example, **Stone on
Ground**). These cards rotate the one authored tile around a vertex to form the
complete four-tile junction. They do not duplicate or merge profile assignments.
Hovering shows the exact card name and assigned-slot count.

All semantic material brushes operate on terrain vertices. Size 1 selects the
nearest vertex and resolves the maximum four tiles sharing it. Larger circles
and squares select every vertex in the visible footprint; each affected tile is
then chosen from its four requested corner materials and the authored atlas
rotation. MPR storage, preview, save, and undo remain tile-based.

Some authored detail materials have no uniform four-corner tile. Each contextual
palette card therefore retains the exact support material shown in its preview.
A click first places the authored four-tile `detail on support` vertex motif,
with the correct rotation in every incident tile. If two copies touch but the
atlas has no required 2-corner or 3-corner transition, the later click remains
paintable: each missing incident tile uses the closest local authored signature
that still contains the selected material at the selected vertex, and the
editor reports a diagnostic warning. Larger brush sizes still place only the
center motif for a sparse family. This keeps the result bounded and makes the
atlas limitation visible without turning it into a workflow blocker. Ordinary
materials retain the normal vertex-area brush and hard-edge fallback. No paint
action expands beyond tiles incident to the requested vertices.

The coverage label beside **Material atlas...** reports, for example,
`509 / 512 assigned (3 unassigned)`. Deterministic, structurally valid
assignments are usable immediately—even before explicit approval. Unassigned
tiles are excluded from automatic painting. When a requested 3-way or 4-way
transition does not exist, painting uses a hard edge and records a diagnostic;
it never invents atlas content.

## Visual editing

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

Review previews and schema-6 profiles use one literal visual contract: NW is
the material visible at the upper-left of the displayed tile, and likewise for
NE, SE, and SW. The paint compiler reflects that signature once into terrain
grid space and applies the renderer's one fixed packed-rotation convention.
Atlas content cannot redefine the meaning of the MPR rotation bits.
Selecting a tile never recolours its texture. The thumbnail member is marked with a bold
outlined **T** in the expanded material list; selecting only a material shows
that thumbnail without selecting or editing a tile.

Ground/Underwater and Overlay families cannot coexist in one tile. The editor
rejects such a drop immediately, explains the conflict, and leaves the previous
assignment unchanged. An unusual inferred tile that initially crosses layers
stays unassigned; drag it to the correct material first, then refine corners
within that layer.

There are no family and slot “Needs review” checkboxes. Slot ambiguity is
resolved by assigning or excluding the tile. If the parser could not prove a
material's identity or classification, that material shows one explicit
**Confirm material** action with the reason. Tile moves and corner edits update
all dependent memberships automatically.

## Saving and profile identity

**Apply + Close** writes the current profile to the visible managed path and
refreshes the paint palette. Approval is derived from whether its active
assignments are structurally valid; it is not a separate authoring action.
Unassigned unusual tiles remain excluded. **Save copy...** chooses another
location and **Load profile...** reopens such a copy.

Profile schema 6 records tile-preview corner space explicitly as well as
whether an authored internal family is paint selectable. Pixel-inferred legacy
profiles already contain visual corners and keep them unchanged. Known authored
profiles are reflected once from their former paint-grid representation while
preserving names, thumbnails, and user edits. A profile
is accepted only when all three bindings match the loaded atlas:

- internal MPR namespace;
- SHA-256 of the decoded atlas pixels;
- atlas slot count.

“Profile does not match this atlas” therefore means the chosen profile belongs
to another namespace or atlas revision, not that its outer filename differs.
No profile silently transfers semantic meaning across those boundaries.
