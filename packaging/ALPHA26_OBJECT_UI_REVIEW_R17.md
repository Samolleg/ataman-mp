# 0.3.0-alpha.26 Object UI review r17

## Changes

- Position, rotation, scale, and other three-component vector properties now
  place X, Y, and Z on one row whenever the available value-column width can
  fit all three editors.
- Narrow property panels automatically return those components to a vertical
  X/Y/Z layout instead of squeezing unusable controls into one row.
- Long values in the horizontal layout are shortened visually with an
  ellipsis. Focusing a component restores its complete stored value for
  inspection and editing; the shortened preview never changes the property.

## Focused manual checks

1. Select an object with Position, Rotation, or Scale and widen Object
   Properties. Confirm X, Y, and Z move onto one row when they fit.
2. Narrow the panel and confirm the same vector returns to three readable rows
   without losing its values.
3. With the panel just wide enough for the horizontal layout, use a component
   containing a long decimal value. Confirm its unfocused preview ends in an
   ellipsis, then click it and confirm the complete value appears.
4. Edit a component, commit it, resize the panel in both directions, and verify
   the value and undo behavior remain unchanged.

## Known limitations

- The exact switch width is derived from the active UI font and style, so it
  can differ slightly between display-scale and theme configurations.
