# Alpha 26 diplomacy labels and roadmap update (r24)

## Changed

- MOB diplomacy cells now show their stored value and meaning:
  **0 (Ally)**, **1 (Neutral)**, or **2 (War)**.
- Each cell tooltip explains that the row faction applies that relationship to
  the column faction.
- The diplomacy table tooltip contains the complete value legend and explains
  that double-clicking a non-diagonal cell cycles its value.
- The roadmap now records the completed Behavior hierarchy/collection
  authoring work separately from the remaining orphan/wrong-parent validation
  and cross-owner remapping work.

The serialized diplomacy values and the existing Undo/Redo cycle are unchanged.

## Focused test

1. Open **MOB Parameters > Diplomacy...** on a MOB with a diplomacy table.
2. Confirm cells read **0 (Ally)**, **1 (Neutral)**, or **2 (War)** rather than
   displaying unexplained numbers.
3. Hover each relationship type and confirm its row-to-column meaning.
4. Double-click a non-diagonal cell, then Undo and Redo. Confirm the symbolic
   label and color follow the stored value after every refresh.
