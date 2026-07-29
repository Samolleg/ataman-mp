# 0.3.0-alpha.26 MPR canvas resize R4

## Changelist

- Changing canvas settings no longer moves the zoomed or panned resize preview.
- Right-dragging the original map updates its tile offsets without accumulating
  viewport drift.
- All R3 resize, companion-MOB, and generated graph-staging behavior is retained.

## Focused manual check

1. Open **MPR Settings > Canvas resize**, zoom and pan the preview, then change
   width, height, taper, and offset controls repeatedly. The viewed location and
   zoom must remain fixed.
2. Right-drag the original map repeatedly in every direction. The map placement
   should follow the cursor in tile steps while the preview camera stays fixed.

## Known limitation

Generated graph delivery is temporary for Test Current Map. Persistent external
`.grp` export and explicit embedding into a chosen MOB remain pending.
