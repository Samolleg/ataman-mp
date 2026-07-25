# MPR maxZ transaction foundation

This internal foundation proves the first serialization boundary for the next
MPR Settings stage. It does not expose `maxZ` mutation in the UI yet.

## Proven behavior

- `maxZ` is patched only in the existing MP member at byte offset 4.
- The replacement is same-length and provenance-checked through the normal
  `CMprEditTransaction` path.
- A fresh reopen must parse the requested finite positive value.
- Archive member names, order, offsets, timestamps, collision links, unknown
  payloads, and every SEC payload remain byte-identical.
- Reapplying the exact stored float is a no-op and emits no transaction edit.
- Zero, negative, non-finite, missing, and ambiguous MP cases fail closed.

## Remaining gate before UI enablement

The combined undo command must atomically coordinate this MP edit with either:

1. unchanged raw samples (**Scale terrain with range**), or
2. deterministic raw-height requantization (**Preserve world heights**).

Its preview must report world-range change, rounding error, clamped vertices,
changed SEC members, navigation invalidation, and MOB height risk. Save must
then pass the existing staged reopen, semantic diff, backup, and atomic commit
contract. Until that complete command is proven, the MPR Settings scale page
remains disabled.

No proprietary map or live game installation is used by this test.
