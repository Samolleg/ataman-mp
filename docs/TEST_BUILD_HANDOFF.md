# Local tester-build handoff contract

This is the Ataman local build-and-handoff contract for main and all worktrees.
Executable names remain Endurance until a separate branding change.
Ataman versions are integers starting at 1, stored in root VERSION; each separate
fix or feature increments the number under AGENTS.md. Historical Endurance
versions remain historical evidence only.
Builds are local deliverables, never an implicit upload or publication request.

## Another contributor's computer

Read [COAUTHOR_HANDOFF.md](COAUTHOR_HANDOFF.md) before the first build from a
clean GitHub clone. Paths are relative to that contributor's actual checkout;
historical absolute paths refer to the report author's machine. The repository
does not include Qt/MinGW, Visual Studio, installed game data or old runtime
binaries. Obtain dependencies under the current task's authorization and build
the runtime components from source. Do not recover private game fixtures from
old history or add them to Git/distributions. Required packaged test data must
be owned distributable synthetic data; an unavailable private-data check is
recorded as unavailable, never PASS. All staging, manifest and permanent-target
preservation rules below still apply. Another permanent target needs an explicit
request; a build never authorizes touching someone else's installation.

## 1. Build the application

Use the documented release wrapper from the active worktree:

```powershell
.\scripts\build_release.cmd `
  -QtBin <Qt-MinGW-bin> `
  -MinGwBin <matching-MinGW-bin> `
  -BuildDirectory build-release-<version>-<purpose>
```

The raw shadow-build directory is compiler output. It is never the folder
given to a tester.

Run proportionate automated validation before handoff. This normally includes
the release build and `--ui-widget-check`, plus focused document, renderer,
runtime, or packaging checks for the changed area. Headless commands may run
automatically. Do not open the packaged GUI for the user or begin their manual
test on their behalf.

Report every failure, timeout and skipped check. A historical exact-commit
rebuild may be delivered as an explicitly unvalidated test artifact with these
limits, not as a verified release. For a new fix/feature, failure of its original
reproduction or relevant regression blocks declaring the task complete. Do not
change an explicitly requested historical commit to make checks pass.

## 2. Stage a temporary runnable package

Create a new, previously nonexistent owned temporary staging folder; the user-facing target stays fixed:

```text
build-staging-<version>-<purpose>/
```

The folder must be directly runnable and contain the editor executable, its Qt
runtime, current Endurance runtime components, required data/test fixtures,
licenses/notices, `VERSION`, checksums, and exactly one current changelist.
For the retained MinGW kit, stage with `windeployqt --compiler-runtime` while
the matching MinGW `bin` directory is on `PATH`, or copy its
`libgcc_s_seh-1.dll`, `libstdc++-6.dll`, and `libwinpthread-1.dll` explicitly.
The distribution validator requires all three compiler-runtime DLLs.

Do not include:

- `.o` files, generated Makefiles, qmake intermediates, or build-system debris;
- `endurance-options.json`, upstream `options.json`, or developer paths;
- workflow/runtime logs, crash dumps, saves, or autosave data;
- unrelated source artifacts or historical alpha changelists;
- a ZIP unless the user explicitly asks for one.

Always finish with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\packaging\finalize_package.ps1 `
  <tester-folder>
```

Before finalization, resolve the exact absolute target and verify it is that
new owned staging folder, not the repository root, a game installation or a
folder already handed to a user. The finalizer deletes settings, logs, saves
and caches. Never point it at a user-run or existing distributed folder.

The finalizer must pass. Do not run the finalized package afterwards: that
would recreate user settings/logs and invalidate its clean first-run state.

## 3. Update one permanent installation

Owner decision, 2026-09-17: each requested build updates the same program folder.
The permanent target is `artifacts/EI-Mapper-Ataman-2-first-test` relative to the
Ataman repository root. Its original name is a stable path, not a version claim.
Use VERSION and the executable's version as the installed version. Do not create
a new user-facing installation for every fix. Another target requires an owner
request. The build request already authorizes program replacement at this target.

The shadow-build and staging directories are temporary engineering outputs;
section 2 and the finalizer apply only to a fresh owned staging package.
Never run the finalizer or clean-package deletion rules on the permanent target.

Update procedure:

1. Build, test and finalize staging; complete section 4's independent checks.
   Retain the previous installation if the build or package-integrity checks fail.
   Functional test failures remain explicit limitations under section 1; never
   label them PASS or silently claim a complete fix.
2. Resolve the exact permanent target and verify that no editor or runtime
   component from it is running. If it is running, ask the user to close it;
   do not kill a user's session or replace loaded binaries.
3. Use the old and new manifests to identify managed program files. Preserve all
   settings, logs, caches, maps, saves, resources added by the user and unlisted
   files. Do not mirror, recursively clear, or overwrite the entire directory.
   If a new managed path collides with an unlisted user file, or a managed file
   differs from its recorded old hash, retain it and resolve that conflict first.
4. Back up the exact files to be replaced or removed and the previous manifest
   into one owned rollback area outside the permanent folder. Verify the backup
   hashes before replacing anything. This is a technical backup, not another
   user-facing installation. Never overwrite the only rollback copy mid-update.
5. Copy verified program files, VERSION and current notes from staging. Remove
   obsolete managed program files only when their current hashes match the old
   manifest. Never include protected user data in this removal. Replace the
   manifest last. If any step fails, restore this update's changed files from
   the verified backup and remove only newly created files owned by this update;
   report whether rollback succeeded.
6. Verify every installed managed file against the new manifest, including exe
   and runtime hashes. User files outside that manifest are expected and must
   remain unchanged; the clean-package validator is not an installed-data cleaner.
   Report installed version and the same permanent folder link. Keep at most one
   completed rollback generation: after successful verification of the next
   update, remove only an older, positively identified owned rollback generation.

Older reports describe immutable per-version delivery as history. This owner
choice supersedes that workflow for the named Ataman installation only. Game
installations, Endurance checkouts and other users' packages remain untouched.

## 4. Changelist and test notes

The package's one current changelist should state:

1. What changed, in user-visible terms.
2. What was deliberately not changed when the distinction matters.
3. Short numbered manual test steps for behavior that automation cannot prove.
4. Known limitations or expected warnings relevant to the test.

Do not turn routine tests into a long engineering report. Link deeper validation
documents only when the tester needs them.

Since Ataman 21, validate_distribution.ps1 checks integer-version changelog
identity, manifest syntax, safe/unique paths, every SHA-256, missing files and
complete coverage. finalize_package.ps1 invokes this validator after generating
the manifest. The validator also rejects linked manifest paths.

The finalizer and validator must pass. Independently compare the fresh tested
executable and staged executable hashes and verify the staged --version result
before finalization. Record source SHA, absolute package path and verification
results in the handoff evidence. Do not modify or run the finalized package.
Package completeness remains distinct from functional test success: a valid
manifest cannot prove that the editor's behavior is correct. Historical reports
before version 21 retain their earlier independent-check requirements as evidence.

For each Ataman version use a fresh shadow-build directory: changing VERSION
changes a compiler definition, which incremental Make builds may fail to notice.
Check the freshly built executable with --version before staging and require its
reported version to match VERSION (the output also includes the application name).
Include one current CHANGELOG.md containing only that
version's entry and focused test notes, not the entire historical changelog.

## 5. Handoff format

The final response uses this order:

1. Outcome and version.
2. Clickable link to the permanent installation updated under section 3.
3. Concise changelist.
4. Focused test steps, when needed.
5. Automated checks and commit hash.
6. Known limitations, only if relevant.

Never provide only a raw executable/build directory, never lead with a ZIP, and
never launch the editor in place of the tester.
