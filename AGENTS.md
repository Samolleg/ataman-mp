# EI Mapper Endurance contributor instructions

For every local tester build, package, or handoff, follow
`docs/TEST_BUILD_HANDOFF.md`. These rules apply equally to main and feature
worktrees.

## EI research and delegation boundaries

- Read-only inspection of the user's installed games, mods, proprietary corpus,
  tools, binaries, data, and authorized private source is allowed for in-scope
  EI work. Prefer direct research over unsupported assumptions.
- "Do not touch" means "do not modify" and does not prohibit reading, hashing,
  parsing, comparing, disassembling, or researching unless the user explicitly
  forbids read access.
- Ask before copying installed game/mod files into a working area for editing;
  edit the copy. Live mutation requires explicit authorization and backups.
- Use `evil-islands-modding` for artifact safety and
  `evil-islands-private-engine-reference` for the exact private-source privacy,
  clean-room, and disclosure rules. Read-only internal source inspection is
  permitted, but private source must not be copied into this repository or any
  public/test artifact.
- Delegation prompts must state read permissions and write restrictions
  separately. Never use ambiguous standalone "do not touch" wording.
- If there is material ambiguity about intent, workflow, scope, evidence, or
  implementation choice, ask the user instead of guessing. Concisely identify
  the source of confusion and the viable options with their practical effects.
  First resolve anything answerable through safe read-only inspection of files,
  source, or documentation; ask only about the remaining user decision.

- Deliver an unpacked, user-runnable tester folder, not raw compiler output.
- Do not launch the packaged editor interactively; the user runs it.
- Provide a clear changelist, useful test steps, and a clickable folder link.
- Do not create a ZIP unless explicitly requested.
- Prefer updating and re-finalizing the existing folder for a minor incremental
  fix to the same test target. Use a new folder for a new version, a materially
  different test scope, or to preserve an already-shared immutable build.
- Never ship settings, logs, dumps, saves, object files, generated Makefiles,
  stale changelists, or local/private paths.
