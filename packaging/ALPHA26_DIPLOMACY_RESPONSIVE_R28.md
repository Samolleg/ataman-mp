# Alpha.26 diplomacy responsive r28

## Changed

- Fixed the automated widget test leaking its synthetic `733x511` diplomacy
  size into the real per-user settings store during late window teardown.
- Production sizing uses a clean V4 key, so the leaked r27 size is ignored.
  Future user resizing remains persistent.
- Live resize events now coalesce table repaints. The window frame and layout
  track the gesture immediately; the 1,024-cell table repaints once 75 ms after
  resizing pauses instead of repainting on every intermediate native event.
- The rotated column-header renderer now owns the active-column state and paints
  its label bold directly. Row header bolding and the cross-highlight remain.

## Manual checks

1. Open diplomacy and confirm it starts at the content-derived size rather than
   `733x511`.
2. Continuously drag a window edge and confirm the gesture stays responsive and
   the table catches up immediately after the drag pauses.
3. Select several cells and confirm both the row name and rotated column name
   are bold.
4. Choose a custom size, close, and reopen; confirm that size is retained.
