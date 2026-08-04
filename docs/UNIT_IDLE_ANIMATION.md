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
action member contains quaternion/translation channels and, where present,
per-frame vertex morph deltas for each component. The
logical timeline runs at 15 Hz and preserves the extra closing loop sample.
Component matrices propagate through the figure link hierarchy using EI's
child-before-parent quaternion composition order, including
canonical equipment attachment channels such as `rh3.sword00` applied to the
selected TTI variant. Color and selection passes use the same cached frame
context, part matrices, and morph-updated buffers. Morph buffers are updated in
place; figure topology and buffer objects are not rebuilt per frame.

Resolved clips are cached by animation-provider ID, immutable database
generation, model, and exact equipped-weapon list. Units with the same request
share the parsed, implicitly shared clip data, while their playback time and
applied pose remain instance-local. Changing profiles or refreshing the active
database generation clears this cache, so Base/JMV and source revisions cannot
cross-contaminate one another.

The JMV provider selects only standing-state (`allowedState == 2`) defaults.
It prefers `cidle01` for ordinary actors and the matching JMV 2H action family
for equipped humanoids. StrongMan-authorized dual-2H and 2H+shield layouts use
the ordinary `cidle01` one-hand/shield and dual-wield stance because the active
JMV humanoid ADB has no dedicated Titan Grip standing action.
Aggregate ADB weapon masks remain compatible with unarmed actors, which is
required by the `unmogo` family. Root translation is applied relative to its
first animation sample so the actor stays at its MOB position.

## Performance policy and diagnostics

The Render settings page contains **Animate unit idle cycles**. When enabled:

- hidden or frustum-culled units perform no animation evaluation;
- selected units and units at most 100 world units away interpolate at 15 Hz;
- units from 100 to 300 units update poses at 7.5 Hz;
- visible units beyond 300 units continue advancing at 3.75 Hz.

`AnimationFrameContext::counters()` exposes per-frame full, half-rate,
low-rate, off-screen-skip, and static-fallback counts for profiling and
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
   counters for full, 7.5 Hz, and 3.75 Hz tiers. Every visible unit must keep
   advancing; only hidden or frustum-culled units may skip evaluation.
6. Disable the Render toggle and confirm all units return to their static bind
   pose without changing their MOB data.
7. Use a profile with an unknown animation provider and a test copy missing an
   ADB or ANM action. Confirm a diagnostic/static fallback and no Base/JMV
   substitution.
8. JMV: check `zone9.mob` ID 207 and representative `unhufe`/`unorfe` units;
   they must use a standing idle. Check `unmogo` actors animate, `unmoli` uses
   its two-handed ready idle, and no actor moves vertically from its MOB origin.
9. JMV: check `unmodg` wing membranes. The membrane vertices must follow their
   ANM morph frames while their parts follow the animated wing hierarchy.
10. JMV: check `unmoli` hand-to-trident contact, goblin/lizard integral weapon
    rotation, `unmodr` standing idle, and humanoid limb/helmet/weapon joints.
    No child may orbit away from its parent as rotations interpolate.

Synthetic coverage lives in `tests/unit_animation_tests.cpp` and checks ADB
bounds/mask selection, Base/JMV isolation, nested ANM closing-sample
interpolation, hierarchy propagation to numbered equipment, and distance
tiers.
