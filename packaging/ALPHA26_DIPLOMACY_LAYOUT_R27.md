# Alpha.26 diplomacy layout r27

## Changed

- First-open sizing now sums all 32 settled column widths and all 32 settled
  row heights after the window has completed one layout event. It fits that
  content when possible and otherwise uses up to 95% of the available screen.
- The corrected calculation uses a new saved-size key, so the undersized r26
  result is reset once. User resizing remains persistent afterward.
- Diplomacy columns and rows are measured once when the table is populated,
  then use fixed section sizes. Resizing the window no longer continuously
  remeasures 32 rotated headers.
- Selecting a cell explicitly bolds both the acting-faction row header and the
  affected-faction column header, in addition to the row/column cross-highlight.

## Manual checks

1. Open diplomacy and confirm it initially shows the complete 32x32 table when
   the screen can fit it, or occupies nearly all available space otherwise.
2. Drag-resize the window continuously and confirm it remains responsive.
3. Select several cells and confirm the corresponding row and column names are
   both bold each time.
4. Resize, close, and reopen the window; confirm the chosen size returns.
