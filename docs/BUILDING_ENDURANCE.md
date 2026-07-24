# Building EI Mapper: Endurance Edition

## Supported baseline

- Windows x64
- Qt 5.12 or newer with the MinGW x64 toolchain
- qmake
- C++11
- Visual Studio 2022 C++ tools with the x86 compiler (for the process-owned
  Test Current Map bridge and runtime host)

The application target is `EIMapperEndurance.exe`. The upstream v0.3 binary and
its `options.json` are not build inputs and are never overwritten.

## Application

Use the checked build wrapper from any PowerShell prompt:

```powershell
.\scripts\build_release.cmd `
  -QtBin C:\Qt\5.15.2\mingw81_64\bin `
  -MinGwBin C:\Qt\Tools\mingw810_64\bin `
  -BuildDirectory build-release
```

The paths can instead be stored in `EI_MAPPER_QT_BIN` and
`EI_MAPPER_MINGW_BIN`. The wrapper validates `qmake`, `uic`, and
`mingw32-make`, prepends both runtime directories to `PATH`, reruns qmake, and
builds into a separate shadow directory.

The wrapper also rebuilds both 32-bit runtime components on every release
build and places only their distributable `.plg`/`.exe` files under
`build-release\release\runtime`. This is intentional: copying an older binary
from `source\runtime\*\bin` can leave the editor executable and runtime protocol
out of sync even though the Qt application compiled successfully.

Qt 5.12 also cannot reliably generate Makefiles below a non-ASCII Windows path.
When necessary, the wrapper temporarily maps the repository to a free drive
letter with `subst`, builds through that ASCII path, and removes the mapping on
completion. This does not require elevation and does not persist across the
build.

This `PATH` setup is required even when `qmake.exe` is invoked by its absolute
path. Qt's `uic`, `moc`, and `rcc` tools are dynamically linked executables and
must find the Qt and MinGW runtime DLLs when make starts them. A failure such as
`uic` exit code `-1073741515` (`0xC0000135`) means that runtime setup was
skipped; it is a build-environment failure, not a source compile error.

The `.cmd` entry point uses a process-scoped execution-policy bypass only for
the checked-in PowerShell wrapper. It does not change the user's Windows or
PowerShell policy.

If using a preconfigured Qt MinGW command prompt, the arguments may be omitted:

```powershell
.\scripts\build_release.cmd -BuildDirectory build-release
```

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

## Known build failures

| Symptom | Meaning | Resolution |
|---|---|---|
| `uic`, `moc`, or `rcc` exits with `-1073741515` / `0xC0000135` | A Qt or MinGW runtime DLL is missing from the child-process `PATH`; this is not a source error. | Use `build_release.cmd` with the correct `-QtBin` and `-MinGwBin`, or set `EI_MAPPER_QT_BIN` and `EI_MAPPER_MINGW_BIN`. |
| PowerShell says that running scripts is disabled | The machine's execution policy blocks direct `.ps1` invocation. | Use `build_release.cmd`. Its bypass applies only to that child process and does not change system policy. |
| qmake displays the repository path as `??`, mojibake, or cannot write `Makefile.Release` | Qt 5.12 cannot reliably process the non-ASCII repository/build path. | Keep the shadow directory inside the repository and use the wrapper; it reuses or creates a temporary ASCII `subst` path and removes mappings it owns. |
| The wrapper cannot find `.git\HEAD` in a linked worktree | A Git worktree uses a UTF-8 `.git` pointer file instead of a `.git` directory. | Use the current wrapper. It resolves normal repositories and linked worktrees without assuming `.git\HEAD` exists. |
| `Test-Path` reports access denied for an unrelated mapped, network, or virtual drive | The drive is enumerated by PowerShell but is not readable while the wrapper searches for an existing ASCII repository mapping. | Current wrappers skip inaccessible drives automatically. If this appears in an older checkout, update the build scripts rather than granting the build access to unrelated drives. |
| qmake cannot rewrite a generated `.rc`, Makefile, or object file | A compiler process from an interrupted/timed-out build may still own the file. | Let `qmake`, `mingw32-make`, `uic`, and `moc` finish or exit, then rerun the wrapper. Do not delete a live shadow tree. |
| `VERSION` changed but `--version` still prints the previous value | qmake regenerated the Makefile, but Make does not track changes to compiler command-line definitions as source dependencies. | Build in a fresh shadow directory for every packaged version and verify the executable with `--version` before staging it. |
| qmake reports an unknown module or a compiler header/library is missing | The selected Qt kit and MinGW kit do not belong to a compatible installation. | Select the matching 64-bit Qt/MinGW pair; do not mix MSVC Qt libraries with MinGW. |
| The build finishes the Qt executable and then reports that the x86 runtime bridge/host failed | Visual Studio 2022 C++ x86 tools are unavailable, or a runtime component has a compiler error. A package made with an older bridge is not valid. | Install/repair the Visual Studio Desktop C++ workload, run from a normal prompt, and rebuild; never copy a stale runtime directory into the package. |
| `mingw32-make` prints a C++ filename, line number, and compiler diagnostic | This is a source compile failure rather than tool discovery. | Fix the first compiler error and rerun the same wrapper; qmake is regenerated automatically. |
| The editor builds but the package validator rejects the folder | Runtime bridge/host, notices, hashes, or required deployment files are missing, or private settings/logs leaked into the folder. | Stage a fresh folder, deploy the runtime components, then run `finalize_package.ps1`; never distribute a previously launched folder. |

Do not validate a build by launching `EIMapperEndurance.exe` without arguments:
that opens the GUI and may leave version/setup windows behind. Use `--version`,
`--ui-widget-check`, the focused tests, and the package validator.

## Unit tests

The build-wrapper identity regression creates an isolated temporary repository,
a linked worktree under a Unicode path, and an ASCII `subst` mapping. It removes
all three after the check:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests\build_release_worktree_tests.ps1
```

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
