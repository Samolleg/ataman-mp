# AI graph object placement R8

## Changelist

- Replaced renderer-oriented first-hit terrain projection for terrain-relative
  MOB objects with a dedicated runtime-compatible terrain-surface query.
- The canonical query now observes displaced SEC vertices, native coordinate
  and height quantization, nearest-cell correction, the fixed terrain diagonal,
  and folded-cell fallback before applying the stored MOB vertical offset once.
- Visible object placement and regenerated navigation collision continue to use
  the same resolved world transform. MPR, MOB, and graph serialization are
  unchanged in this revision.

## Manual checks

1. Open `gz16h` and compare the stone bridge with the game: its deck and both
   banks should now have the same vertical relationship in both views.
2. Regenerate layer 3 and confirm the navigation surface lies on the visible
   bridge and connects to terrain at both ends.
3. Use Test Current Map and issue a long-distance movement order across it.
4. Spot-check ordinary terrain-relative world objects in `gz1g` or `gz3g` for
   unexpected vertical shifts.

## Known boundary

Script-loaded autozone geometry participates only when its corresponding MOB is
loaded in the current workspace. Runtime confirmation of the `gz16h` bridge is
still required because synthetic tests cannot contain the proprietary map.
