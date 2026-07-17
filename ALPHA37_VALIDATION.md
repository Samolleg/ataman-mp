# Alpha.37 external-script critical hotfix validation

Alpha.37 supersedes alpha.36 for all testing. It prevents pending-marker watcher
feedback and defers the modal import question until Endurance owns application
focus.

1. Open a MOB and use **Ctrl+Alt+M** to open its managed script externally.
2. Change one obvious line and save while the external editor remains focused.
   Confirm Endurance remains responsive and no prompt is placed behind the
   external editor.
3. Return to Endurance. Confirm exactly one **External script changed** question
   appears in front of the Endurance window.
4. Accept it. Confirm exactly one **Import external MOB script** undo entry,
   normal viewport frame rate, and the expected dirty state.
5. Undo and redo once. Confirm the script alternates between the exact old and
   new text.
6. Save the external file repeatedly without changing its contents. Confirm no
   prompt and no dirty-state change.
7. Make a second distinct external change. Confirm a second single prompt and no
   performance regression.
8. Include `workflow.log` in any report. A real change should log one
   `Showing external script import prompt` followed by accepted import or no
   import; repeated directory notifications must not stall the UI.

Also repeat the alpha.36 script, property clipboard, and model-parts undo checks.
