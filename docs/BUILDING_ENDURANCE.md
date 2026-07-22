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

### Windows paths containing Cyrillic or other non-ASCII characters

The Qt 5.12 MinGW resource compiler used by the supported toolchain cannot
reliably open the generated absolute icon path when the repository path
contains Cyrillic characters. Build through an ASCII-only directory junction;
do not copy or relocate the working tree just for the build:

```powershell
$source = 'C:\path\containing-Cyrillic\ei-mapper-endurance'
$link = 'C:\build-links\ei-mapper-endurance'
New-Item -ItemType Junction -Path $link -Target $source
Set-Location "$link\build-release"
qmake ..\ei_maper.pro CONFIG+=release
mingw32-make -o Makefile.Release -f Makefile.Release release/EIMapperEndurance.exe -j4
```

The `-o Makefile.Release` option is intentional for junction builds. It keeps
MinGW Make from repeatedly regenerating the makefile after qmake canonicalizes
the junction target. If a build command is interrupted, verify that no orphaned
`mingw32-make` or `qmake` processes remain before retrying. The junction and
shadow-build directory are disposable; tracked source remains in the worktree.

For unattended build verification, use `--ui-widget-check`, the headless MOB/MPR
commands below, or the non-dialog `--version` option. Validation must not launch
the editor without an explicit file or leave About/version windows open.

Before creating a distribution archive, run:

```powershell
.\packaging\validate_distribution.ps1 <staged-package-directory>
```

The staged package must not contain `endurance-options.json`, upstream
`options.json`, workflow logs, or crash dumps. A fresh extracted package must
therefore enter first-run game/mod setup.

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
