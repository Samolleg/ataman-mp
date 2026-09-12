# Combined be440676 baseline and world terrain fixes — R5

## Owner acceptance — 2026-09-12

After testing the delivered R5, the owner confirmed "Всё работает как надо"
and requested final PR submission. This supersedes the pending user checks below
for the reported painting and moving-seam scenarios. The broad UI diagnostic
failure remains disclosed and was not fixed by this work.

The owner explicitly selected base commit
`be4406766c19257d86f85c2e02ab7dd45a96ff4d` (PR #6). R1–R4 were based on
main `ffdc28ea` plus PR #7 and omitted that branch's HD texture approvals.
This was an integration error, not evidence that the texture approvals had failed.

R5 merges that exact history into the existing world-sculpt contributor branch,
retaining published history without force-push or a main merge. It includes PR #6
database placement/identity, exact HD atlas approvals and DejaVu profile changes,
alongside all R4 world-sculpt fixes. Code merged automatically. Conflict resolution
kept both roadmap sections and both sets of translation messages; the XML was
parsed and the binary translation regenerated (3017 completed translations).
The HD presets and archive binding resolver match be440676 byte-for-byte.

## Verification

- Rebuilt document suite: 10 passed, no failures/skips. Native read-only decoder
  confirms 38/38 HD namespaces approved, all 88 material families supplied, and
  41/41 installed MPRs bound to exact approved HD sources. Additional cases check
  preset/embedded/archive binding and object-to-lever raw-child preservation.
- Rebuilt world terrain suite: 13 passed, including whole shifted-seam movement,
  installed three-map calculations, history, protection and cohort save/reopen.
- Widget suite: 16 passed / 2 offscreen context skips. Separate native GPU
  regression: 3 passed, no skips.
- Release editor and x86 runtime components built successfully.
- Full native-environment `--ui-widget-check` still fails with `0xC0000005`.

Installed game data was read only and not packaged. Interactive painting artwork,
stroke behavior on the recorded scene, transitions and viewport performance are
still manual acceptance. No new claim of full database/profile runtime acceptance
is made beyond the inherited be440676 validation and checks above.

R5 is a new combined-baseline tester folder, with the exact base explicitly in
BUILD_INFO. Prior test folders are retained. See `packaging/WORLD_TERRAIN_SCULPT_R5.md`.
