# EI Mapper Endurance 0.1.0-alpha.14 validation

Validation date: 2026-07-16

## Testing-group scope

This is a private collaborative bug-finding build for JMV map makers. It is
appropriate for disposable edit tests, not the only copy of a production map.
Settings, logs, backups, source, and executable identity remain separate from
upstream EI Mapper.

## Included corrections

- Game and Mod Setup is under Settings and suppresses the automatic legacy
  resource dialogs.
- Setup refreshes the viewport and resource state without requiring a restart.
- The first Open dialog uses the selected profile's preferred Maps directory.
- MOB-only camera movement works immediately, and attaching/detaching terrain
  reprojects display positions without changing stored MOB coordinates.
- Unit appearance lookup records prototype, model, database archive, candidate
  texture, and texture archive provenance in the workflow log.
- The active mod `database.res` takes precedence over adjacent backup or
  original/unmodified databases for unit appearance lookup.

## Automated and read-only validation

- Complete Windows x64 Qt 5.12.12/MinGW release and symbol builds: passed.
- Document, backup, resource-profile, resource-name, and duplicate-ID suites:
  35 passed, 0 failed.
- Base and JMV `units.udb` parsing checks: passed without modifying either
  installation.
- JMV profile discovery found both map and resource directories in mod-first
  order and indexed 91 RES archives.
- Final ZIP extraction and SHA-256 manifest verification are required before
  handoff.

## Known issues and limits

- Base-game `zone3obr.mob` humans and orcs still do not render their correct
  textures. Candidate skin-index lookup alone is insufficient; this is deferred
  to full database/template-driven character visuals. JMV is the priority.
- Humanoid equipment is not yet assembled from database armor/weapon fields.
- Saving is guarded by snapshots, pre-save backups, staging, fresh-parser
  validation, and atomic replacement, but unknown-record preservation is not
  proven across every map family.
- The editor is not approved for modifying the only copy of a production map.

## Manual group test gate

- Test only copied maps and record their source SHA-256.
- Exercise MPR-first and MOB-first load order after a fresh start.
- Reopen every changed save in Endurance and MobExplorer before game testing.
- Use `drawall`, then unload/reload and exit gracefully.
- Report successful tests as well as failures using `GROUP_TEST_MATRIX.md` and
  `BUG_REPORTING.md`.
