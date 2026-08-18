# Alpha 26 modern-atlas R130 publication workflow test

This revision makes ordinary **Save MPR** and **Save MPR As** own the complete
modern-terrain publication flow. **Save editable modern draft as...** remains a
separate action for intentionally unfinished working-atlas snapshots.

## Changes

- Removed the second MPR destination prompt. **Save MPR** publishes back to the
  open MPR; **Save MPR As** publishes to the already selected destination.
- The textures archive chooser now starts at the active resource profile's
  first existing resource directory, with `textures.res` preselected. The main
  review shows the exact MPR and RES targets before publication begins.
- Kept explicitly chosen RES archives outside the active profile available,
  while warning that the editor and game will not resolve them until installed
  or added to the profile.
- A single save now publishes modern-painted Ground and Overlay layers together
  and preserves the exact packed slot and rotation field for both.
- Editing only the material resource identity now enables **Save MPR**. MPR
  Settings also provides **Reset to current identity**, and a cancelled save no
  longer discards the requested identity.
- Replaced user-facing finalization terminology with the normal MPR save model.

## Focused manual test

1. Open a copy of an MPR under the JMV resource profile, paint both Ground and
   Overlay materials, and choose **Save MPR**.
2. Confirm the RES chooser opens in the profile's mod `Res` directory with
   `textures.res` selected, then verify the review names the exact current MPR,
   that RES file, and both material layers.
3. Complete the save, confirm the current document stays open and clean, then
   paint and save it again.
4. Repeat with **Save MPR As**. Confirm there is only one MPR path choice and the
   review shows that selected destination.
5. Change only the resource identity in MPR Settings. Confirm **Save MPR** is
   enabled, then verify **Reset to current identity** cancels that pending edit.
6. Optionally select a RES outside the active profile and confirm the review
   warns about installation/profile visibility without refusing the choice.

## Known limitations

- Rotation-equivalent physical deduplication remains disabled by decision.
- This build does not change the current semantic catalog or approved masks.
