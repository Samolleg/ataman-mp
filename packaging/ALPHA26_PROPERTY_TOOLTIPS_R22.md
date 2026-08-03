# 0.3.0-alpha.26 Property tooltips r22

## Changes

- Keeps editor-only, preserved, and currently unused MOB fields editable.
- Guard idle time now explains that it is preserved but unused by the audited
  unit AI.
- Music now explains that it is preserved and has no confirmed runtime audio
  behavior instead of claiming that it changes a source into music.
- A selected look point explains that X/Y determine facing while its stored Z
  value is not used to calculate the look angle.
- Inherited magic-trap fields without confirmed native trap behavior or
  visualization use now say so only when a magic trap is selected. Ordinary
  objects keep their normal field descriptions.
- Sound delay tooltips no longer repeat the unsupported 15-ticks-per-second
  claim.
- Includes r21's read-only derived trap target summary; all serialized source
  fields remain preserved.

## Focused manual checks

1. Select a unit Logic slot and inspect Guard idle time. Confirm it remains
   editable and its tooltip explains its preserved/unused status.
2. Select a sound and inspect Music. Confirm it remains editable and no longer
   claims a runtime music effect.
3. Select a look point. Confirm the Position tooltip distinguishes X/Y facing
   from the stored Z value.
4. Select a magic trap in Object mode. Check rotation, scale, secondary
   texture, shadow, Parent ID, parent template, script availability, and
   attached quest value. Confirm they remain editable and receive the
   trap-specific preservation tooltip.
5. Select an ordinary object and a point light. Confirm their normal tooltips,
   including point-light Cast shadows, remain unchanged.

## Known limitations

- “No confirmed runtime use” intentionally describes the present evidence; it
  does not discard, normalize, hide, or lock the serialized value.
