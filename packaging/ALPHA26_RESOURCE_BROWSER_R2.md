# Resource Browser r2 test build

## What changed

- Replaced the artificial archive tree with flat archive rows. Each row now
  shows member count and archive size directly.
- Selecting an archive now filters the content list to that provider; select
  **All archives** to return to the complete effective resource set.
- Corrected MMP widget previews to use the editor's established vertical image
  orientation without changing archive bytes or UV semantics.
- Added bounded background CPU previews for standalone FIG8 geometry and MOD
  assemblies, including LNK hierarchy and same-stem BON offsets when available.
- ANM resources now show their action/channel inventory and, when available,
  the same-stem MOD model as visual context.
- Extended automated archive mutation checks for add, replace, rename, delete,
  unchanged payload offsets, source immutability, and stale-write destination
  preservation.

## Manual checks

1. Open **View > Resource Browser**. Confirm archive rows have no expand arrow
   and show **Members** and **Size** columns.
2. Select one archive, then **All archives**. Confirm the member list changes
   to the selected provider and returns to the full catalog.
3. Preview a known MMP and compare its orientation with the editor's other
   texture/minimap views.
4. Preview representative FIG, MOD, and ANM resources. Confirm geometry remains
   responsive while loading and ANM shows its action/channel inventory.
5. Use only a copied/disposable archive for any manual mutation test.

## Known limitations

- The browser's model preview is an untextured, fixed-angle CPU render. It does
  not create mutable scene/cache state or depend on an OpenGL context.
- Standalone ANM playback is not synthesized: correct posing requires the
  provider ADB action/timing context. ANM currently shows the paired static
  model plus a verified action/channel inventory.
- LNK and ADB files without a containing MOD/ANM context retain metadata/hex
  fallback.
- Nested RES navigation, audio playback, and external-editor save watching are
  not part of this revision.
- Rename/delete reference coverage remains explicitly incomplete for opaque or
  dynamically constructed script references.
