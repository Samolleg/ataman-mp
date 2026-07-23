# Map Catalog Editor Integration Draft

Status: deferred until the baseline landscape-editing branch is closed.

This note defines how a verified `map.txt` save should become visible to the
rest of Endurance without silently replacing loaded MPR/MOB data or discarding
work. It is an implementation draft, not a statement of current behavior.

## Save report

An integrated `CMapTableEditorDialog` should report each successful verified
save with:

- the canonical destination `texts.res` path;
- the old and new archive and `map.txt` hashes;
- the affected zone identities;
- a semantic summary separating comments, entrances, `#maps`, zone metadata,
  and MPR/MOB registration changes.

The standalone editor can ignore this report. The main editor should collect
reports while the modal catalog window remains open and process the final
active-profile change after the window closes. This prevents reload prompts
from appearing behind the catalog window.

## Active-catalog detection

The main editor must compare the canonical saved archive path with
`CMapCatalog::load(...).sourceArchive` for the active resource profile.

- Saving another archive does not refresh the active editor. Report that it
  was saved outside the active profile.
- Saving the active archive refreshes the catalog and map browser once.
- Atomic replacement of the active archive must still count as the same path.

Catalog loading should have one owner. A proposed
`MainWindow::refreshActiveCatalog(reason, saveReport)` loads `texts.res` once,
then supplies the same `SMapCatalogResult` to the map browser and active
workspace binder. This replaces the current duplicate loads performed by
`refreshMapsBrowser()` and `refreshMapTestContext()`.

## Loaded-workspace response

| Change | Required behavior |
| --- | --- |
| No workspace loaded | Refresh map and collection lists. |
| Only comments or unrelated zones changed | Refresh lists; leave the workspace untouched. |
| Loaded zone entrances, `#maps`, sky, type, or other catalog metadata changed | Rebind the catalog context and rebuild entrance controls/overlays without reloading MPR/MOB files. |
| Loaded zone resolves to a different physical MPR or MOB | Ask whether to reload the updated pair. **Keep Working** leaves the current workspace open. |
| Loaded zone was removed or renamed | Keep the workspace open, mark it unregistered, and disable catalog-dependent map testing until an identity is selected. |
| A collection is loaded | Preserve its explicit MPR/MOB composition; refresh its mapped zone metadata and entrances. Report a non-blocking mismatch if the catalog registration now names other resources. |

The prompt occurs only when the resolved physical composition changes. A
comment, minimap name, entrance, sky, or other metadata edit must not force an
MPR/MOB reload.

**Reload Now** must use the existing unsaved-change contract before unloading
anything. Cancelling that contract or choosing **Keep Working** leaves a
visible `Catalog changed - workspace refresh deferred` state. The catalog save
has already succeeded, so this choice must not be worded as cancelling the
save.

## Preserving editor state

The current `refreshMapTestContext()` performs full-load cleanup, including
clearing custom spawn state. Catalog refresh needs a separate preserve-state
path or an explicit policy parameter.

During a metadata-only refresh:

- preserve the loaded MPR/MOB documents and undo stacks;
- preserve the selected entrance when the same route still exists;
- preserve a terrain custom spawn while the physical MPR is unchanged;
- rebuild entrance overlays and update sky/catalog-dependent controls;
- invalidate catalog-dependent runtime staging when the mapped identity is no
  longer valid.

An edge-zone exit ID is not unique. Entrance selection must therefore use a
stable route key such as `(exit ID, destination zone, destination exit,
occurrence)` rather than exit ID alone.

## Entry points

Both existing entry points should call the same coordinator:

1. **Refresh maps and collections** requests a catalog refresh and active
   workspace rebind.
2. A verified save to the active `texts.res` queues the same refresh after the
   catalog dialog closes.

Opening a map pair can continue to use `openMapPair()`. Opening a collection
can continue to use `openCollection()`. Catalog refresh must not call either
method unless the user accepts a physical resource reload.

## Initial validation matrix

1. Save an inactive copied archive: no active map-list or workspace change.
2. Save active comments only: list refresh, no workspace reset.
3. Change an active entrance: selector and overlay update; undo stacks and
   custom spawn remain intact.
4. Add repeated edge exit IDs: all routes remain selectable by occurrence.
5. Change active MPR/MOB registration with dirty terrain or MOB edits: reload
   prompt appears, **Keep Working** preserves all edits.
6. Accept reload: the normal unsaved-change contract runs before replacement.
7. Remove the loaded identity: workspace remains open but map testing becomes
   unavailable until an identity is selected.
8. Refresh a collection after catalog resource names change: explicit
   collection composition remains unchanged and a mismatch is reported.

## Out of scope for the baseline

- automatic archive installation or resource-profile switching;
- silent collection rewriting;
- silent terrain/MOB reload;
- atlas generation or RES patch generation;
- minimap texture export and archive insertion.
