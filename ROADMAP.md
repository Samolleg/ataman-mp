# EI Mapper: Endurance Edition roadmap

This is the living project checklist for the private Endurance Edition line.
An item is checked only after its implementation has landed and received the
listed level of validation. New findings and wishlist requests belong here so
they do not get lost between test sessions.

Current development release: `0.1.0-alpha.14`.

## Current focus

- [ ] Collect structured alpha.14 testing-group results, including successful
  tests, workflow logs, source hashes, load order, MobExplorer comparisons, and
  focused in-game results.
- [ ] Expand changed-save regression coverage across the private JMV map corpus.
- [ ] Finish lossless preservation of unknown MOB records and MPR archive details.

These are the remaining safety gates before editing live maps can be recommended.

## Alpha.14 testing campaign

- [x] Build and checksum-verify the private alpha.14 testing-group package.
- [x] Include source, symbols, GPL notices, validation notes, safe-test rules,
  bug-report instructions, and a shared test matrix.
- [x] Record base-game `zone3obr.mob` humanoid textures as a known unresolved
  issue instead of treating candidate-name lookup as a rendering fix.
- [ ] Collect coverage for MPR-first, MOB-first, MOB-only, terrain
  attach/detach, repeated open/close, and cold/warm loading.
- [ ] Collect narrow changed-save tests for properties, copied objects, patrol
  points, scripts, and terrain, followed by Endurance reopen, MobExplorer
  comparison, `drawall`, unload/reload, and graceful exit.
- [ ] Triage every crash against its matching rotated workflow log and exact
  reproduction steps; do not infer one cause from a shared crash address.
- [ ] Convert confirmed regressions into minimized synthetic fixtures where the
  format permits it without distributing game or JMV assets.
- [ ] Publish the next private alpha only after fixes and package validation;
  keep alpha.14 immutable once it has been handed to testers.

## Foundation and identity

- [x] Create an independent private repository with complete upstream history.
- [x] Keep the public project as the read-only `upstream` remote.
- [x] Tag the imported upstream baseline at commit `1cebe83d`.
- [x] Brand the product and executable as EI Mapper: Endurance Edition.
- [x] Start independent SemVer at `0.1.0` and separate Endurance settings/log paths.
- [x] Preserve GPL-3.0, upstream attribution, history, and `NOTICE.md`.
- [x] Keep game assets, private paths, dumps, and integration fixtures out of Git.

## Stability and safe saving

- [x] Stop automatic duplicate-ID repair while opening a MOB.
- [x] Report duplicate IDs without changing the source or dirty state.
- [x] Preserve exact bytes for unchanged MOB/MPR Save As operations.
- [x] Use `QSaveFile` for atomic replacement.
- [x] Fix sunlight/ambient serialization confusion.
- [x] Preserve unit prototype trailing bytes required by existing maps.
- [x] Keep patrol/look coordinates and unit height in stored map coordinates.
- [x] Correct SOUND field ordering, wind float preservation, and optional VSS handling.
- [x] Add deduplicated open snapshots and timestamped pre-save backups. ([alpha.7](https://github.com/Ilufus/ei-mapper-endurance/commit/8a7bc3c))
- [x] Refuse replacement when the required pre-save backup fails.
- [x] Add a rotating backup retention policy and **Tools > Open Backup Folder**.
- [x] Verify backup creation, metadata, hashes, and source preservation in a disposable end-to-end test.
- [x] Validate the staged document with a fresh parser before committing it.
- [ ] Preserve every unknown MOB record, nested length, ordering detail, and raw payload.
- [ ] Complete corpus tests for every observed object family and optional section.
- [ ] Complete lossless MPR preservation for names, timestamps, alignment, and extra entries.
- [ ] Verify CP1251 script byte lengths and round trips across the corpus.
- [ ] Add an explicit duplicate-ID Repair preview with reference and script auditing.
- [ ] Add in-app backup browsing and one-click restore to a user-selected destination.
- [ ] Add integrated failure-injection tests for backup, staging, validation, and replacement failures.
- [ ] Stress repeated open/close/save/cancel and abnormal-exit behavior on large maps.
- [ ] Complete the game test matrix: `drawall`, save/load, unload/reload, scripts, traversal, and exit.

## Diagnostics and crash recovery

- [x] Put workflow logs in a dedicated Endurance logs folder.
- [x] Start a clean current log and timestamp the previous log on launch.
- [x] Record file-open phases and last-operation breadcrumbs for hard-crash investigation.
- [x] Add crash handling that preserves useful diagnostic context where Windows permits it.
- [ ] Add a diagnostics panel for phase timings, validation findings, and missing resources.
- [ ] Add an opt-in crash bundle creator that removes private paths and game assets.
- [ ] Exercise hard-crash logging with controlled parser, decoder, and OpenGL failures.

## Resources, figures, textures, and performance

- [x] Index RES tables and lazily read payloads instead of expanding whole archives repeatedly.
- [x] Normalize mixed-case archive lookups and cache missing-resource results.
- [x] Treat configured RES archives as generic sources with deterministic priority.
- [x] Discover JMV archives beyond the old figure/texture category restrictions.
- [x] Resolve the previously missing representative Zone 3/18 JMV figures.
- [x] Preserve unit figure, texture, and database fields during changed saves.
- [x] Resolve model-qualified `_xN` unit skins such as Zone 12 skeleton textures.
- [x] Parse human and orc prototype skin indices from the active `units.udb`
  and produce model-qualified candidate texture names.
- [ ] Complete database/template-driven character rendering, including race
  texture lists, secondary layers, and equipped visuals. Base-game
  `zone3obr.mob` remains the known primary-skin regression case; JMV rendering
  remains the current priority.
- [x] Prefer an active mod `database.res` over adjacent backup or
  `original-unmodified` database archives for unit appearance lookup.
- [x] Log and display the nearest existing family variation when a MOB references an absent skin, without changing the stored field.
- [x] Resolve complete terrain-atlas sets case-insensitively with deterministic JMV-first provenance.
- [x] Refuse partial terrain-atlas assembly and use a complete diagnostic fallback instead.
- [x] Use the embedded MPR map namespace for Tile Parameters, adapting upstream PR #19.
- [ ] Persist catalog indexes and invalidate them by path, size, timestamp, and header hash.
- [ ] Inventory and support every MMP format found in the private JMV corpus.
- [ ] Show diagnostic placeholders with asset, archive, format, and failure reason.
- [ ] Move indexing, parsing, and CPU decoding to cancellable worker tasks.
- [ ] Batch OpenGL uploads on the GUI thread without freezing the editor.
- [ ] Report archive precedence, collisions, and shadowed assets.
- [ ] Meet the warm-load target: at least 10x faster than v0.3 and within 15 seconds on representative maps.

## Document workflow

- [x] Provide separate Open MPR and Open MOB commands.
- [x] Allow basic MOB-only opening and rendering without automatically loading terrain.
- [x] Keep an MPR open while independently choosing a MOB.
- [ ] Introduce the full `MapWorkspace` model: optional MPR, multiple MOBs, explicit active document.
- [ ] Add Close MPR and Close Active MOB commands.
- [ ] Add an explicit Open Matching MOB convenience command.
- [x] Reproject display positions safely when terrain is attached or detached
  without changing stored MOB coordinates.
- [ ] Disable only terrain-dependent tools in MOB-only mode.
- [ ] Add independent `.mpr` and `.mob` drag-and-drop behavior.
- [x] Add headless `--profile-load` discovery and JSON output.
- [ ] Add Open Workspace support.

## UI and script workflow

- [x] Make the object outliner resizable and persist its splitter layout.
- [x] Enable the external-script-editor button with OS association fallback.
- [ ] Convert the outliner to a resizable/detachable dock.
- [ ] Add stable filtering by text, type, ID, model, database name, and validation state.
- [ ] Add a configurable external editor executable.
- [ ] Export embedded CP1251 scripts to managed UTF-8 workspaces.
- [ ] Watch external edits, display a diff, and require explicit import.
- [ ] Make script import one undoable edit and reject non-CP1251 characters clearly.
- [ ] Recover unimported script workspaces after an abnormal exit.

## JMV setup and validation

- [x] Configure and test the development build against a confirmed JMV installation using settings outside upstream v0.3.
- [x] Keep all live maps untouched during automated tests; use hashed disposable copies.
- [x] Provide `--validate`, `--roundtrip-check`, and JSON output for automation.
- [x] Add the JMV setup wizard with auto-discovery and explicit confirmation.
- [x] Move Game and Mod Setup under Settings and suppress automatic legacy
  resource dialogs and warnings.
- [x] Use the selected profile's preferred Maps directory for Open dialogs.
- [x] Refresh resource state and viewport geometry after setup without requiring
  an editor restart.
- [ ] Validate archive signatures and show source precedence during setup.
- [ ] Offer a read-only, previewed import of compatible upstream `options.json` values.
- [ ] Maintain a hash inventory for the private Zone 1/3/4/6/12/18/19 integration corpus.
- [ ] Automate semantic comparison with MobExplorer where its interfaces permit it.
- [ ] Compare ID-preserving and explicit-repair builds before linking ID changes to earlier game crashes.

## Release gates

- [x] `0.1.0-alpha.1` through `0.1.0-alpha.13`: initial safety, diagnostics, resources, serializer fixes, backups, model-qualified skins, terrain-atlas correctness, consolidated duplicate-ID reporting, staged-save validation, mod-maker preview reporting, installation-based reusable resource profiles, and corrected first-run/MOB-only viewport initialization.
- [x] `0.1.0-alpha.14`: testing-group build with Settings-based setup,
  explicit base-game humanoid known issue, database appearance diagnostics, and
  a coordinated test/report matrix.
- [ ] Next private alpha: address confirmed alpha.14 regressions and publish a
  coverage summary from the group test matrix.
- [ ] `0.1.0-beta.1`: correct indexed resources and responsive fast loading.
- [ ] `0.1.0-rc.1`: independent workspace workflow, UI, script editing, and completed safety validation.
- [ ] `1.0.0`: JMV-ready release passing the full corpus, MobExplorer, and in-game validation matrix.
- [ ] Package releases with executable, Qt runtime, source snapshot, GPL notices, build instructions, checksums, and rollback guidance.
- [ ] Consider Qt6/CMake modernization only after 1.0; use a major version if compatibility breaks.

## Wishlist inbox

Add requested enhancements here first. We can then assign each one to a release
or move it into the relevant section above without losing its history.

### High priority

- [ ] Replace the secondary-MOB workflow with Unreal-style multi-MOB loading and editing.
  - Show all open MOBs in a **Loaded MOBs** tab/list.
  - Allow individual MOBs to be locked against selection and accidental edits.
  - Make the currently selected/active MOB explicit; new objects and pasted objects go there.
  - Show which loaded MOB owns the currently selected object.
  - Treat MOBs similarly to Unreal Engine sublevels within the loaded map workspace.
- [ ] Allow unit texture fields to support both database-backed dropdown selection and manual keyboard entry.
- [ ] Add periodic autosave with recoverable, versioned backup states.
- [ ] Add a command to launch the currently loaded MPR and MOB set directly in Evil Islands for testing.
- [ ] Add a resizable workflow-log panel along the bottom of the viewport, similar to the UE4 output log.

### Medium priority

- [ ] Load database-defined humanoid textures for humans, skeletons, and orcs where applicable.
  - Prototype skin-index parsing and candidate lookup are implemented.
  - JMV remains the working visual baseline; base-game `zone3obr.mob` is still
    incorrect and must be retested against the full database/template pipeline.
  - Keep this open until visual corpus testing covers primary and secondary
    layers for all supported humanoid models.
- [ ] Render every applicable humanoid with the weapons and armor assigned by
  its database setup, including humans, orcs, undead, and any other humanoid
  race that equips items in game.
  - Core scope: reproduce the normal equipped armor and weapon visuals defined
    by each supported database/template entry.
  - **Stretch goal (JMV only):** correctly assemble one-handed weapon plus
    shield, dual-wield, and two-handed configurations.

### Low priority

- [ ] Add configurable UI font-size scaling for 4K displays and accessibility.
- [ ] Add a Russian translation and an in-app language switch.

## Updating this checklist

Each completed code item should normally include a commit, test, or release link.
If testing exposes a regression, uncheck the item and add the concrete failing
case beneath it. Changes to this file are committed to `main`, so GitHub keeps a
complete history of what was added, completed, reopened, or reprioritized.
