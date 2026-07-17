# Alpha.39 external-script notification-storm validation

Alpha.39 supersedes alpha.38 for external-script testing. It removes persistent
directory watching and coalesces a burst of script-file notifications into one
deferred read and import question.

1. Start a clean editor session, open a MOB, then open its managed script in the
   same external editor that reproduced the alpha.38 first-save slowdown.
2. Make the first script change and save. Confirm Endurance remains responsive
   while the external editor writes the file.
3. Return to Endurance. Confirm exactly one import question appears and accept
   it. Confirm one undo entry and normal viewport frame rate.
4. Make and import a second distinct external change. Confirm identical behavior.
5. Save again without changing text. Confirm no import and no dirty-state change.
6. Inspect `workflow.log`. Each save burst may report one
   `Processing external script change` with a `coalescedNotifications` count,
   but it must not contain per-event file/directory notification floods.
7. Undo/redo both imported changes and verify exact script text.

Also repeat the alpha.38 MOB Parameters close checks.
