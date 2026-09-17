# Local tester-build handoff contract

This is the Ataman local build-and-handoff contract for main and all worktrees.
Executable names and version remain Endurance until a separate branding change.
Builds are local deliverables, never an implicit upload or publication request.

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

## 2. Stage one runnable tester folder

Create a new, previously nonexistent owned staging folder:

```text
artifacts/EI-Mapper-Endurance-<version>-<purpose>-test/
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

## 3. Reuse versus create

Reusing a folder is allowed only for the agent's own never-delivered staging
area, after verifying it has no user-generated data. For an existing user build,
make a separate revision/commit-named package, even for a small fix. Do not
replace or delete prior builds/settings without a separate owner instruction.

Create a new folder when:

- the Endurance version changes;
- the test scope is materially different;
- the user needs an A/B comparison;
- the previous folder has already been shared and must remain reproducible.

Do not silently mutate a build that has already been distributed to other
testers. Use a new version or explicit revision suffix for that case.

## 4. Changelist and test notes

The package's one current changelist should state:

1. What changed, in user-visible terms.
2. What was deliberately not changed when the distinction matters.
3. Short numbered manual test steps for behavior that automation cannot prove.
4. Known limitations or expected warnings relevant to the test.

Do not turn routine tests into a long engineering report. Link deeper validation
documents only when the tester needs them.

The current packaging scripts are not a complete quality gate: at version
0.5.0-alpha their changelist check is not enforced, and the distribution validator
does not compare manifest digests to actual file hashes. Until separately fixed,
explicitly verify exactly one current changelist, every manifest entry/hash,
coverage of all packaged files, and equality of fresh/packaged executable hashes.
Package completeness is distinct from functional test success.

## 5. Handoff format

The final response uses this order:

1. Outcome and version.
2. Clickable link to the unpacked tester folder.
3. Concise changelist.
4. Focused test steps, when needed.
5. Automated checks and commit hash.
6. Known limitations, only if relevant.

Never provide only a raw executable/build directory, never lead with a ZIP, and
never launch the editor in place of the tester.
