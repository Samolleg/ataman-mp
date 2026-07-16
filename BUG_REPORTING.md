# Reporting Endurance bugs and test results

If you have access to the private repository, open an issue at:

https://github.com/Ilufus/ei-mapper-endurance/issues/new/choose

Otherwise send the same report and attachments to the person who shared the
preview with you.

## Include in every report

- Endurance version shown in the window title.
- Whether the result is a crash, visual/resource problem, save difference, UI
  problem, performance issue, or successful validation test.
- Exact reproduction steps starting from editor launch.
- Map filenames and SHA-256 hashes, but not the map files themselves.
- Whether the inputs were disposable copies and whether an MPR was already open.
- Expected and actual behavior.
- For saves: the precise objects/fields intentionally changed and the in-game
  test performed.
- `logs/workflow.log` from the affected session. If the editor was restarted,
  attach the matching timestamped `workflow-YYYYMMDD-HHMMSS.log` instead.

Use `Tools > Open Logs Folder` to find the logs. Each launch rotates the previous
session, so reproduce once, close the editor, and copy the matching log before
running unrelated tests.

## Privacy and copyrighted data

Workflow logs can contain Windows account paths, installation paths, map names,
timestamps, and resource names. Review them before sharing. Do not attach:

- `.mob` or `.mpr` files unless their author explicitly approves private transfer;
- RES archives or extracted game/mod assets;
- crash dumps to an ordinary issue—dumps can contain arbitrary process memory;
- another person's private installation paths or credentials.

If a map or dump is essential, coordinate a private transfer and provide its
SHA-256 in the issue so the artifact can be identified without reposting it.

## Useful focused tests

- Opening MPR and MOB independently and in both orders.
- No-op round trip with `--roundtrip-check`.
- One narrowly defined object/property change.
- Save, editor reopen, MobExplorer comparison, and in-game `drawall`.
- Map unload/reload and graceful game exit.
- Missing or incorrect figure/texture provenance.
- Cold and warm load time, object count, and whether the UI stayed responsive.
