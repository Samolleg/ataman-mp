# X64NAV03A — этап 1, 2026-10-27

**Маппер: TECHNICALLY VERIFIED / PREPARED 227+x64nav.1. Данные r12: DATA QUALITY BLOCKER.**
Чтец, слой и запуск запекателя подготовлены; полная приёмка всех профилей
остаётся открытой из-за выбросов detail-геометрии в самих r12-файлах.
Установка, push и merge не выполнялись.

## Идентичность и разрешение

- Ветка rebuild-x64; worktree `C:\GOG Games\maped\ei-mapper-ataman-rebuild-x64`.
- База main: `adfcad6dd64c594fa183b563f9aa6eba1fea14d8`, содержит установку227
  `825d29dbf219a1d057b7e69545b7841d91e7efd5`.
- Реализация: `5ce1fecadae5e464915c9719806dff8059e6e05e`; диагностика и
  **источник финальной сборки**: `d2c7d28a496fab91538b7a8223da852f68401137`.
  Последующие изменения отчёта/ROADMAP не меняют программу этой сборки.
- VERSION ветки —227+x64nav.1; номер линии main не занят, основной VERSION227
  и checkout main сохранены. Коммиты Codex <codex@localhost>, с заданным trailer.
- Владелец2026-10-27: «собственный код владельца, разрешение на перенос 2026-10-27».
  Подтверждено новое происхождение Navigation без приватного/декомпилированного
  кода движка. Прежний лицензионный блокер снят; r11 заменён владельцем на r12.

## Источник и vendor

EIRebuild64 task/X64NAV01: **e333b482922e47ad5dbf7b3e33a26a91628c07b2**.
Файлы взяты через git archive этого SHA, не из изменяемого рабочего дерева
`C:\Users\samol\OneDrive\Документы\Проклятые Земли\work\eirebuild64-wt\X64NAV01`.

`third_party/eirebuild64-nav`: **19 побайтных копий** — Navigation/Types.h,
Navigation/README.md, Detour/Include (9 заголовков), Detour/Source (7 cpp),
external/recastnavigation/License.txt (zlib). RecastNavigation1.6.0 upstream:
`6dc1667f580357e8a2154c28b7867bea7e8ad3a7`. DebugUtils, Recast, Navigation/
Build.cpp, Geometry и Query EIRebuild64 не переносились. Types.h и README
повторно просмотрены на новом SHA: собственные структуры/математика/контракт,
без прямых зависимостей от приватного движка. Detour сохраняет upstream notices.

source.lock.json содержит SHA источника, путь/SHA256 каждого файла и разрешение.
Все19 staged Git blobs совпали с источником; `-text` в .gitattributes сохраняет
исходные окончания строк. Qmake проверяет lock до компиляции; тестовая порча
отдельной копии Types.h обнаружена. Vendor не правился; inverse byte shuffle
написан в коде маппера по README, отдельно от Recast-построения.

## Что реализовано

- Container3 / algorithm7 / Detour1.6.0 / DT_POLYREF64; списки допустимых
  контейнеров/алгоритмов — в одном модуле. Старый container2 и неизвестный
  алгоритм дают понятное требование перепечь/обновить маппер.
- LZMS через системный System32/cabinet.dll. Лимиты, длины и unchanged header
  проверяются до unshuffle; затем float/индексы/detail/BV/ABI/firstLink — до
  addTile, который восстанавливает связи. CPU-загрузка — QtConcurrent.
- Единственная функция findNav64ForScene проверяет встроенные MPR/MOB/profile.
  Штатное имя ускоряет поиск, без подмены проверки полей. При резервном поиске
  дубликаты отвергаются. Cache64/Nav не используется. Base fallback использует
  Nav64 активного профиля мода, а не ошибочно Base/Maps/Nav64.
- Док Navmesh рядом с AI-панелью и в меню:8 профилей, радиус/высота из файла,
  detail-треугольники по областям, рёбра, тайлы, старый AI-граф/Navmesh.
  Геометрия по тайлам; offscreen тайлы не отправляются в OpenGL. LOD/утрата
  detail не потребовались по замеру. MOB и старый граф не меняются.
- Острова — компоненты связности по всем высотным флагам; маркеры вне крупнейшей.
  Узкие порталы — приблизительные кандидаты по ребру короче диаметра на уже
  эродированной сетке. Это не измерение исходного прохода и не проверка позы.
- Актуальность приблизительна по mtime MPR/MOB/.navsrc и несохранённым правкам.
  Полный геометрический fingerprint не вычисляется; модели/архивы и правки с
  сохранением mtime так не проверяются. UI прямо говорит «приблизительно».
- startBake — единственная граница запуска QProcess/CREATE_NO_WINDOW; путь
  настраивается, default game-root/Engine64/EIRebuild64NavBake.exe. Аргументы
  передаются списком, --output-root — корень: producer сам добавляет Maps/Nav64.
  Продуктовый default — мод; перед перезаписью — CBackupManager и leases.
  Запуск только явной кнопкой, журнал, отмена собственного процесса.
- PROFILE/MAP_DONE/MAP_ERROR/MAP_MISSING/BAKE_DONE разбираются. Успех требует
  normal exit0 и BAKE_DONE completed=maps, missing=0, errors=0. PROFILE отдельно
  не является успехом. После успешного bake той же пары слой перечитывается.
- .navsrc: отключённое место в UI/коде; авторинг и запись не реализованы.

## Финальная проверка

**132 PASS,0 FAIL, exit0,70,659с** на production objects финального Release.
Это PASS чтения/отрисовки/диагностики/запуска, **не PASS качества исходных r12**.
Квитанции: [stdout](../../build-x64nav-final-r2/evidence/stdout.txt),
[process](../../build-x64nav-final-r2/evidence/process.json),
[сохранность](../../build-x64nav-final-r2/evidence/preservation.json).

- r12, zone1/zone1 у ворот (пилот X172–201/Y103–125 из отчёта X64NAV01),
  bz19g/bz19g и крупная zone14/zone14:24 контейнера, все8 профилей в слое.
- Проверены переименование при сохранённой embedded identity, неверная карта,
  старый контейнер, неизвестный алгоритм, обрезанный файл и fallback каталога Base.
- Старый AI-граф доступен на3 картах; изменение framebuffer подтверждено.
  Короткий регресс227: zone7 + Autozone7letter -> zone6 -> zone7, дополнительный
  MOB корректно отсоединён. MPR/MOB тестовых карт побайтно не менялись.
- Реальный r12 baker SHA256 до запуска:
  `663fcbe929da4281f78d551d6e808d1bcdf4689dc9a8e679ae0d19da06f2e6d2`.
  Запуск через startBake на bz19g, приватные game-root и output-root:
  exit0, BAKE_DONE maps1/completed1/missing0/errors0,919мс,8 профилей,
  578627 байт. Все8 повторно прочитаны, слой обновлён из результата.
- Реальные ошибки: отсутствующая карта -> MAP_MISSING/exit5; испорченный
  приватный MPR -> MAP_ERROR/exit4. Неверный путь EXE, неполный журнал,
  ненулевой exit при BAKE_DONE, отмена и --all проверены отдельно; --all
  проверялся тестовым процессом, вся кампания заново не запекалась.
- Бэкапы:59 payload SHA совпали с sourceSha256; квитанция
  [backup-receipt](../../build-x64nav-final-r2/evidence/backup-receipt.json).
  [Манифест реальных8 выходов](../../build-x64nav-final-r2/evidence/baked-nav64-manifest.json),
  [журнал панели](../../build-x64nav-final-r2/evidence/baker-log.txt).

### Zone1: время

| Профиль | CPU чтение+unshuffle+mesh+геометрия, мс | Готовность слоя в скрытом GUI, мс |
|---|---:|---:|
| tiny |31|159|
| small |28|1231|
| human |27|77|
| large |24|95|
| giant |18|52|
| levitate_tiny |24|54|
| levitate_small |23|52|
| levitate |22|30|

GUI-замер включает очередь Qt/paint/подготовку сцены; единичные1231мс не
приписываются декодеру, не скрыты и не являются обещанием latency UI.
Кадр у ворот1100×820:64/168 тайлов,112865 submitted vertices;
20 измерений после3 прогревочных кадров, paintGL+GPU finish:
median7,1882мс, p959,7333мс. Это полный кадр, не чистое время слоя. GL errors0.

## Снимки и дефект исходных данных r12

- [Zone1: Navmesh / старый AI](../../build-x64nav-final-r2/evidence/screenshots/zone1-navmesh-ai.png),
  [все8 профилей zone1](../../build-x64nav-final-r2/evidence/screenshots/zone1-eight-profiles.png).
- [bz19g: сравнение](../../build-x64nav-final-r2/evidence/screenshots/bz19g-navmesh-ai.png),
  [все8](../../build-x64nav-final-r2/evidence/screenshots/bz19g-eight-profiles.png).
- [zone14: сравнение](../../build-x64nav-final-r2/evidence/screenshots/zone14-navmesh-ai.png),
  [все8](../../build-x64nav-final-r2/evidence/screenshots/zone14-eight-profiles.png).

На снимках tiny/levitate_tiny видны высотные выбросы. Независимый C# reader
с отдельным LZMS/доступом к byte lanes подтвердил их **в r12-контейнерах**:
zone1 navY3273.21167 (IEEE454C9363) при header bounds около -3.59..30.45м.
В C++ overlay к высоте прибавляются только0,12м; это не причина выброса.

| Карта | detail-вершины вне высотных границ: tiny | levitate_tiny |
|---|---:|---:|
| bz19g |7|10|
| zone1 |9|19|
| zone14 |12|35|

Остальные6 профилей на каждой из3 карт:0 таких вершин. Панель предупреждает
и сохраняет геометрию как записана, без обрезки/ремонта/записи Nav64.
[Независимые результаты](../../build-x64nav-final-r2/evidence/independent-geometry-receipt.txt),
[декодер](../../build-x64nav-final-r2/evidence/Independent.cs),
[геометрический контроль](../../build-x64nav-final-r2/evidence/geometry-receipt.txt).
**Нужно исправление producer и новый выпуск данных. Причина в построителе не
исследовалась и не исправлялась в этом задании. Полная приёмка r12 открыта.**

## Release и staging

Release из git archive ветки, source d2c7d28a496fab91538b7a8223da852f68401137.
Qt5.12.12/MinGW7.3 x64; bitness не менялась. C++14 нужен неизменённому Types.h.
2176 архивных inputs совпали с Git blob bytes (кроме генерируемых lrelease QM);
vendor19+lock побайтно сохранены. [Идентичность](../../build-x64nav-final-r2/source-identity.json),
[inputs](../../build-x64nav-final-r2/archive-input-receipt.json).

**Финальный staging:**
`C:\GOG Games\maped\ei-mapper-ataman-rebuild-x64\build-x64nav-final-r2\build-staging-227-x64nav1`.
Raw/staged --version: EI Mapper Endurance Edition 227+x64nav.1;
4 SHA EXE/MCP/runtime совпали. Финализатор с baseline установки227 PASS:
174 ->176 managed,0 потерь; новые только docs/NAVMESH.md и licenses/Detour-zlib.txt.
cabinet.dll системная, не включена; запекатель/игровые данные не включены.
Manifest SHA256 `E293FC511192B151A79B830C751661F11F2A160588591A66D8FD6A46AD6B7C76`.
[Состав](../../build-x64nav-final-r2/composition.json),
[идентичность бинарников](../../build-x64nav-final-r2/staging-identity.json),
[финализатор](../../build-x64nav-final-r2/finalize.log).
Пакет после финализатора не запускался. Прежний build-x64nav-final staging
из5ce1feca является промежуточным, для передачи используется только final-r2.

## Сохранность, очистка и оставшиеся границы

Живой MODS/Dejavu/Maps/Nav64, Cache64, игра, установленная227, AppData,
настройки/процессы владельца, буфер и приоритеты не изменялись. Основной
checkout остаётся main; SHA256 его ROADMAP и .mcp.json совпали с исходными.
Тяжёлые private test packs использовались через hardlinks только на чтение.
После PASS удалены11 созданных в этой задаче fixture trees, включая старые
r11 и промежуточные р12-копии; квитанции и6 снимков сохранены:
[cleanup](../../build-x64nav-final-r2/cleanup-receipt.json).

Не выполнены: интерактивная приёмка владельца, игровой проход/позы/точная
ширина ворот и физическая корректность навигации, массовый bake49 карт.
Авторинг .navsrc остаётся открытым; полный fingerprint не проверяется.
Ближайший внешний шаг — исправленный producer/data для tiny/levitate_tiny,
затем повтор только затронутых данных. UI/CLI-контракт запекателя подключён.
Задача приёмки всех r12-профилей не закрыта.

Копия отчёта: `C:\Users\samol\OneDrive\Документы\Проклятые Земли\work\supervision\X64NAV03A\REPORT.md`.
В другом проекте записан только этот файл, без изменений EIRebuild64.