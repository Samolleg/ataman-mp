# Ataman76 distribution — 2026-09-27

Task: EI-PORTABLE-DIST-76. Branch: codex/portable-direct-launch-v76.
Source implementation: 92a1edf8f5ebc773af0d655abaafbc9d80f3149c.
Handoff base: 9fc6decf0baf1cb65a543f33b5c1bb94c071f9af.

Repackaged the previously built and tested binaries without source changes or
version increment. Created a new owned build-staging-76-distribution, checked
--version76 before finalization, finalized, then copied to:

`C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-76-distribution`

Source and destination distribution validators PASS. Finalizer PASS,0 stale
files removed. All128 copied files match finalized staging SHA256;127 managed
files also match the permanent installation, which required no replacement.
No user settings, logs or game data included. No ZIP requested or created.
Finalized staging and distribution were not launched.

EXE SHA256: C9015E3E6E7CDF6E2BEFBD8AF507F1226EAC22F0B80EFBBE0658068120DC152E.

Functional evidence and limitations remain as recorded in
ATAMAN_PORTABLE_DIRECT_76_20260927.md:11 profile tests,57 launcher tests and native
isolation/UI checks passed;3 optional external scenarios skipped. Actual portable
gameplay acceptance remains open. Packaging success does not close it.
