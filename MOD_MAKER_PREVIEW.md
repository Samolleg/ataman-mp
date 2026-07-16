# EI Mapper: Endurance Edition mod-maker preview

This alpha is intended for collaborative bug-finding and disposable edit tests.
It is not yet approved for editing the only copy of a production map.

## First launch

1. Extract the complete ZIP to a writable folder.
2. Run `Start EI Mapper Endurance.cmd` so settings, logs, and support files use
   that folder.
3. When prompted, open Resource Settings.
4. Add one `.res` archive from the mod resource directory and one from the base
   game `Res` directory. Endurance indexes every `.res` archive beside each
   selected file.
5. Keep the mod source above the base-game source. Higher sources override lower
   sources. The legacy Figure/Texture labels both feed the same generic catalog.

Do not copy another tester's `endurance-options.json`: it contains paths specific
to their machine. Endurance does not read or overwrite upstream EI Mapper's
`options.json`.

## Safe test contract

- Copy each `.mob` or `.mpr` to a separate test directory before opening it.
- Record the source SHA-256 before editing.
- Never use a live map as the only rollback copy.
- Save, close, reopen, and inspect the generated file before putting it in a test mod.
- Test MOB changes with `drawall`, then map unload/reload and graceful game exit.
- Keep generated backups until the test has been accepted or rejected.
- Do not redistribute JMV/game maps, RES archives, crash dumps, or extracted assets.

Endurance writes a temporary staged file, reopens it with a fresh validator,
checks its MOB/MPR container structure and exact staged bytes, creates a pre-save
backup when replacing a file, and then uses atomic replacement. This reduces the
risk of truncated or malformed output, but it does not yet prove preservation of
every unknown record in every map variant.

## Reporting results

Use `Tools > Open Logs Folder` and follow `BUG_REPORTING.md`. Report successful
edit tests too: knowing which map families and edit types work is part of the
validation corpus.
