# EI-NAVMESH-MAIN-228 — 2026-10-27

## Поручение и идентичность

- Main до слияния: adfcad6dd64c594fa183b563f9aa6eba1fea14d8.
- Fast-forward main: 0e033b64e5699d2c36a058f55d0a747bdb3e7017, без merge-коммита.
- Отдельный коммит228 и источник Release: **4d1e8b844d9b4b0c66cc2737d13f01235bd06e1d** (main). Автор/коммитер Codex <codex@localhost>; трейлер Claude Opus5.5.
- Версия228, navmesh далее в main. Ветка rebuild-x64 и её worktree сохранены, больше не рабочие.
- X64NAV03A этап1: слой чтения .nav64 container3/algorithm8, восемь профилей, заливка33%/контуры, внешний EIRebuild64NavBake.exe. Старый AI-граф в MOB/.grp сохраняется.
- EIRebuild64 918cb6ff578bf09de4c28745c1cf17a37530a802; vendor19 Detour/Types.h с source.lock.json, собственный код владельца, разрешение2026-10-27. Вендорный код не изменялся. Навыки private-engine/modding в сессии отсутствуют; приватные исходники движка не исследовались, использованы только разрешённый vendor и обычный код маппера.
- Владелец разрешил Release/установку228; без push. Мосты Codex/Claude/EIMapperMcp не завершать. Маппер и игра должны быть закрыты перед установкой.

## Сохранность

До работы сохранены исходный ROADMAP, его binary patch и .mcp.json в build-navmesh-228. После fast-forward patch владельца применён обратно; в stage попадает только собственный пункт228. Содержимое владельческого patch сверено дословно, за исключением служебных заголовков diff/смещений строк; .mcp.json совпал по SHA. Исходный снимок всех файлов постоянной установки (SHA/размер/mtime) — installation-before.json. Сборка и тесты выполнены вне установки: **все18983 файла совпали по SHA/размеру/mtime до запуска установщика**. Живые Nav64/MPR/MOB/RES только читались; AppData/настройки тестов изолированы.

## Проверки и передача

**PASS / УСТАНОВЛЕНА228.** Все нижеприведённые квитанции находятся в `build-navmesh-228` этого checkout.

- `tests/nav64_main228_check.cpp`, production objects архивного Release: **22 PASS /0 FAIL**, exit0,35,046с. Проверены OFF по умолчанию, реальные обе кнопки без запекателя/сообщение «Запекатель не подключён»/без выхода, один bz19g--bz19g.human.nav64 r13 (container3/algorithm8,0 высотных выбросов), реальное отличие framebuffer Navmesh/старого графа, байты MPR/MOB/nav64, zone7 + Autozone7letter -> zone6 -> zone7. [Журнал](../../build-navmesh-228/evidence/stdout.txt), [процесс](../../build-navmesh-228/evidence/process.json).
- AI document: **44 PASS /0 FAIL /0 SKIP**,2,960с; загрузка/serialization/comparison/invalidations. [QtTest](../../build-navmesh-228/ai-document-results.txt).
- Старый AI regeneration: **26 PASS /0 FAIL**, exit0,14,123с; явный пересчёт, очередь/отмена, сравнение с исходным и текущим графом, отсутствие записи в исходные MPR/graph bytes. [Журнал](../../build-navmesh-228/ai-regeneration.out), [процесс](../../build-navmesh-228/ai-regeneration-process.json).
- Vendor lock: **19/19 PASS**, источник918cb6ff578bf09de4c28745c1cf17a37530a802. Вендорные файлы не изменялись. Код старого AI-графа не переписывался.
- После всех проверок защищённые оригиналы сохранили SHA/размер/mtime (для read-only RES hardlinks размер/mtime). Ровно одна приватная копия r13 и приватный fixture с hardlinks удалены после PASS; журналы сохранены. [Cleanup](../../build-navmesh-228/cleanup-receipt.json).

## Release и состав

- Новый `git archive main` коммита4d1e8b844d9b4b0c66cc2737d13f01235bd06e1d, Qt5.12.12/MinGW7.3 x64; оба32-битных runtime собраны wrapper. Проверены2180 архивных файлов, отличается только генерируемый translations/ei_mapper_ru.qm. [Inputs](../../build-navmesh-228/archive-input-receipt.json), [build exit0](../../build-navmesh-228/build-process.json), [сборка](../../build-navmesh-228/build-complete.log).
- Первое внешнее логирование сборки трактовало диагностическое stderr qmake как PowerShell NativeCommandError; завершение wrapper подтверждено корректным exit0 на том же новом дереве. В журналах runtime есть нефатальная диагностика поиска vswhere, после которой штатный fallback Visual Studio2022 успешно собрал оба компонента.
- Свежий staging: `build-navmesh-228/build-staging-228-navmesh`. Raw/staged --version: **EI Mapper Endurance Edition228**;4 SHA EXE/MCP/runtime совпали. EXE SHA256: `8429E6B546BB25C242C4B31E141646B4FDDC754E0B62F095E4867EF4996F0C49`. [Identity](../../build-navmesh-228/staging-identity.json).
- Финализатор/внутренний validator **PASS** против принятой установленной227: **174 ->176 managed,0 потерь**. Новые только `docs/NAVMESH.md`, `licenses/Detour-zlib.txt`; запекатель и игровые файлы не включены. Manifest SHA256: `D7A7119D333B00735D0099CBC5D76FE43CC0137B612121D03A53921DF1803416`. [Finalizer](../../build-navmesh-228/finalize.log), [состав](../../build-navmesh-228/composition.json). Пакет после финализатора не запускался и не изменялся.

## Установка27.10

- После PASS всех вышеуказанных проверок и двух проверок отсутствия редактора (включая фон)/игры выполнен канонический `packaging/install_program.ps1`. Источник — проверенный staging, цель — `artifacts/EI-Mapper-Ataman-2-first-test`.
- **8 managed замен /2 добавления /0 удалений**; все176 installed SHA совпали со staging. Установленный EXE с `--version` подтвердил **228**, exit0; только этот неинтерактивный вызов выполнен с приватными cwd/AppData/settings, чтобы не изменить пользовательские журналы/настройки.
- **18805 пользовательских файлов** сохранены по набору/SHA/размеру/mtime,0 различий. Служебный `.ataman-retired.json` и точно учтённые retired program files не относятся к пользовательским данным.
- Откат: **artifacts/rollback227-to228-navmesh-20261027**. Полный старый manifest174 и SHA256SUMS.txt скопированы и проверены до замены; `rollback.ps1 -VerifyOnly`: **ROLLBACK_VERIFIED**.
- **40/40** учтённых EIMapperMcp/Codex/Claude процессов живы после установки; ни один не завершался. Занятый EIMapperMcp.exe заменён штатным rename; точное имя и SHA записаны в retiredAfter квитанции. Два прежних свободных retired-файла удалены самим каноническим установщиком по его журналу; прежние каталоги отката/ветка/worktree сохранены.
- [Квитанция](../../build-navmesh-228/install-receipt.json), [установщик](../../build-navmesh-228/install.log), [откат](../../build-navmesh-228/rollback-verify.log), [нет записей до установщика](../../build-navmesh-228/installation-untouched-before-installer.json).
- Запись установки и текущий пункт ROADMAP фиксируются отдельным scoped-коммитом без чужого diff ROADMAP/.mcp.json. Push не выполнялся. Установка завершена; дальнейший navmesh — в main.

Предыдущий полный r13 этап1: docs/validation/ATAMAN_X64NAV03A_STAGE1_20261027.md (160 PASS). Он не подменяет короткую проверку новой228. Художественная/игровая приёмка, .navsrc и полный fingerprint вне этого технического этапа.
