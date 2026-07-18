# Provider-resolved unit idle animation

Actor eligibility is semantic: every visible `CUnit` asks the
manifest-selected `unitAnimation` provider for a
compatible default IDLE cycle. The resolver reads only the active immutable
database snapshot's model ADB table. `base.standard.v1` accepts Base weapon
masks; `jmv.v1` additionally accepts JMV's extended weapon-type bits. An
unknown provider, missing ADB/ANM member, incompatible weapon mask, malformed
channel, or incomplete figure hierarchy produces a logged static fallback. No
MOB or database value is repaired or changed.

Static scenery and other `CObjectBase` subclasses never enter this path. A
unit's animation capability is established by its provider-resolved ADB,
model-local ANM action, and `.mod`/`.bon` assembly hierarchy—not by hardcoded
model-name or humanoid-name lists.

The selected model's `.anm` resource is a nested RES archive: the selected
action member contains one quaternion/translation channel per component. The
logical timeline runs at 15 Hz and preserves the extra closing loop sample.
Component matrices propagate through the figure link hierarchy, including
canonical equipment attachment channels such as `rh3.sword00` applied to the
selected TTI variant. Color and selection passes use the same cached frame
context and part matrices; vertex buffers are not rebuilt per frame.

## Performance policy and diagnostics

The Render settings page contains **Animate unit idle cycles**. When enabled:

- hidden or frustum-culled units perform no animation evaluation;
- selected units and units at most 100 world units away interpolate at 15 Hz;
- units from 100 to 300 units update poses at 7.5 Hz;
- units beyond 300 units retain the first idle pose.

`AnimationFrameContext::counters()` exposes per-frame full, half-rate,
first-pose, off-screen-skip, and static-fallback counts for profiling and
future viewport instrumentation.

## Runtime validation cases

Validate each case in both a Base profile and the JMV reference profile:

1. Open a map containing human, orc, skeleton, and undead humanoids; confirm
   each uses an ADB-selected idle and remains assembled while moving.
2. Check unarmed, one-handed, and ranged Base units. Confirm Base never selects
   an extended JMV weapon family.
3. Check JMV ordinary, shield, dual-one-hand, and single-two-hand layouts;
   confirm equipment follows the animated hand/attachment hierarchy.
4. Select a near and a distant unit; selection color geometry must match its
   visible pose exactly.
5. Move the camera through the 100 and 300 unit boundaries and inspect the
   counters for full, 7.5 Hz, and first-pose tiers.
6. Disable the Render toggle and confirm all units return to their static bind
   pose without changing their MOB data.
7. Use a profile with an unknown animation provider and a test copy missing an
   ADB or ANM action. Confirm a diagnostic/static fallback and no Base/JMV
   substitution.

Synthetic coverage lives in `tests/unit_animation_tests.cpp` and checks ADB
bounds/mask selection, Base/JMV isolation, nested ANM closing-sample
interpolation, hierarchy propagation to numbered equipment, and distance
tiers.
