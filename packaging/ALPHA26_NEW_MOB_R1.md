# New MOB creation R1

## What changed

- **File > New MOB...** now creates an empty primary map MOB or supplemental /
  AutoZone MOB.
- The dialog requires an explicit object-ID range and exposes primary world
  settings instead of silently inventing map metadata.
- A loaded MOB can supply selected ranges, normal script, diplomacy, or primary
  world settings. Objects, embedded AI graph, VSS, and editor-only directory
  data are never copied.
- Generated files use staged validation, fresh reopen checks, backup on
  replacement, race detection, atomic commit, and post-commit validation.

## Focused test

1. Use **File > New MOB...** to create a supplemental MOB with a unique ID
   range and no copy source. Confirm it opens and MOB Parameters reports the
   supplemental role.
2. Create a primary MOB with recognizable world values and the optional neutral
   diplomacy table. Close and reopen it, then verify the values.
3. With an existing MOB loaded, create another MOB while explicitly copying its
   normal script and ranges. Confirm no objects or AI graph were copied.
4. Confirm replacing a currently loaded MOB is refused. Replacing an unloaded,
   disposable MOB should succeed after confirmation and create a backup.

## Known limitation

External `.grp` selection and Test Current staging are implemented but remain
pending the planned game-side runtime matrix. They are not yet runtime-approved.
