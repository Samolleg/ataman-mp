# Ataman95 distribution, 2026-10-02

Task EI-DISTRIBUTION-95-20261002. Owner requested a runnable distribution.
Branch codex/distribution-v95-20261002; documentation base
93e25344157052b461f9081d732ccb96facf8a6f, application code
3cbf3cedb9390cc1d490de000592d0b7b7006319. VERSION95 unchanged.
No code changes or new compilation: reuse the exact tested95 release executable
and freshly built runtime components documented in
[assembly validation](ATAMAN_ASSEMBLY_CATALOG_95_20261002.md).

## Package verification

Existing clean build-staging-95-assembly passed the distribution validator.
Only its127 manifest-listed files were copied into a previously nonexistent
owned build-staging-95-distribution-20261002; no unlisted tester files were copied.
The current docs/OBJECT_ASSEMBLIES.md was added to make the Readme assembly guide
available to recipients. Qt including Qt5OpenGL.dll, matching MinGW DLLs,
Windows plugins, current game-launch runtime, LICENSE and NOTICE are included.
Game data, profile model-catalog.json, private fixtures and settings are excluded.
One current CHANGELOG.md only; no ZIP created.

Raw/staged editor and both runtime binaries matched SHA-256 before finalization.
Hidden staged --version returned EI Mapper Endurance Edition95, exit0.
Finalizer and independent staging validator exited0. The fresh finalized folder
was moved to the previously nonexistent destination after absolute containment
checks; final validator exited0 and checked128 managed files and complete
manifest coverage. No executable was run after finalization.

Distribution:
C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-95-distribution.

Editor SHA-256:
5A96196B1B1664CC615E4CD2EC34501795D4C55AA3738477A111D52D402639B3.

## Permanent installation and preserved files

Permanent target remains
C:/GOG Games/maped/ei-mapper-ataman/artifacts/EI-Mapper-Ataman-2-first-test.
All127 managed file hashes and its manifest already match the verified95
program package. No installation write or redundant replacement was necessary;
user settings, maps, logs and unlisted files were not opened for modification.
The existing verified rollback94→95 remains available. The finalizer was never
used on the permanent target. Endurance and installed game/profile data were
not modified. Foreign ROADMAP changes, validation52, translated QM and untracked
distribution86 report remain outside this scoped documentation commit.

Evidence: build-tests-95-distribution-20261002/package.ps1, result.json,
version.out/.err, validate-source.log, finalize.log, validate-stage.log,
validate-distribution.log. First roadmap-write command failed before execution
because python was not on PATH; corrected with native PowerShell text I/O.
This was a documentation-driver failure, not a package/test failure.

## Functional boundaries

Exact unchanged EXE retains the prior95 evidence: assembly57 PASS/0 FAIL,
rotation25 PASS/0 FAIL, connector42 PASS/0 FAIL and focused widget/catalog/theme
checks exit0. No redundant functional rerun claimed in this packaging task.
Broad --ui-widget-check has10 existing clipboard failures, scenario check
48 PASS/2 existing failures; document/resource tests have18/2 unavailable external
fixture skips and database preview lacks required fixture environment.
None are reclassified as PASS. Manual GUI/game acceptance, including real
Blender modules, remains open. Manifest integrity proves package completeness,
not complete editor correctness.

Status: clean runnable distribution ready, permanent installation already95
and verified. Recipient selects their own game/mod profile on first launch;
existing unresolved parent tasks are not closed by distribution.
