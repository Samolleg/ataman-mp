# Alpha 26 modern terrain atlas MVP test

## What changed

- Terrain brushes now represent one unique vertex material identity. A drag
  previews that material unblended, and every touched vertex remains mandatory
  when the stroke resolves on release.
- Exact authored four-corner tiles are reused first. Missing 1/3, 2/2,
  three-family, and four-family tuples remain exact virtual identities and are
  generated deterministically during atlas finalization.
- Finalization deduplicates rotations and identical pixels, rebuilds external
  MMP pages and mipmaps, remaps every used terrain reference, and writes a
  deterministic map-specific MPR/RES namespace without replacing the loaded or
  shared atlas.
- Map-wide usage accounting removes only truly unused slots. The MVP clearly
  refuses output above the currently validated 512-slot ceiling instead of
  silently substituting a different material.

## Focused checks

1. Open Zone 1, select terrain painting, and confirm each paintable material is
   shown once rather than as several `on ...` context variants.
2. Draw both a single-vertex mark and a wider continuous stroke. During the drag
   the chosen material should be visibly unblended; after release, every touched
   vertex must remain that material. Undo and redo should treat the stroke as
   one action.
3. Repeat in Zone 2 with several incomplete/mixed material families, including
   adjacent strokes. Missing transitions must not drop a requested vertex or
   replace its identity merely because the shipped atlas lacks that tuple.
4. Use **Finalize virtual atlas** and choose a new empty output location. Confirm
   that it creates an isolated renamed MPR, an `-atlas.res` resource archive,
   and an audit manifest, while leaving the original map and shared resources
   unchanged.
5. Reopen the generated MPR in the editor and inspect painted seams and rotated
   transitions at multiple zoom levels.

## Known limitations

- The finalizer refuses more than 512 used/generated slots pending controlled
  9-page and 16-page validation in the shipped runtime and Endurance renderer.
- Land and overlay virtual edits must be finalized in separate sessions.
- An inactive overlay region without a complete recoverable vertex baseline is
  refused instead of being guessed or flattened.
