# EI Mapper Endurance contributor instructions

For every local tester build, package, or handoff, follow
`docs/TEST_BUILD_HANDOFF.md`. These rules apply equally to main and feature
worktrees.

- Deliver an unpacked, user-runnable tester folder, not raw compiler output.
- Do not launch the packaged editor interactively; the user runs it.
- Provide a clear changelist, useful test steps, and a clickable folder link.
- Do not create a ZIP unless explicitly requested.
- Prefer updating and re-finalizing the existing folder for a minor incremental
  fix to the same test target. Use a new folder for a new version, a materially
  different test scope, or to preserve an already-shared immutable build.
- Never ship settings, logs, dumps, saves, object files, generated Makefiles,
  stale changelists, or local/private paths.
