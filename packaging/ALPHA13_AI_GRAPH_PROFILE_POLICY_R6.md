# EI Mapper Endurance 0.3.0-alpha.13 — AI graph profile policy R6

This tester build corrects regenerated navigation previews for all eight engine
movement profiles while retaining the MPR vertical-scale work from R5.

## Changes

- Regeneration samples the actual half-metre terrain topology instead of a
  coarse corner approximation.
- Water depth is evaluated per profile. Prone movement is blocked by water;
  ordinary profiles respect their clearance; the levitating profile follows
  the water surface.
- All profiles use their correct footprint, slope limit, diagonal corner rule,
  terrain traversal policy, and weighted connection cost.
- Generated links are reciprocal and connected components are assigned
  deterministically.
- Object collision remains a conservative rendered-bounds approximation. The
  diagnostic now says so explicitly; loaded graphs remain authoritative around
  bridges, doors, and complex multilevel objects.

## Focused checks

1. Compare Loaded graph and Regenerated preview at the Zone 1 river: Prone must
   not cross the water.
2. Review all eight layers at shallow and deep water. Levitating may cross the
   water surface; ordinary layers must obey profile clearance.
3. At the test mountain near X 202.04, Y 61.91, Z 20.50, regeneration must not
   create direct ordinary-profile links between the summit and ground.
4. Move a fence or sculpt terrain and confirm asynchronous regeneration updates
   the preview without dirtying MPR/MOB data.
5. Around bridges, doors, and stacked geometry, compare against Loaded graph and
   treat differences as approximation diagnostics, not save-ready graph output.

Only use copied or disposable maps. Regenerated graphs remain preview data and
are not serialized by this build.
