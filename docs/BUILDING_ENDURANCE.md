# Building EI Mapper: Endurance Edition

## Supported baseline

- Windows x64
- Qt 5.12 or newer with the MinGW x64 toolchain
- qmake
- C++11

The application target is `EIMapperEndurance.exe`. The upstream v0.3 binary and
its `options.json` are not build inputs and are never overwritten.

## Application

From a Qt 5.15 MinGW command prompt:

```powershell
qmake ei_maper.pro CONFIG+=release
mingw32-make -j4
```

Build into a separate shadow-build directory for normal development.

For unattended build verification, use `--ui-widget-check`, the headless MOB/MPR
commands below, or the non-dialog `--version` option. Validation must not launch
the editor without an explicit file or leave About/version windows open.

Before handing over any tester folder or creating a distribution archive, run:

```powershell
.\packaging\finalize_package.ps1 <staged-package-directory>
```

This is mandatory for direct tester folders as well as ZIP releases. It removes
`endurance-options.json`, upstream `options.json`, logs, saves, and crash dumps;
regenerates `SHA256SUMS.txt`; and then runs the strict distribution validator.
Never use a folder that has already been launched as an unsanitized package
source. A fresh package must enter first-run game/mod setup.

`validate_distribution.ps1` remains the non-mutating CI check. It must pass
after finalization; do not bypass either step for convenience builds.

## Unit tests

```powershell
Set-Location tests
qmake endurance_document_tests.pro CONFIG+=release
mingw32-make -j4
.\release\endurance_document_tests.exe

qmake backup_manager_tests.pro CONFIG+=release
mingw32-make -j4
.\release\backup_manager_tests.exe

qmake resource_profile_tests.pro CONFIG+=release
mingw32-make -j4
.\release\resource_profile_tests.exe

qmake resource_name_tests.pro CONFIG+=release
mingw32-make -j4
.\release\resource_name_tests.exe

qmake duplicate_id_report_tests.pro CONFIG+=release
mingw32-make -j4
.\release\duplicate_id_report_tests.exe

qmake runtime_provider_tests.pro CONFIG+=release
mingw32-make -j4

qmake profile_database_tests.pro CONFIG+=release
mingw32-make -j4

qmake profile_visual_provider_tests.pro CONFIG+=release
mingw32-make -j4
.\release\runtime_provider_tests.exe

qmake view_frustum_tests.pro CONFIG+=release
mingw32-make -j4
.\release\view_frustum_tests.exe
```

## Private JMV validation

Do not copy JMV maps into the repository. Run the built validator directly
against the live files in read-only mode, or preferably against hash-verified
copies:

```powershell
EIMapperEndurance.exe --validate <map.mob>
EIMapperEndurance.exe --roundtrip-check <map.mob> --json
EIMapperEndurance.exe --roundtrip-check <map.mpr> --json
EIMapperEndurance.exe --profile-load <game-installation> --json
```

`--roundtrip-check` never writes the input file.
`--profile-load` only discovers and validates a recommended resource profile;
it does not write settings or modify the installation.
