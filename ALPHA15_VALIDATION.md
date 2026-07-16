# EI Mapper Endurance 0.1.0-alpha.15 validation

## Windows 7 MPR crash diagnostic release

This build follows up an alpha.14 access violation reported while opening an
MPR on Windows 7. The single alpha.14 crash record is not sufficient to assign
a root cause: it incorrectly calculated every module offset from the editor
executable base, even when the fault address belonged to another module.

Alpha.15 adds:

- Correct fault-module name, base, and module-relative offset reporting.
- Access-violation operation and target-address reporting.
- x86/x64 register capture and a normal minidump under `logs/`.
- MPR archive, header, terrain-atlas, sector, and OpenGL-upload crash contexts.
- OpenGL vendor, renderer, version, and Qt backend logging.
- An ANGLE compatibility launcher for controlled comparison with old graphics
  drivers. The package must include `libEGL.dll` and `libGLESv2.dll`.

## Windows 7 retest gate

1. Use the same copied MPR that reproduced the alpha.14 crash.
2. Run normally once and retain `workflow.log` plus any `crash-*.dmp`.
3. If it crashes, restart through the ANGLE compatibility launcher and repeat.
4. Report Windows 7 edition/SP level, 64-bit status, GPU, driver version, map
   filename/hash, last successful log phase, and whether ANGLE changes the
   result.

Do not infer a graphics-driver cause unless the new module/context evidence or
the controlled backend comparison supports it.

## Local validation

- Windows x64 Qt 5.12.12/MinGW release build: passed.
- Separate-debug-info build for minidump symbolication: passed.
- Document, backup, resource-profile, resource-name, duplicate-ID, and runtime
  provider suites: 44 passed, 0 failed.
- Read-only byte-identical round trips: Base `zone1.mpr` and JMV `zone1.mpr`
  passed.
- Actual Windows 7 reproduction: pending tester hardware.
