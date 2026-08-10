# Russian localization

Endurance uses Qt 5.12 translation catalogs. English source text is the default
and fallback; Russian is stored in `translations/ei_mapper_ru.ts` and compiled
to the embedded `ei_mapper_ru.qm` resource by the checked release wrapper.

## Language choice

The persistent selector is in **Settings > General > Language**. The stored
option is `General.language` in `endurance-options.json` and accepts `en` or
`ru`. Unknown values and missing catalogs safely select English.

A restart is required. Endurance creates long-lived generated models, property
metadata, secondary windows, and viewport overlays. Retrofitting every one of
those objects on `LanguageChange` would be error-prone, so the translator is
installed once before the splash and first visible widget are constructed.
Saving a different language shows a restart-required dialog immediately.
Headless validation runs before translator installation and remains English.

## Translation sources

- Qt Designer `.ui` forms are extracted automatically.
- C++ widget, action, dialog, status, tooltip, viewport-help, and preflight text
  uses `tr()` or `QCoreApplication::translate()`.
- Dynamic property names and descriptions in `CTableManager` use `tr()` while
  property IDs and serialized values remain unchanged.
- MOB-family findings remain English in their report model for logs and JSON.
  The validation panel matches the English message templates declared with
  `QT_TRANSLATE_NOOP` and translates only the displayed copy.
- Resource names, paths, object names, script contents, MOB/MPR/JMV/NID tokens,
  file extensions, and command-line diagnostic output are not translated.

## Maintainer workflow

Use the retained Qt 5.12.12 MinGW kit:

```powershell
lupdate.exe ei_maper.pro -no-obsolete
python scripts/seed_ru_catalog.py       # optional seed for new unfinished text
python scripts/review_ru_catalog.py     # reviewed EI terminology overrides
lrelease.exe translations/ei_mapper_ru.ts -qm translations/ei_mapper_ru.qm
python tests/localization_contract_tests.py
```

Review newly seeded translations before committing. The contract test checks
catalog completeness, placeholder and rich-text parity, line breaks, protected
technical tokens, representative terminology, build integration, fallback,
settings persistence hooks, validation presentation, and English diagnostics.
The release `--ui-widget-check` additionally loads the embedded catalog and
exercises persistence and fallback at runtime.
