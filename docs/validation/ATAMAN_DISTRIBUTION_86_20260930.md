# Ataman86 distribution package — 2026-09-30

## Source and package

- Requested source: `7c3f1abede433cf0b6790cfba75031ed6bfb8b49`.
- A clean tracked snapshot was extracted from that exact commit; the current checkout's unrelated working changes were not included in the build.
- Fresh Release build completed with the documented Qt 5.12.12/MinGW wrapper. Qt deployment used `windeployqt --release --compiler-runtime`; the matching 32-bit runtime bridge/host were rebuilt by the wrapper.
- Package: `C:\GOG Games\maped\ei-mapper-ataman\build-staging-86-distribution`.
- `VERSION` and raw/staged `--version`: `86`.
- Raw and staged EXE SHA-256: `1FBA711BF847A6CE099623B871C25C80023265781F60AD6DC522D675709E27A9`.
- Finalizer: PASS, removed 0 files. Independent distribution validator: PASS. Independent SHA audit: 127 manifest entries, 0 errors; package contains those 127 files plus `SHA256SUMS.txt`.
- The package includes one concise `CHANGELOG.md`; no ZIP was created.

## Functional evidence and limits

- The exact source commit's earlier committed EXE passed the focused 18-check profile catalog UI set, full `--ui-widget-check`, model gallery/database creation checks and scenario check 50. See `ATAMAN_PROFILE_MODEL_CATALOG_86_20260930.md`.
- On this fresh rebuild, `--profile-model-catalog-check` passed all 18 checks when run with the normal Qt Windows platform. An earlier attempt using the unavailable `offscreen` plugin hung; only that test process was stopped.
- Fresh `--model-gallery-check` and `--database-creation-check` passed. Fresh `--ui-widget-check` failed on clipboard/paste and component-copy checks (including `compatiblePastePreview`, `fieldPastePreview`, `componentPlatePaste`, and `selectedComponentCopy`). Fresh `--scenario-layer-check` reported 48 passed and 2 failed: synthetic recovery snapshot in isolated Qt test data, and synthetic MOB load without unexpected modal warnings. The causes were not isolated. These fresh failures are not PASS; the earlier exact-source passes do not prove this rebuilt EXE.
- After the editor closed, the permanent installation at `C:\GOG Games\maped\ei-mapper-ataman\artifacts\EI-Mapper-Ataman-2-first-test` was refreshed under the build handoff contract. Four managed files changed (EXE, changelist, runtime bridge and runtime host); all 127 installed managed hashes match the new manifest. The installed EXE reports version 86 and has the same SHA-256 as the fresh/staged EXE. All 15,359 files outside the managed manifest remained byte-identical. Rollback of the prior v86 files is retained at `artifacts/rollback-EI-Mapper-Ataman-v86-rebuild-20260930`; the superseded v85→86 rollback generation was removed after verification.
- The staged package and refreshed installation must be described as having the unresolved automated check failures listed above. They are an exact-source rebuild with valid package integrity, not a fresh functional PASS.
- Owner visual acceptance on real profile models remains outstanding.
