# Alpha.26 minimap polish tester checklist

Use copies of maps/resources and the configured test profile. Do not point an
export at the only copy of an archive.

1. Load a normal gameplay MPR + MOB and select Embedded. Click the viewport
   refresh button. Confirm the source switches to Generated after capture.
2. Orbit the camera through steep downward, shallow downward, and slightly
   upward angles. Confirm the minimap camera frame never crosses itself, remains
   a white outline, and has no tinted/opaque interior.
3. Click several points in the minimap. Confirm camera XY moves while its
   height/orbit state is preserved.
4. Confirm the lock glyph is optically centered in both locked and unlocked
   states and stays synchronized with Export Minimap.
5. Open Export Minimap on a second monitor. Confirm it has the EI Mapper icon in
   the taskbar, normal minimize/maximize/close controls, taskbar activation, and
   that closing it leaves the editor running.
6. Confirm Reset settings is beside Fit/100%, and Apply & Lock / Unlock has a
   stable wide button. Regenerate and grade while zoomed/panned; the comparison
   view must retain its transform.
7. Load a catalog zone with authored type `brief`. Confirm the viewport minimap
   is hidden, no generated texture is staged for Test Current Map, and Export
   Minimap reports: `Minimaps are not available in briefing zones.`
8. Return to a gameplay zone and confirm embedded/generated sources and export
   become available again without restarting the editor.
