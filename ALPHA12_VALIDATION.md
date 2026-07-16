# EI Mapper Endurance 0.1.0-alpha.12 validation

Validation date: 2026-07-16

## Installation and profile setup

- The first-run workflow asks for the Evil Islands installation directory, not
  individual RES archives.
- Known installations are discovered from mounted-drive candidate locations.
- The built-in JMV profile recognizes `Starter/MODS/JamaisVu135` and compatible
  immediate children of `Starter/MODS` using required files, directories, and
  signature scoring.
- Resource directories are stored in explicit priority order and every `.res`
  archive directly inside them is indexed.
- Base-game-only setup remains available.
- External schema-v1 JSON profiles are loaded from `resource-profiles` beside
  the executable; the package includes documentation and an inactive example.

## Automated validation

- Resource-profile tests: 6 passed, 0 failed, covering JMV discovery and
  priority, base-only setup, invalid roots, and external profiles.
- The complete Windows x64 Qt 5.12.12/MinGW application build succeeded.
- Document, backup, resource-name, duplicate-ID, and resource-profile suites:
  32 passed, 0 failed.

## Read-only real-installation check

`--profile-load` was run against the current JMV installation without modifying
it. It selected `jamais-vu`, found the JMV and base map directories, placed the
JMV resource directory above the base-game resource directory, and indexed 91
RES archives. The command returned exit code 0 with no validation messages.

No installation path or generated settings file is included in the tester
package.
