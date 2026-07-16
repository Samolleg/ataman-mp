# Alpha.23 selection and asset-picker regression validation

This is a focused regression build. Use copied MOB files for any save test and
do not overwrite a live JMV map.

## Selection responsiveness

1. Open a representative large MPR and MOB that showed the alpha.22 slowdown.
2. Record the normal unselected viewport FPS.
3. Single-click several units and world objects in the viewport and outliner.
4. Double-click an outliner object to center the camera.
5. Undo and redo selection changes repeatedly, including selection to none.

Expected: selection, outliner reveal, camera centering, Undo, and Redo respond
without multi-second stalls. Selecting an object must not cause a sustained FPS
collapse. The Model and Texture controls must not build their catalogues until
one of those controls is actually opened or edited.

## Model and texture filtering

Test both Object Details and Create New Object.

1. Select a model or texture field and type `h` without accepting a result.
2. Continue typing a known prefix, then remove characters with Backspace.
3. Press Escape or move focus away without choosing a highlighted suggestion.
4. Repeat and explicitly click a result.
5. Enter a complete custom value not present in the list and finish editing.

Expected: results begin with the typed text, case-insensitively. Merely filtering
or highlighting the first result never changes the object or dirty state. An
explicitly chosen result is applied once. A complete custom value is accepted
when editing finishes.

## File menu ordering

Expected File-menu order: Open MPR or MOB, Open MPR, Open MOB(s), Open Recent.

