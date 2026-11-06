# EI-MINIMAP-IDENTITY-281 — миникарта gz1g

2026-11-06. База main `afb19d9c46e66c79717d1e3544bdeb28865d8dc2`, VERSION280;
281 свободен. AGENTS.md неизменён относительно HEAD, SHA256
`2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7`.
Разрешение владельца: вернуть только первый hunk refreshMapTestContext из
`b7e09aa86affe2778f941e265f083a46f52197c1`. STOP BEFORE INSTALLATION; без push.

## Реализация и исходный красный результат

Первый hunk применим по смыслу и перенесён без остальных изменений278.
В source/ изменена только MainWindow::refreshMapTestContext(): отдельный поиск
явного zoneId до MPR; неявный поиск MPR сверяет MOB с mapTestStagingMob(),
для world отсутствие MOB по-прежнему не разрешает совпадение.
Функция уже сбрасывает m_mapTestEntry перед поиском, поэтому прежняя идентичность
не влияет на выбор MOB в новом проходе.

На свежесобранном коде280 обычный openMapPair(zone1.mpr, zone1.mob, "gz1g")
выбирает **bz1g/brief**, хотя активный документ — zone1.mob.
Миникарта скрыта, планирование генерации запрещено. Красный результат получен
до изменения production-функции и сохранён в red/result.json.

Приватный стенд: work/minimap-identity-281-20261106/fixture, только четыре файла
из текущей DejaVu: zone1.mpr, zone1.mob, basecam.mob, texts.res — исходные байты
и SHA сохранены в fixture-manifest.json. map.txt не переписывался; каталог
загружается штатным CMapCatalog/refreshMapsBrowser из скопированного texts.res.
Для всех bz1g/gz1g/cz1g каталог и openMapPair используют **один путь копии**
zone1.mpr; исходные порядок bz1g перед gz1g и типы brief/game сохранены.
Остальные игровые ресурсы и весь пакет не копировались: для идентичности,
проверки видимости и разрешения генерации они не нужны. Полнота рендера и
игровое прохождение этим стендом не оцениваются.

## Зелёный результат и регресс

tests/minimap_identity_check.cpp/.pro связывает объекты свежего production
Release с собственным изолированным main; подменяется только lock-store на
существующий тестовый вариант EI_MAPPER_LOCK_STORE_TESTING. Production-функции
идентичности, открытия, миникарты и потребителей не заменяются.

| Открытие | 280 | 281 |
|---|---|---|
| Явная gz1g + zone1.mob | bz1g/brief, скрыта, генерация запрещена | gz1g/game, видна, генерация разрешена |
| gz1g → bz1g + basecam.mob | bz1g/brief, скрыта | bz1g/brief, скрыта |
| Возврат gz1g + zone1.mob | снова ошибочная bz1g | gz1g/game, видна, генерация разрешена |
| Без явной зоны + zone1.mob | ошибочная bz1g | gz1g, MOB совпадает |
| Без явной зоны + basecam.mob | bz1g | bz1g, MOB совпадает |
| World, без явной зоны + zone1.mob | gz1g | gz1g |
| World, без явной зоны + basecam.mob | bz1g | bz1g |

Оба финальных прогона: **42 PASS, 0 FAIL, exit0**, native visible windows=0.
Для генерации учитывается как активный таймер, так и уже выполняющийся capture:
первоначальный вариант теста ошибочно считал работающий capture запретом из-за
неактивного таймера; уточнён тест, production-код не менялся.
Сравнение состояний настоящей bz1g и обоих world-вариантов совпало по всем
проверяемым полям; только время нахождения в capture не сравнивается.
Evidence: red/result.json, green/result.json, behavior-diff.json, consumers.json,
red/process.log, green/process.log в каталоге задачи.

## Все потребители m_mapTestEntry: поведение gz1g до → после

Поиск всех source/*.cpp нашёл 51 обращение в 26 функциях двух файлов.
Ниже перечислены все функции, включая записи и ветки, где поведения не меняется.
Изменения значений и доступных действий отделены от фактического исполнения:
регистрацию ресурсов, ремонт, сохранение коллекций и запуск игры тест не выполняет.

| Функция | До → после на gz1g |
|---|---|
| refreshMapTestContext | bz1g/brief/basecam → gz1g/game/zone1; skyType пустой и allodId=gipat неизменны; обновление неба получает те же значения |
| populateEntranceCombo | 1 вход briefing (187.5,117.5) → 6 входов gz1g: (54.7,138.6), (187.5,106.6), (259.6,275.1), (342.0,205.1), (293.7,222.4), (349.5,31.0) |
| updateEntranceOverlays | активные прямоугольники и подписи принадлежат gz1g вместо bz1g; соседние зоны отображаются по прежнему алгоритму; R2 mapEntranceSources не возвращён |
| eventFilter | проверка наличия зарегистрированной зоны остаётся true; выбор точки использует выбранный игровой вход вместо briefing, если включён picker |
| setCustomSpawnPicking | зарегистрированная зона по-прежнему требует вход; источником служит один из 6 входов gz1g вместо входа bz1g |
| currentMapContextKey | суффикс общего пути MPR: bz1g → gz1g; временная точка и кэш миникарты получают правильную область действия |
| preserveCustomSpawnAfterDocumentReplacement | проверка переноса/диагностика точки использует gz1g; алгоритм и координаты не меняются |
| updateMapTestAction | исчезает запрет Play from Here/Godmode по brief; на реальных файлах игры hasAutoSpellMob: false → true. В минимальном профиле кнопки и запуск остаются disabled из-за отсутствия точного runtime manifest; guard не ослаблен |
| mapTestStagingMob | ожидаемое имя basecam → zone1; реально выбранный MOB zone1.mob до/после одинаков; после выбирается непосредственно по правильному имени |
| launchCurrentMapTest | локальная копия entry получает gz1g/game и игровые входы вместо bz1g/brief; autorun/custom-spawn допускаются при остальных штатных условиях. Физический staging MOB остаётся zone1.mob, точные runtime/валидационные guards неизменны; игра не запускалась |
| scheduleViewportMinimapRefresh | снимается ранний briefing-return, разрешён таймер/генерация |
| refreshViewportMinimapSources | миникарта видна, имя текстуры пустое → zone1map, контекст экспорта gz1g; приватный стенд без textures.res использует generated preview |
| refreshViewportMinimap | briefing guard больше не блокирует capture gz1g; настоящий bz1g продолжает блокировать |
| generateMinimap | вместо сообщения о недоступности briefing доступен штатный экспорт с suggestedName=zone1map/zoneId=gz1g; сам диалог не запускался |
| registerExportedMinimap | возможная регистрация направлена в gz1g вместо bz1g; записи в texts.res не выполнялись |
| chooseBorrowedMapIdentity | ручной выбор по-прежнему явно заменяет entry; логика и меню не меняются, для корректно распознанной gz1g заимствование не требуется |
| refreshActiveCatalog | previousEntry теперь gz1g; следующий refresh сохраняет правильную зону, исходные physicalMismatch/unsaved guards неизменны |
| openDiplomacy | дополнительный источник из каталога basecam.mob → zone1.mob, совпадающий с открытым игровым MOB; NPC briefing больше не примешиваются через ошибочную идентичность; запись не выполняется |
| mobFamilyValidationRequest | familyLabel bz1g → gz1g, mappedPrimaryMob пустой → zone1.mob; seedMobPaths и preferred MOB остаются zone1.mob; includeProfileWideMobs=false и явная композиция сохранены |
| nidAllocationContextDigest | в ключе basecam.mob → zone1.mob; инвалидируется устаревший кэш NID; открытые документы и диапазоны не меняются |
| reviewAndRepairNidCollisions | primaryPath basecam.mob → zone1.mob: существующий владелец коллизии в игровом primary теперь корректно защищён от предвыбора перенумерации. NID0, blockers и необходимость явного Apply сохранены; ремонта не было |
| newCollection | предлагаемые mapZoneId и имя bz1g → gz1g; MPR и список открытых MOB прежние; коллекция не создавалась |
| saveCollection | записываемый при явной команде mapZoneId bz1g → gz1g; пути и набор MOB не меняются; команда не выполнялась |
| createTravelEditor, callback редактирования map.txt | fallback uniqueZone при пустом currentZoneId bz1g → gz1g; явный currentZoneId сохраняет приоритет |
| refreshTravelContext | travelZone bz1g → gz1g, редактор переходов привязывается к игровым deploy/remove; проверка физического MPR сохранена |
| registerTravelMap | предлагаемый ID bz1g → gz1g, allod=gipat прежний; имя MOB берётся из того же zone1.mob, регистрация не выполнялась |

CAM-панель отдельно проверена: refreshCameraContext выбирает по всем совпадающим
MPR, а не m_mapTestEntry, и до/после сохраняет сообщение о разных камерах зон.
Ухудшения относительно280 в затронутом поведении не обнаружено; остальные
дефекты и визуальные изменения278 не исправлялись.

## Сборка, изоляция и передача

Свежий git archive, source tree до завершающих правок только теста/документации:
`3209b6295d5231632fb86d564c2929a52f557c7f`.
Release Qt5.12.12/MinGW7.3 x64; сборка через
`C:\GOGGAM~1\maped\EI-MAP~2`, без subst/junction. Финальный архивный .pro
не менялся; конфликт MSYS shell устранён только окружением дочернего процесса
(SHELL=cmd.exe, PATH Qt/MinGW/Windows). Первая попытка baseline потребовала
build-only system_quote вместо shell_quote в архивной .pro; source/280 оставлен
побайтно исходным. Полный финальный build_release.cmd пересобрал MCP и оба x86
runtime-компонента из того же archive; работающие мосты не запускались/не менялись.

Стенд перенаправляет QSettings, writable QStandardPaths, временные файлы,
backup, discovery и lock-store только в каталог задачи. Фильтр скрывает каждое
верхнее окно до показа; desktop/computer-use, clipboard и снимки не использовались.
Проверка hasAutoSpellMob читает наличие SpellAddonX.asi/AutoRunSpellAddon123.mob
и SPELLADDON.INI в игре, не загружая DLL и не создавая игровой процесс.

Staging: `build-staging-281-minimap-identity-20261106`, новый отдельный каталог.
Raw/staged --version281/exit0 и SHA256 EXE совпали:
`be81277ac495ce69e0d06023319f25eb77ade7585a3f14c271ab9faec1af8ee5`.
Первый финализатор:192→192/0 потерь PASS; после добавления отчёта281
окончательный состав192→193/0 потерь, протокол finalize.log.
Квитанция work/minimap-identity-281-20261106/package-identity.json и finalize.log.
После финализации staging не запускать. Установка280 не изменяется.

Незакоммиченные owner-файлы сохранены; ROADMAP добавляет только собственный пункт
и закрывает только OPEN про миникарту gz1g. OPEN по150% и посторонние строки
остаются прежними. В коммит не входят owner-render/test/QM/.mcp.json, отчёты277
и rebuild64 probe. Сведения сохранности: protected-before.json и preservation.json.
Четыре тестовые копии удалены после PASS по SHA и точным путям, без рекурсивного
удаления; три опустевших каталога удалены. cleanup-plan.json и cleanup-result.json
подтверждают remainingFixture=false; протоколы и рабочие сборки сохранены.
Ручная визуальная и игровая приёмка остаётся владельцу. STOP BEFORE INSTALLATION.
