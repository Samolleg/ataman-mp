# Alpha.28 semantic provenance and frustum-culling validation

Use copied MOB files only. Alpha.28 does not expand the set of changed fields
that may be saved: `OBJ_PLAYER` remains the only enabled changed-save class and
structural saves remain blocked.

## Tester evidence carried forward from alpha.27

The supplied `autozone4hadagansTest.mob` is the same size as the installed
`Autozone4hadagans.mob` and differs in exactly one byte. The only semantic change
is `OBJ_PLAYER` 12 to 11 on `Human Hadagan Expidition Male-1854`, NID 1000090,
`UNIT[9]`. There are 117 unchanged objects, no added or removed objects, and no
structural or unrelated byte changes.

The alpha.27 workflow log confirms atomic paste cancellation and Undo behavior,
but its ordered provenance assumption failed closed on representative large MOBs.
Alpha.28 replaces that assumption with a complete semantic raw-versus-projection
alignment. The installed corpus passes 254 of 254 projection transaction checks;
252 object-bearing MOBs also pass raw-subtree copy/delete checks and two objectless
MOBs are skipped.

## Frustum-culling A/B test

1. Open a large map such as Zone 18 and allow the first rendered frame to finish
   building cached object bounds.
2. Use a repeatable close camera view where most map objects are outside the
   viewport. Record FPS for at least ten seconds.
3. Open **Settings > Render**, disable **Frustum culling**, return to the same
   camera position, and record FPS again.
4. Repeat with a distant overview where most objects are visible.

Expected: the close view should improve when culling is enabled. The overview may
improve less because fewer objects are outside the frustum. The first frame is not
a valid measurement because bounds are populated lazily and then cached.

Orbit, pan, select, rectangle-select, and double-click focus objects near every
screen edge. No visible object may disappear early or become unpickable. A selected
object must remain handled conservatively. Logic-edit mode and **Draw Logic**
deliberately bypass culling so patrol paths and other extended helpers are not cut.

If geometry pops incorrectly, disable the setting and return the camera position,
object identity, screenshot, and clean-session workflow log. Also report hardware,
resolution, and FPS for both setting states.

## Provenance regression

Open representative small, large, and duplicate-NID MOBs. The workflow log must
report `Bound raw MOB provenance to <count> projected objects`, where the count
equals the loaded object count. Any incomplete semantic-alignment warning remains
a safe failure, but should be returned with the source MOB and log.

Repeat one isolated diplomacy/player edit on a disposable copy. Saving must
produce exactly one authorized `OBJ_PLAYER` leaf change. Model, texture, position,
copy, delete, and other unvalidated edits must still be refused as a whole.

## Automated validation

- Production Qt 5.12/MinGW application build passes.
- `view_frustum_tests` covers inside, intersecting, and outside spheres for
  identity and perspective clip matrices.
- `endurance_document_tests` covers deterministic matching of reordered exact
  duplicate subtrees.
- Installed Base/JMV projection corpus: 254 passed, 0 failed.
- Source round-trip and hidden UI regression checks are required on the packaged
  build before publishing.

## Tester result

Zone 18 rendered smoothly at a stable reported 60+ FPS with no observed visual
problems. Semantic provenance bound all 4,463 Zone 18 objects, all 1,693 Zone 1
objects, and all 118 Autozone objects. The Settings window could not switch tabs,
preventing the requested enabled/disabled A/B comparison. Entering Logic mode
reduced performance to 10-20 FPS because alpha.28 conservatively disabled culling
for the entire scene; this is corrected in alpha.29 by separating object geometry
from logic-helper drawing.

The returned Autozone save is 78,598 bytes, matching its source size. It contains
one byte change at offset `0x8FDA`, `OBJ_PLAYER` 12 to 13, on `UNIT[17]` (`NewObject
- 1000014`, NID 1000014). Semantic comparison reports 117 unchanged objects and
one modified object, with no additions, removals, or unrelated byte changes. A
subsequent prototype edit was rejected as an unvalidated save class.
