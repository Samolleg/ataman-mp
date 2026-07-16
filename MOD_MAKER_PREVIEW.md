# EI Mapper: Endurance Edition mod-maker preview

This alpha is intended for collaborative bug-finding and disposable edit tests.
It is not yet approved for editing the only copy of a production map.

## First launch

1. Extract the complete ZIP to a writable folder.
2. Run `Start EI Mapper Endurance.cmd` so settings, logs, and support files use
   that folder.
3. In **Game and Mod Setup**, choose the Evil Islands installation folder—the
   folder containing `game.exe`.
4. Endurance automatically selects the JMV preset when Jamais Vu is installed,
   discovers its resource and map directories, and previews the result.
5. Confirm that the JMV resource directory appears above the base-game `Res`
   directory, then press **OK**. No individual archive selection is required.

Use **Settings > Game and Mod Setup...** to change the installation or profile
later. Other mods can add JSON presets in the `resource-profiles` directory by
following its README and example.

The first Open dialog starts in the preset's preferred map directory. For JMV,
that is the Jamais Vu `Maps` directory; for the base profile, it is the game's
root `Maps` directory. Legacy per-resource folder settings remain available as
an explicitly labeled advanced option and never open automatically.

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

For alpha.13, please test both load orders after a fresh start: MPR then MOB,
and MOB then MPR. Confirm that MOB-only camera movement works immediately and
that attaching/detaching terrain updates display positions without changing the
stored MOB coordinates.
