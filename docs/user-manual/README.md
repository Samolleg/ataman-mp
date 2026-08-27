# EI Mapper Endurance user manual / Руководство пользователя

This manual describes the implemented UI and authoring contracts in **EI
Mapper Endurance 0.5.0-alpha** at repository baseline
`eec794b1aab6df8e187816174bdebc7987e49ce2`.

It is not a roadmap: unchecked wishes are excluded, and current limitations and
advanced diagnostic/ownership operations are labeled in the text.

## Browse the manual

- [English — browsable HTML](html/en/index.html)
- [Русский — HTML для просмотра](html/ru/index.html)
- [English — maintainable Markdown source](en/manual.md)
- [Русский — исходник Markdown](ru/manual.md)

## Maintainer material

- [Coverage inventory and regression](COVERAGE.md) — source-backed feature to
  section mapping.
- [Screenshot checklist](SCREENSHOT_CHECKLIST.md) — safe current-UI captures
  still wanted; legacy upstream screenshots are deliberately not substituted.
- `assets/` — privacy-safe conceptual diagrams and the offline HTML stylesheet.
- `tools/build_manual.py` — deterministic standard-library Markdown-to-HTML
  builder for the subset used by these manuals.
- `tools/validate_manual.py` — EN/RU anchor parity, coverage, TS terminology,
  internal-link, image, and generated-HTML validation.

Run from the repository root:

```powershell
python docs/user-manual/tools/build_manual.py
python docs/user-manual/tools/validate_manual.py
```

The generated HTML is committed so the manual is browsable without a Python or
documentation toolchain. Rebuild it after changing either Markdown source.

## Versioning policy

Update the version/baseline in this index, both manuals, and `COVERAGE.md` when
the described UI changes. Add implemented features to the coverage inventory
before documenting them. Keep the explicit anchors and their order identical
between languages; existing anchors are stable links and should be renamed only
with a compatibility alias.

Russian button/menu terminology must continue to match
`translations/ei_mapper_ru.ts`. Logging, headless diagnostics, paths, resource
names, scripts, serialized values, and tokens may remain English as described
by `docs/RUSSIAN_LOCALIZATION.md`.
