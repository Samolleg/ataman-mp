# Ataman 25 distribution — 2026-09-18

EI-DISTRIBUTION-025, codex/distribution-v25. User requested a distributable folder.
No ZIP/publication requested. Repackaging retains version 25, no code changes.
Source SHA: 8c6764c0ebe101d0471bd1f8c03834ab0475f9a5.

Input build-staging-25-rotation was revalidated with the current validator and
its EXE matched F8BDCF76B3D797B3FCC42E8E33E38E9E21AD9A2020165E5ABC0B9EEF9013293E.
Created new owned build-staging-25-distribution, copied only the clean package,
replaced Readme/CHANGELOG with recipient-oriented first-run instructions.
Retained all libraries, runtime components, synthetic fixtures, LICENSE/NOTICE.
Finalizer ran only on the new staging and removed zero files. No package was run.

Output: artifacts/EI-Mapper-Ataman-25-distribution.
101 files, 76,458,914 bytes including SHA256SUMS.txt.
Final validator PASS. Independent check: 100 hashes, unique safe paths, full
coverage, version 25; all files except the two packaging documents match the
previous tested clean package. Text/config scan found no author paths or recent
file settings. Permanent installation and earlier distribution/ZIP unchanged.

Existing binary validation: document suite 184 PASS / 18 optional SKIP; native
hidden UI PASS, creation/navigation PASS, 47 scenarios PASS, zone1/zone9 each
42 PASS (1527/1396 static rotations). No redundant rebuild/test run for repackaging.
Details: ATAMAN_NEIGHBOR_ROTATION_25.md. Manual scene/FPS acceptance and execution
on another computer remain unverified; package integrity is not stable-release
certification. Deliver the entire folder to the recipient.
