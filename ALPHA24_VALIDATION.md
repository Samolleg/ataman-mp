# Alpha.24 outliner and asset-completion validation

Use copied MOB files for any save test. Do not overwrite a live JMV map.

## Outliner scrolling

1. Expand a long outliner category and scroll away from its midpoint.
2. Single-click different currently visible outliner rows.
3. Undo and redo those selection changes.
4. Select an object directly in the 3D viewport.

Expected: clicking an outliner row does not recenter or otherwise automatically
scroll the outliner. Undo and Redo restore selection safely. A selection made in
the 3D viewport still expands, reveals, and centers the corresponding row.

## Model and texture completion

Test both Object Details and Create New Object.

1. Type `h` into Model or Texture without accepting a suggestion.
2. Verify every suggestion begins with `h`, case-insensitively.
3. Continue typing and use Backspace; do not click or press Enter on a result.
4. Confirm the object value and MOB dirty state remain unchanged.
5. Explicitly choose a suggestion and confirm that exact value is applied once.
6. Enter a complete custom value and finish the edit.

Expected: filtering never replaces the typed prefix or silently applies the
first suggestion. Suggestions use prefix matching, not contains matching.

Automated companion check: `EIMapperEndurance.exe --ui-widget-check`.

