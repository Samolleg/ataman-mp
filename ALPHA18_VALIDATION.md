# Alpha.18 selection and object-lifecycle validation

This test build adds selection changes to the main undo history and hardens object
creation/deletion against duplicate insertion during redo.

## Focused checks

1. Select object A, move it, then select object B.
2. Undo once: selection should return to object A without changing MOB contents.
3. Undo again: object A's move should be reverted and the dirty marker should clear
   when this returns the document to its loaded state.
4. Redo twice: the move and selection should return in order.
5. Copy/paste one unit with patrol points, then repeatedly undo and redo the complete
   paste sequence. Object and logic-path counts must alternate by exactly one copy and
   must never grow after a complete redo cycle.
6. Repeat the copy/paste test in logic editing mode and confirm that no ghost unit or
   patrol path remains while creation is undone.
7. Open **Settings -> Settings...**. It must open on **General**, where **Undo history
   limit** defaults to 512. The Resources tab retains the legacy resource controls.

Selection-only commands do not alter MOB data and must not set the MOB dirty marker.
The configured undo limit applies after restarting the editor.
