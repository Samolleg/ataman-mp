# Alpha 26 Russian localization test

## What changed

- Added Russian localization across the editor: menus, settings, setup, object
  properties and value help, viewport/status help, validation and preflight
  findings, information panels, secondary editors, and user-facing notices.
- Added **General > Language** with persistent English and Russian choices.
  Restart the editor after changing the language. English remains the default
  and untranslated text safely falls back to English.
- Kept diagnostic logs, command-line/headless output, serialized values,
  resource and object names, identifiers, file formats, and scripts in their
  original language.

## Manual test

1. Start `EIMapperEndurance.exe`, complete game/mod setup if prompted, then open
   **Settings > General**.
2. Set **Language** to **Russian**, choose **Save && Close**, and restart the
   editor.
3. Inspect the main menus, setup window, object properties and dropdown value
   tooltips, map/object information, validation panel, and secondary editors.
4. In compact property and validation panels, confirm long Russian labels are
   readable and controls do not overlap.
5. Switch the language back to English, restart, and confirm the English UI is
   restored.

## Known limitation

The full catalog has automated coverage and a reviewed core Evil Islands
terminology pass, but some less common wording may still benefit from a native
Russian editorial polish pass. Diagnostic/runtime logs intentionally remain in
English.
