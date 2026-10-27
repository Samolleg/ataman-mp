# EI-NAVMESH-MAIN-228 — 2026-10-27

## Поручение и идентичность

- Main до слияния: adfcad6dd64c594fa183b563f9aa6eba1fea14d8.
- Fast-forward main: 0e033b64e5699d2c36a058f55d0a747bdb3e7017, без merge-коммита.
- Версия228, navmesh далее в main. Ветка rebuild-x64 и её worktree сохранены, больше не рабочие.
- X64NAV03A этап1: слой чтения .nav64 container3/algorithm8, восемь профилей, заливка33%/контуры, внешний EIRebuild64NavBake.exe. Старый AI-граф в MOB/.grp сохраняется.
- EIRebuild64 918cb6ff578bf09de4c28745c1cf17a37530a802; vendor19 Detour/Types.h с source.lock.json, собственный код владельца, разрешение2026-10-27. Вендорный код не изменялся. Навыки private-engine/modding в сессии отсутствуют; приватные исходники движка не исследовались, использованы только разрешённый vendor и обычный код маппера.
- Владелец разрешил Release/установку228; без push. Мосты Codex/Claude/EIMapperMcp не завершать. Маппер и игра должны быть закрыты перед установкой.

## Сохранность

До работы сохранены исходный ROADMAP, его binary patch и .mcp.json в build-navmesh-228. После fast-forward patch владельца применён обратно; в stage попадает только собственный пункт228. Исходный снимок всех файлов постоянной установки (SHA/размер/mtime) — installation-before.json. Сборка и тесты выполняются вне установки. Живые Nav64/MPR/MOB/RES только читаются; AppData/настройки тестов изолированы.

## Проверки и передача

IN PROGRESS. Следующий шаг: свежий Release из git archive main; адресный native Navmesh/227, legacy AI regeneration/comparison, vendor lock; staging/finalizer/состав против227. После PASS — канонический установщик и проверка отката/managed SHA/пользовательских файлов, отдельный scoped-коммит записи установки.

Предыдущий полный r13 этап1: docs/validation/ATAMAN_X64NAV03A_STAGE1_20261027.md (160 PASS). Он не подменяет короткую проверку новой228. Художественная/игровая приёмка, .navsrc и полный fingerprint вне этого технического этапа.
