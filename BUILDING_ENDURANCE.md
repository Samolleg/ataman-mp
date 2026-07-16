# Building EI Mapper: Endurance Edition

## Supported baseline

- Windows x64
- Qt 5.15.x with the MinGW x64 toolchain
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

## Unit tests

```powershell
Set-Location tests
qmake endurance_document_tests.pro CONFIG+=release
mingw32-make -j4
.\release\endurance_document_tests.exe

qmake resource_name_tests.pro CONFIG+=release
mingw32-make -j4
.\release\resource_name_tests.exe

qmake duplicate_id_report_tests.pro CONFIG+=release
mingw32-make -j4
.\release\duplicate_id_report_tests.exe
```

## Private JMV validation

Do not copy JMV maps into the repository. Run the built validator directly
against the live files in read-only mode, or preferably against hash-verified
copies:

```powershell
EIMapperEndurance.exe --validate <map.mob>
EIMapperEndurance.exe --roundtrip-check <map.mob> --json
EIMapperEndurance.exe --roundtrip-check <map.mpr> --json
```

`--roundtrip-check` never writes the input file.
