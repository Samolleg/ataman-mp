# Alpha 26 Russian localization test

## What changed

- Added Russian localization across the editor: menus, settings, setup, object
  properties and value help, viewport/status help, validation and preflight
  findings, information panels, secondary editors, and user-facing notices.
- Added **General > Language** with persistent English and Russian choices.
  The editor now prompts for a restart after the language is changed. English
  remains the default and untranslated text safely falls back to English.
- Revised Russian wording for concise, natural editor terminology and removed
  literal machine translations. Previously missed script-window titles, setup
  summaries, comparison values, undo-history labels, diplomacy help, and map
  catalog notices are now localized.
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

Diagnostic/runtime logs intentionally remain in English. Object names, paths,
serialized values, script text, and file-format identifiers are not translated.
