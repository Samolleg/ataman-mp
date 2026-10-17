# Ataman202 — EI-WORLD-HISTORY-202 — 2026-10-17

## Request and previous behavior

Preserve shared-brush Undo/Redo after Save; validate on a copy of DejaVu zone3obr, build and install. Base201 main b123e5bd. Previously saveWorldTerrainMaps explicitly cleared shared history and rebuilt document/atlas bindings. The201 regression asserted this checkpoint; simply deleting clearHistory would retain stale SEC hashes and semantic baselines.

## Implementation

Before publishing, prepare a detached session whose current and historical paint models adopt the new saved SEC payload hashes, height baseline and semantic baseline. Preserve their current values, layout offsets, materials and artwork. Swap this prepared history only after successful publication; retain index and Redo branch. Disk baseline is decoded once per saved document, not once per historical snapshot. Keep the ordinary Undo stack during atlas adoption.

Historical atlases may predate pages already published by a later command. When saving such a state, retain the saved but currently unused physical tail and slot types. Shared Save continues preserving native page sizes and checked source identities. Refresh disk page/profile bindings without replacing historical artwork. UI text now states that Undo/Redo is retained.

Scope: history survives Save within the open session. Closing/reopening still starts a fresh editing history. No game files modified for testing, no new transition kernel, no network publication.

## Validation

Private directory `build-world-history-202-20261017`:
- `probe.out`:58 native checks PASS on copied real zone3obr: actual strokes, Save/Undo/Redo, dirty/clean state, Save after Undo, remaining Redo, branching, repeated saves and reopened final sector data. Original page headers/native untouched pixels retained.
- `unit-results.txt`:3 QtTest passes,0 fail/skip; real shared height command across two synthetic maps, adopt saved baselines, Undo restores both originals, Redo restores saved content and clean state. Initial test fixture lacked the materials normally loaded by CWorldTerrainSession; fixed fixture initialization before counting PASS. Initial PowerShell runner mistook compiler stderr warnings for command failure; corrected error handling, no product change for that runner issue.
- Fresh Release202 and both runtime components built. `final2.out`:62 PASS /exit0 on a fresh copied source, including cancellation, clean/dirty checks, and exact untouched water/transition pixels after final reopen (not just the in-memory atlas). `final.out`61 PASS preceded this added physical-readback assertion.
- `original-hashes.json`: read-only game-source identities used to create test copies. Test writes only private fixture. No claim of owner visual/game acceptance.

## Handoff

Native and unit gates passed. Pending scoped source commit, snapshot identity, clean staging and guarded installation over201. Preserve owner settings/data and retain verified previous program rollback.
