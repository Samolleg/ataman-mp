# MOB hardening validation

Validation date: 2026-07-16

This report records read-only validation of the strict raw-section `MobDocument`.
No game or mod files were written, copied into Git, or opened through the editor UI.

## Automated tests

- Qt 5.12.12 / MSVC Core test target: 13 passed, 0 failed.
- Qt 5.12.12 / MinGW production build: succeeded.
- Covered strict bounds, truncated headers, opaque unknown leaves, structural
  duplicate IDs, exact no-op bytes, staged reopen, and added/moved semantic diff.
- Covered provenance-checked leaf replacement, stale-provenance rejection, exact
  untouched-leaf emission, ancestor-length derivation, and strict CP1251 encoding.
- Full focused suite: 44 passed, 0 failed (document 13, profile 6, runtime
  provider 4, backup 6, duplicate-ID reporting 4, resource naming 11).
- Production executable SHA-256:
  `FBFAD35E81DD8BFC9FA15B8943D7BA8D92AAF48B62DC9B9CB681C907AB86EA98`.

## Private integration corpus

- Base EI `Maps`: 99 MOB files.
- JMV `Maps`: 153 MOB files.
- Total: 252 files passed complete byte coverage and byte-identical no-op round trips.
- One corpus file reported duplicate IDs; all occurrences were preserved.
- The corpus remains outside Git. This report contains no proprietary payloads or
  user-specific installation paths.

## Runtime stack

The built-in Base and JMV runtime manifests contain the approved development
stack hashes. `--profile-load <game-root> --json` reports each observed hash and
distinguishes a complete stack from the exact validated stack.

The current JMV development installation matched all six required components:
runtime stack complete and validated, with zero rejected component hashes.

## Remaining safety boundary

This validates the strict read-only document tree and no-op serialization. It
does not yet prove changed-save losslessness. Dirty MOB saves are therefore
refused at the Save boundary instead of falling back to the legacy rebuilding
serializer. Changed saves return only after UI edits are migrated to
provenance-based `MobEditTransaction` operations and candidate changes are gated
by semantic diff.

Direct game launch remains unavailable. No live map, Starter selection,
configuration, save, executable, addon, or Lua file was modified during this pass.
