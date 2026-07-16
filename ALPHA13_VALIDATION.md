# EI Mapper Endurance 0.1.0-alpha.13 validation

Validation date: 2026-07-16

## First-run and viewport corrections

- Game and Mod Setup now completes before the OpenGL viewport is constructed.
- Game and Mod Setup is available under Settings alongside the advanced legacy
  resource controls; diagnostic and support actions remain under Tools.
- Automatic legacy resource warnings and dialogs are suppressed. The legacy
  resource-folder UI remains available as an explicitly labeled advanced action.
- A first-run workspace receives a usable viewport/outliner split instead of
  retaining a cramped partially initialized state.
- Applying setup while the editor is open refreshes indexed names, negative
  caches, viewport geometry, and rendering without requiring a restart.
- The first Open dialog uses the profile's preferred Maps directory.
- The redraw timer starts for MOB-only sessions, not only after the first MPR.
- Loaded MOB objects and unit logic display geometry are reprojected when terrain
  is attached or detached; stored object coordinates are not modified.

## Automated validation

- Complete Windows x64 Qt 5.12.12/MinGW application build: passed.
- Document, backup, resource-profile, resource-name, and duplicate-ID suites:
  32 passed, 0 failed.
- Resource-profile tests assert JMV Maps-before-base-Maps ordering and base Maps
  selection.

## Read-only real-installation check

`--profile-load` was run against the current JMV installation without modifying
it. It selected `jamais-vu`, found both map directories in the correct priority,
found both resource directories in the correct priority, indexed 91 RES archives,
and returned exit code 0 without validation messages.

## Manual test gate before publishing

- Fresh portable start shows only Endurance Game and Mod Setup.
- Setup leaves a correctly sized viewport and an MPR renders without restart.
- Open starts in the JMV Maps directory.
- MOB-first mode supports camera movement immediately.
- Attaching the matching MPR corrects display projection without reloading MOB.
- Detaching terrain restores standalone display projection.

This candidate must not be published until the manual test gate is confirmed.
