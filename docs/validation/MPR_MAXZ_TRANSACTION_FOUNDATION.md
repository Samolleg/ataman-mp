# MPR maxZ transaction foundation

This increment proves and exposes the complete non-structural `maxZ` migration
boundary in **MPR Settings > Vertical scale**.

## Proven behavior

- `maxZ` is patched only in the existing MP member at byte offset 4.
- The replacement is same-length and provenance-checked through the normal
  `CMprEditTransaction` path.
- A fresh reopen must parse the requested finite positive value.
- Archive member names, order, offsets, timestamps, collision links, unknown
  payloads, and every SEC payload remain byte-identical.
- Reapplying the exact stored float is a no-op and emits no transaction edit.
- Zero, negative, non-finite, missing, and ambiguous MP cases fail closed.
- **Scale terrain with vertical range** retains raw samples while changing
  world-space heights.
- **Preserve world heights** deterministically requantizes every raw sample.
  Rounding error and clamped vertices are reported before application.
- Clamping requires an explicit opt-in; it is never inferred or silent.
- A scale migration is one undoable command coordinating current `maxZ`, raw
  height deltas, renderer preview, MOB reprojection, MPR dirty state, and graph
  invalidation.
- Because vertical scale changes terrain slopes in world space, derived packed
  normals are rebuilt in every SEC even when raw samples are unchanged.
- Changed saves append the same-length MP edit and required SEC edits to the
  normal staged transaction, then use fresh reopen, semantic diff, backup, and
  atomic replacement.
- Synthetic coverage proves a combined MP+SEC candidate reopens and leaves an
  unknown archive member byte-identical.

## Deliberately separate structural work

Crop/expand is not part of this command. It changes the sector lattice, may add
or remove SEC members, and requires runtime-valid archive hash chains. It stays
disabled until a dedicated Save As-only structural transaction is proven.

No proprietary map or live game installation is used by this test.
