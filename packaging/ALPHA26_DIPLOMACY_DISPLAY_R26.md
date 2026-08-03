# Alpha.26 diplomacy display r26

## Changed

- Diagonal diplomacy cells now use their stored relation color; the usual
  Ally `0` diagonal is green instead of gray.
- The diplomacy window calculates its first-open size from the populated
  headers, rows, and explanatory controls. It fits the complete content when
  the screen allows and otherwise uses up to 90% of the available screen.
- The corrected sizing uses a new saved-size key, so the undersized r25 default
  is not inherited. Resizing the corrected window is still remembered.
- Selecting a diplomacy cell highlights both its acting-faction row and its
  affected-faction column.

## Manual checks

1. Open a MOB diplomacy table and confirm the window initially fits the matrix,
   or fills most of the available screen when the matrix is larger.
2. Confirm ordinary diagonal `0` cells are green.
3. Select a cell and confirm its complete row and column are highlighted.
4. Resize, close, and reopen the window; confirm the chosen size returns.
