# Parallel Codex workers for EI Mapper Endurance

Use this workflow whenever the user explicitly asks for a separate task/thread
to implement Endurance work in parallel.

## Why `Not a git repository` happens

The common parent workspace is a container for several projects and worktrees;
it is not itself the Endurance Git repository. A projectless thread opened at
that parent cannot create a Git worktree. Calling a worktree fork from it can
start with:

```text
[info] Starting worktree creation
[stderr] Not a git repository
```

This is a thread-targeting error, not a damaged repository. Do not retry the
same projectless worktree operation.

## Correct creation workflow

1. Call `list_projects`.
2. Select the saved project whose repository root is the
   `ei-mapper-endurance` checkout. Do not select the parent workspace.
3. Call `create_thread` with:
   - `target.type: project`;
   - the returned Endurance `projectId`;
   - `target.environment.type: worktree`.
4. Choose the starting state deliberately:
   - Omit `startingState` for the saved project's default branch.
   - Use `startingState: { type: branch, branchName: main }` when the task must
     start from current committed local `main`, including commits not pushed to
     the remote.
   - Use `startingState: { type: working-tree }` only when the user explicitly
     wants current uncommitted changes included. Do not use it casually: it can
     blur ownership of unrelated dirty files.
   - `branchName` selects an existing ref. It does not name the new worker's
     branch.
5. Worktree creation is asynchronous. Keep the returned client/thread ID and
   wait for setup to complete before sending follow-up instructions or assuming
   a filesystem path exists.
6. Confirm the worker reports a Git branch/worktree and the intended starting
   commit before implementation begins.

Conceptual call shape:

```text
projects = list_projects()
endurance = project with root == <workspace>/ei-mapper-endurance
worker = create_thread(
  target = project(endurance.projectId, worktree, branch "main"),
  prompt = bounded task and handoff requirements
)
wait until worker/worktree setup completes
```

Do not manually invent a `projectId`, derive it from a path, or call a worktree
fork merely because the current thread can read repository files.

## Choosing isolation

- **Worktree project thread:** use for parallel source edits, builds, tests, or
  commits. It gets an isolated checkout and shares Git objects with the project.
- **Same-directory fork:** use only for read-only analysis or coordination. It
  shares the current working tree, so parallel edits can overwrite or stage one
  another's work.
- **Subagent:** use for a bounded subtask inside the current task when the user
  did not request a separately visible Codex thread. Subagents share the
  filesystem and therefore require the same edit-ownership discipline.

## Worker prompt requirements

State all of the following explicitly:

- concrete task and completion boundary;
- expected starting branch/commit when relevant;
- files or subsystems the worker owns;
- build/test evidence required;
- whether commits are required;
- no push or merge unless the user authorizes it;
- read access separately from mutation restrictions, following `AGENTS.md`;
- preserve unrelated dirty/user-owned files;
- follow `docs/TEST_BUILD_HANDOFF.md` for anything the user will run.

Do not use ambiguous wording such as "do not touch the corpus." Say that the
worker must not **modify** live files while read-only inspection is authorized.

## Handoff and integration

1. Require a clean, scoped commit and its full hash, or an explicit report that
   the task is analysis-only.
2. Require test/build evidence and known limitations.
3. Integrate conflict-aware into current local `main`; never reset main to the
   worker branch.
4. Preserve unrelated main changes and rerun proportionate integrated tests.
5. Push only when the user requests it.

## Recovery after a failed creation

1. Leave or archive the failed projectless thread; it has no usable worktree.
2. Call `list_projects` and verify the Endurance repository project exists.
3. Create a new project-scoped worktree thread with the workflow above.
4. If the Endurance project is absent, add/open the repository as a Codex
   project first. Do not work around the problem by treating the parent
   workspace as a repository.
