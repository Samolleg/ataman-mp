# AI graph object placement R7

## Changelist

- Fixed terrain-relative MOB height being applied twice in R6. Static objects
  now sample the terrain from zero and apply their stored vertical offset once.
- Rendering and regenerated AI-graph collision continue to share the corrected
  world transform, so a visible bridge and its navigation deck stay aligned.
- No MPR, MOB, or graph serialization rules changed in this revision.

## Manual checks

1. Open `gz16h` and confirm the stone bridge is at the same height as in game,
   rather than floating above the ravine.
2. Regenerate layer 3 and confirm the bridge navigation surface is drawn on the
   visible bridge and connects to terrain at both ends.
3. Use Test Current Map and issue a long-distance move order across the bridge.
4. Spot-check an elevated static object elsewhere to confirm its visible mesh and
   regenerated collision remain aligned.

## Known boundary

The editor cannot predict a script-loaded autozone that is not loaded in the
current workspace. Load the relevant secondary MOB or collection before
regenerating when its static geometry must participate.
