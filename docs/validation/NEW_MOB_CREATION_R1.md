# New MOB creation R1

## Contract

- **File > New MOB...** creates either a primary `SC_OBJECT_DB_FILE` MOB or a
  supplemental / AutoZone `PR_OBJECT_DB_FILE` MOB.
- The user supplies or explicitly copies the active ID range. Primary world
  settings are visible inputs rather than silently invented metadata.
- Normal script, diplomacy, ranges, and primary world settings can be copied
  independently from a loaded MOB.
- Objects, embedded `AI_GRAPH`, `VSS_SECTION`, and editor directory metadata
  are never copied. New MOBs omit VSS entirely; they do not write an empty or
  synthetic VSS stub.
- Generated MOB publication is separate from changed-MOB serialization. The
  candidate is staged, freshly reopened, backed up when replacing a file,
  race-checked, atomically committed, and reopened again before success.
- A currently loaded destination cannot be replaced by this flow.

## Automated evidence

- The generated-MOB commit regression covers new-file creation, exact canonical
  reopen, supplemental role and range identity, zero objects, absent embedded
  graph, absent VSS, replacement backup, simulated atomic failure, and
  unchanged destination bytes after failure.
- Full MinGW release build and `--ui-widget-check` pass.

## Focused manual test

1. Create a supplemental MOB with a unique ID range and no copy source. Confirm
   it opens as the active MOB and MOB Parameters identifies it as supplemental.
2. Create a primary MOB, enter recognizable world settings, and enable the
   neutral diplomacy table. Reopen it and verify the settings.
3. With an existing MOB loaded, create a new supplemental MOB while explicitly
   copying its normal script and ranges. Confirm the new MOB contains no
   objects and no embedded graph.
4. Attempt to replace a currently loaded destination. Confirm creation is
   refused without modifying the file.
5. Replace an unloaded disposable MOB. Confirm a backup is recorded and the
   newly created file opens.

## Remaining closure caveat

The AI-graph bake/export implementation remains automated-test clean, but its
external `.grp` Test Current staging matrix is still awaiting the planned
manual runtime check. It must not be described as runtime-approved until that
test passes.
