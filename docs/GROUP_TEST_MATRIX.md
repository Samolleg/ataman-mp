# Endurance alpha.14 group test matrix

Use one row per focused test. Successful rows matter: they define which map and
edit families have actually been exercised.

| Tester | Profile | Map + source SHA-256 | Load order | Intended operation | Endurance reopen | MobExplorer check | In-game result | Log/report |
|---|---|---|---|---|---|---|---|---|
| Example | JMV | `zoneX.mob` / `...` | MPR then MOB | Change one chest skill value | Pass | Only intended field changed | `drawall`, reload, exit pass | attached |

## Suggested assignments

- Opening/rendering: MPR-first, MOB-first, MOB-only, attach/detach terrain.
- Safe saving: no-op Save As, one property edit, one copied object, patrol points.
- Resources: figures, textures, terrain atlases, mixed-case names, JMV precedence.
- UI/workflow: setup changes, Open starting folder, outliner resizing, script editor.
- Performance/stability: cold/warm load time, cancel behavior, repeated open/close.

## Report contract

- Start from a disposable copy and record its hash before opening it.
- Change one independently describable thing per test where practical.
- Record exact editor version, profile, load order, expected result, and result.
- For changed saves, state the expected changed objects/fields and compare with
  MobExplorer before putting the file in a test mod.
- Attach the matching `logs/workflow.log` after reviewing it for private paths.
- Do not attach game/JMV maps, RES files, extracted assets, or crash dumps to an
  ordinary issue.

Known duplicate report to avoid: base-game `zone3obr.mob` human/orc textures are
not correct in alpha.14. Report only new behavior, a crash, or a JMV regression.
