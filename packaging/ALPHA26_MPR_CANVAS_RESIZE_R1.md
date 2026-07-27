# 0.3.0-alpha.26 - MPR canvas resize test

## Changelist

- Adds **MPR Settings > Canvas resize** with a generated-map preview, new
  sector dimensions, and nine-point old-canvas anchoring.
- Crop/expand is Save As-only. Retained SEC payloads and unknown archive
  members are preserved; new land sectors extend the nearest existing edge.
- Rebuilds the changed archive's game-compatible RES hash table, freshly
  reopens the candidate, checks canonical seams, backs up an existing
  destination, and commits atomically.
- Exposes an experimental maximum of 32 x 32 sectors for runtime testing.
- Does not invent overlays, map catalog entries, minimap registrations, or AI
  graph files.

## Manual test

1. Open a copied MPR, then open **MPR Settings > Canvas resize**.
2. Expand one axis by one or two sectors. Confirm the preview/retained-added-
   removed summary, use **Resize canvas and Save As**, and verify the new MPR
   opens automatically with seamless projected edge terrain.
3. Crop a copy from an origin-preserving anchor and confirm the expected area
   remains. Reload both source and output; the source must be unchanged.
4. With no MOB loaded, try a different anchor and verify the old canvas moves
   exactly as previewed. With a MOB loaded, confirm an origin-moving resize is
   refused before a destination is written.
5. Create a 32 x 32 copy, then use **Test Current Map**. Report load time,
   navigation/graph behavior, and any engine refusal. Do not use a live or only
   copy of a map for this boundary test.

## Known limitation

The companion operation that translates a MOB and optionally removes objects
outside cropped bounds is not enabled yet. Origin-moving resize therefore
requires unloading MOBs; source MOB files are never modified.
