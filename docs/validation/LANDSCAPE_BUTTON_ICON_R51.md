# Landscape Tools button icon validation R51

## Changelist

- assign the generated mountain icon directly to the persistent Landscape
  Tools viewport button;
- keep the button outside the exclusive transform-operation connector so its
  checked state continues to mirror whether the Landscape Tools dock is open.

## Automated evidence

- Qt 5.12.12 / MinGW 7.3 release build: passed;
- headless widget regression: must pass before packaging;
- packaged `icons_fluent/landscape.ico` must match the project asset by
  SHA-256.

## Manual test

1. Start the editor and confirm the two-green-mountain icon appears to the
   left of **Landscape Tools** on the viewport toolbar.
2. Open and close Landscape Tools with the button and `L`. Confirm the button
   checked state still follows the dock visibility.

This check does not require loading or saving any game map.
