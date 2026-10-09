# EI-WORLD-BRUSH-SWAMP-SWITCH-130 — 2026-10-10

Поручение владельца через сопровождающего Claude. Планируемая версия 130;
VERSION оставлен 128. Работа в общем checkout `C:/GOG Games/maped/ei-mapper-ataman`,
ветка main, без подагентов, веток, worktree, сетевых действий, установки
зависимостей и операций записи в Git. Постоянная установка не изменялась.

База на старте: `33d5c29f6cbca25483dcc589a0efa43ad3db42a7`.
AGENTS.md прочитан с UTF-8 и повторно проверен по SHA256:
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Чужие изменения и общие ROADMAP/CHANGELOG/VERSION/Readme/translations сохранены.
Во время параллельной работы HEAD изменился сопровождающим; наблюдавшийся
SHA `1f3c4367ac706fe95230de5473cb802d237060c8`; при подготовке передачи —
`ef662f74b49f884c60c2217794bf6ca9e28bf3d4`. Это не коммиты этой задачи.

Прочитаны CONTRIBUTING, PROJECT_CONTRACT, TEST_BUILD_HANDOFF,
PARALLEL_THREAD_WORKERS, технические команды BUILDING_ENDURANCE.
Навыки evil-islands-modding и evil-islands-private-engine-reference недоступны.
Приватные исходники движка не использовались; исследован обычный код проекта.

## Результат и границы

- SWITCH-ATLAS: воспроизведён и исправлен конкретный путь потери владения
  поздним рабочим атласом. Сохраняются отказ при недоступном исходном RES и
  отказ при несохранённых изменениях анимации. Правка только в разрешённом
  `source/render/view_world_terrain.cpp`.
- SWAMP: синтетически воспроизведён механизм активной поверхности под землёй,
  а также проверена нормальная активация новой поверхности. Причина исходного
  пользовательского мазка не установлена. Высоты и алгоритм активации не
  изменены: автоматически поднимать существующее покрытие было бы правкой
  авторских данных без подтверждённой причины.
- Тестовая сборка не является финальной сборкой версии 130, установкой,
  интерактивной визуальной или игровой приёмкой.

## Исходные доказательства

Пункты ROADMAP `EI-WORLD-BRUSH-SWAMP` и `EI-WORLD-BRUSH-SWITCH-ATLAS`
фиксируют сообщения от 2026-09-20. Связанные отчёты:
[ATAMAN_WORLD_BRUSH_40.md](ATAMAN_WORLD_BRUSH_40.md),
[ATAMAN_WORLD_BRUSH_WIP.md](ATAMAN_WORLD_BRUSH_WIP.md),
[ATAMAN_LANDSCAPE_PANEL_43.md](ATAMAN_LANDSCAPE_PANEL_43.md).
Первые два подтверждают исходную общую кисть и пределы тогдашних проверок;
последний оставляет SWAMP/SWITCH-ATLAS открытыми.
В этих доказательствах нет координат и SEC-состояния исходного мазка Swamp
или полной последовательности действий перед отказом смены zone9.
Не подменяем эти отсутствующие сведения синтетической реконструкцией.

## SWITCH-ATLAS: условия, первое неверное состояние, исправление

Ожидаемое поведение: переключение карты в сохраняемой общей сессии удерживает
канонический рельеф вместе с используемым рабочим атласом и его исходными
страницами; последующее открытие восстанавливает новые слоты. При невозможности
сохранить эту пару операция должна отказать, сохранив живые данные.

Восстановленная цепочка:

1. `CWorldTerrainSession::load()` создаёт документ с terrain/materials, но
   без atlas. Это нормальное начальное состояние общей сессии высот.
2. Последующее локальное рисование/ремонт может добавить artwork в активный
   `m_workingTerrainAtlas` и ссылку на него в `m_terrainEdit`, не создавая
   world paint snapshot. Синтетический тест использует настоящий
   `importPhysicalTile()` и `CTerrainEditModel::apply()`.
3. `syncWorldTerrainActive()` переносит terrain, но atlas/materials обновляет
   только внутри `if(doc->atlas)`. Первое неверное retained-состояние:
   terrain уже использует новый слот, а `doc->atlas` по-прежнему пуст.
4. `resolveWorldTerrainClose(true)` получает ownsAtlas=false и вызывает
   `buildValidatedLandscapeCandidate()` без `terrainAtlasResource`.
   Этот вызов корректно отказывает с `mpr.test.atlas.destination`: связанный
   RES действительно не может быть передан таким вызовом.

До правки `switch-before.txt`: 2 PASS (служебные init/cleanup), 1 FAIL,
0 SKIP; ошибка именно на ожидании успешного retain. Предшествующие проверки
пустого retained atlas и кода `mpr.test.atlas.destination` успешно выполнены.

Исправление: при retain активного retained-документа, который использует
несохранённые artwork, сначала вызывается существующий
`ensureWorldTerrainAtlas()`. Он удерживает активный атлас, материалы, профиль,
архив и читает полный набор исходных страниц. При отказе atlasReady=false
срабатывает прежний диалог отказа. Кандидат без выхода RES не используется
для принадлежащего общей сессии атласа. Ограничение на анимацию сохранено.
Для обычных высот без нового artwork регистрация RES не добавлена.

Регрессии проверяют retain/восстановление слота и изображения, отказ с
недоступным архивом, сохранение прежнего пути высот без архива, отказ с
несохранённой анимацией. MPR и RES на диске остаются побайтово неизменными.

Для первоначального сеанса zone9 подтверждён непосредственный отказ,
но конкретное действие, создавшее это состояние, исторически неизвестно.
Тест доказывает один достижимый механизм, а не восстанавливает утраченный сеанс.

## SWAMP: условия и синтетическая проверка

Исходное сообщение: zone3obr, общая кисть, «Добавить поверхность», Swamp,
MPR-материал поверхности 0 (вода), видимого результата нет.
Для неактивной клетки ожидается создание покрытия над землёй. Для уже
активной клетки контракт текущего Add Surface сохраняет геометрию, меняя
выбранные текстуру и материал поверхности. Отказ всей общей транзакции должен
оставлять данные неизменными и передавать сообщение через updateMsg.

Тестовые MPR создаются `buildNewCanvas()` из дистрибутивной синтетической
`tests/fixtures/runtime/particle-flat-7x5.mpr`, скопированной в собственный
`build-tests-130-brush/fixture.mpr`. Живые карты/ресурсы не использовались.
Синтетическая таблица поверхностей задаёт материал 0 с типом eTerrainWater
и opacity=1. Атлас задаёт Swamp с runtime terrain type 14; это отдельное
значение, не номер и не тип MPR-материала поверхности.

Четыре сценария проходят через настоящий `paintWorldTerrainAt()`:
Modern/Swamp и Manual/тот же физический слот, каждый на неактивной и уже
активной заглублённой клетке. Проверены материал 0, выбранный packed slot,
изменённые высоты SEC, GPU-загрузка сектора, Undo/Redo всей world snapshot
и побайтовая сохранность файлов источника.

| Вход | Земля | Покрытие после мазка | Результат |
|---|---:|---:|---|
| Неактивная клетка, raw land=30000, maxZ=100 | 45.777065 | 45.798428 | Новое покрытие выше земли |
| Активная клетка, raw overlay=10000 | 45.777065 | 15.259022 | Текстура Swamp записана, исходная высота сохранена |

Цепочка: texture family и liquid material выбираются отдельно; world painter
передаёт оба в `makeOverlayActivation()`. В terrain_edit_model.cpp определяется
`wasActive`. Только `!wasActive` добавляет клетку в activatedCells и инициализирует
вершины из земли с clearance. Существующие активные вершины сохраняются.
В заглублённом сценарии первое непригодное для видимой поверхности значение
уже во входе: raw overlay=10000 < raw land=30000. Это не ошибка новой активации.
Смена текстуры сама по себе не исправляет такое исходное расположение.

Не доказано, что исходный мазок zone3obr попадал на активную заглублённую
клетку. Не выполнена оценка картинки по пикселям framebuffer и не проведена
игровая приёмка. Успешная загрузка GPU-сектора не означает визуальный PASS.

### Конкретные оставшиеся гипотезы и следующие шаги SWAMP

1. **Активное покрытие ниже земли.** Для точных координат мазка снять до/после
   overlayMaterialIndex, packed tile и raw heights всех 3×3 вершин клетки.
   Сопоставить со значением maxZ. Если tile/material изменились, но покрытие
   ниже земли, причина подтверждается; отдельно согласовать нужное поведение
   Add Surface для существующей поверхности, не менять высоты вслепую.
2. **Отказ общей транзакции.** Зафиксировать updateMsg конкретного мазка,
   enabled targets, выбранные family/runtime type/slot и доступность исходных
   страниц атласов. Если payload до/после одинаков и есть ошибка staging,
   исследовать первый отказ соответствующей карты, а не высоту покрытия.
3. **Данные применились, отображение скрыто.** При успешной транзакции и
   покрытии над землёй проверить overlaysVisible, opacity выбранного MPR
   материала, фактический tile/texture и результат previewWorldPaintState.
   Только затем исследовать рендер в view.cpp; этот файл здесь не изменялся.

## Сборка и проверки

Qt 5.12.12 MinGW 7.3 x64 из уже настроенных EI_MAPPER_QT_BIN/MINGW_BIN;
toolchain не менялся. Все outputs и TEMP/TMP в build-tests-130-brush.
Стандартный release wrapper здесь не использован: он пишет translations/*.qm,
что запрещено владением. Отдельный tests/*.pro включает производственные
исходники/ресурсы и заменяет main только в тестовом target. Номер сборки 128.
Никакая новая опция не требует регистрации в source/app/main.cpp.

- Новые native случаи: **8/8 PASS, 0 FAIL, 0 SKIP**.
  QtTest суммарно **24 PASS**, из них 16 init/cleanup и 8 содержательных случаев.
  [Логи случаев](../../build-tests-130-brush/),
  [общий вывод](../../build-tests-130-brush/focused-final.txt.stdout).
- Регрессия `world_terrain_sculpt_tests`: **41 PASS, 0 FAIL, 5 SKIP**,
  [результат](../../build-tests-130-brush/sculpt/results.txt).
  SKIP — optional installedZone2OffsetOnlyReadOnly, installedZone11Zone12ReadOnly,
  installedZone6Zone4QuarterShiftReadOnly, installedZone11Zone12Zone13ReadOnly,
  installedTripleReadOnly. Эти проверки установленных карт не выполнены.
- Production `--scenario-layer-check`: **50 PASS, 0 FAIL**, exit 0, выполнено
  под ui-tests.lock, [вывод](../../build-tests-130-brush/scenario.txt.stdout).
- Исходный отказ: [switch-before.txt](../../build-tests-130-brush/switch-before.txt).
  Проверенная сборка: [build-catalog-fixture.log](../../build-tests-130-brush/build-catalog-fixture.log).
- Все GUI-запуски тестов сериализованы атомарным каталогом
  build-locks/ui-tests.lock с owner.txt; блокировка снимается finally, чужая не
  удаляется. Редактор/игра интерактивно не открывались.

SHA256 проверенного `build-tests-130-brush/release/world_brush_130_integration.exe`:
`EFBA7233F7A2CB504BBA15B84D0B5890A45838F856F380D6C7CE65A3D35787B4`.
SHA256 сохранённого EXE до исправления (`world_brush_130_before.exe`):
`C16A35D75CAE7A0A0D83512F9D4611392D7433F3C9BEE3F4B0731E41AF79594E`.
Хеши проверенных исходников своей задачи:

- view_world_terrain.cpp: `AD26A00D7945288797606C6087D3D77B6ED1886C9CAE30125E8B9292386C2F1E`.
- world_brush_130_integration.cpp: `3578978E566CA88A07551DA76A29C3785012FCFCBFA3BFE155E556813F8FFF96`.
- world_brush_130_integration.pro: `5B7C4FDE5453E422B9F1A8FEE28553FEEF3F85D89CA516EAFEFA04FD9469B409`.

Для повторения сборки — qmake tests/world_brush_130_integration.pro из собственного
shadow-каталога, затем mingw32-make release. Запускать из build-tests-130-brush,
с EI_TEST_TERRAIN_FIXTURE, TEMP/TMP внутри этого каталога, под описанной блокировкой.
Полный набор — world_brush_130_integration.exe без аргументов; scenario — тот же
EXE с --scenario-layer-check (неизменённый производственный entry point).
Финальную программную сборку, новый номер и установку выполняет сопровождающий.

Тестовая программа без аргументов запускает каждый из восьми случаев отдельным
дочерним процессом: CLandscape и legacy operation manager имеют process singleton
lifetimes. Это предотвращает повторное создание/разрушение редакторского контекста
в одном QtTest-процессе. Конкретный случай можно запустить по имени QtTest.
Для нативного запуска требуются PATH toolchain и QT_PLUGIN_PATH его plugins.
Изначальная попытка без plugin path завершилась до тестов (0xc0000602), затем
путь исправлен. Неполный тестовый каталог без runtime family и первоначально
полный capacity=512 также исправлены в фикстуре, без ослабления проверок программы.
В скрытом native GL есть предупреждения swapBuffers для non-exposed window;
контекст, загрузка atlas/sector и данные проверены, показ кадра не заявляется.

## Точный список файлов для коммита сопровождающим

1. `source/render/view_world_terrain.cpp` — изменён.
2. `tests/world_brush_130_integration.cpp` — новый.
3. `tests/world_brush_130_integration.pro` — новый.
4. `docs/validation/ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md` — новый.

Каталоги сборок/фикстуры/логи/EXE и чужие изменения в коммит не включать.
Правок вне владения для подтверждённого исправления не требуется.

## Текст для ROADMAP (общий файл не изменён)

```markdown
## EI-WORLD-BRUSH-SWAMP-SWITCH-130 — диагностика Swamp и удержание атласа

- Поручение 2026-10-10 через Claude; база main 33d5c29f6cbca25483dcc589a0efa43ad3db42a7.
  Разрешены назначенные исходники/тесты/отчёт; Git, общий VERSION и установка — сопровождающему.
- SWITCH-ATLAS: воспроизведён late local atlas в сессии высот: terrain использует
  новый слот, sync оставляет retained atlas пустым. Перед retain теперь регистрируется
  полный рабочий атлас с исходными страницами. Недоступный RES и pending animation
  по-прежнему отклоняются; обычные высоты не требуют новой регистрации RES.
- SWAMP: 4 синтетических native случая Modern/Manual подтверждают корректную новую
  активацию и сохранение высоты уже активного покрытия под землёй. Причина исходного
  zone3obr не установлена; высоты не менялись. Конкретные дальнейшие гипотезы в evidence.
- Проверки: 8 новых native случаев PASS без SKIP; scenario 50 PASS/0 FAIL;
  world terrain 41 PASS/0 FAIL/5 optional SKIP.
- Evidence: docs/validation/ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md.
- Статус: SWITCH-ATLAS исправление проверено синтетически; SWAMP открыт для исходного
  мазка. Следующий шаг: интеграция сопровождающим, финальная версия/сборка/установка,
  ручная смена карты в исходном сценарии; для SWAMP — координаты, updateMsg и высоты до/после.
```

## Текст для CHANGELOG (общий файл не изменён)

```markdown
## 130 — EI-WORLD-BRUSH-SWAMP-SWITCH-130

- Исправлено удержание карты общей сессии после локального появления нового
  рабочего атласа: рельеф, атлас и его исходные страницы теперь удерживаются вместе.
  Проверки недоступного RES и несохранённой анимации сохранены.
- Добавлены 8 native регрессионных случаев. Синтетическая проверка Swamp подтверждает
  активацию над землёй и сохранение высот существующих покрытий; причина исходного
  невидимого мазка zone3obr остаётся открытой, алгоритм высот не изменён.
- Проверки: 8/8 native PASS, 0 SKIP; scenario 50 PASS/0 FAIL;
  world terrain 41 PASS/0 FAIL/5 optional SKIP.
  Подробности: docs/validation/ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md.
```

## Передача 129

EI-FINALIZE-129, 2026-10-10, отдельное поручение владельца через Claude.
Начальная база main и источник сборки:
`b8ca24bb983bfd01b3d99bb9878b122e017cc263`, VERSION=129.
AGENTS.md проверен: SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`,
правила не менялись. Git использован только для чтения/архива;
ветки, worktree, коммиты, подагенты, сеть и установка зависимостей не выполнялись.
Общие ROADMAP/CHANGELOG/VERSION/Readme/translations в рабочем checkout не изменены.
Во время передачи сопровождающий изменил HEAD на
`e13033fb4cf84e82e9a9b0d43920956a0d858ef6`; это не источник сборки129.

### Изоляция и сборка

Снимок создан `git -c core.autocrlf=false -c core.eol=lf archive` указанного
коммита, распакован Python tarfile в `build-release-129-final/src`.
Все **1782 Git blob**, полный список файлов и VERSION129 проверены до сборки;
после сборки **0 изменённых blob / 0 дополнительных файлов**.
Доказательства: [snapshot.json](../../build-tests-129-final/snapshot.json),
[snapshot-after-build.json](../../build-tests-129-final/snapshot-after-build.json).
Начальная распаковка системным tar искажала кириллические имена, а первоначальный
archive использовал CRLF; эта копия сохранена как
`build-release-129-final/src-tar-encoding-unused` и не использовалась для сборки.
Начальная проверка ожидаемого нового QM после сборки оказалась слишком строгой:
QM уже tracked и остался byte-identical; фактический результат — неизменное дерево.

Свежая shadow Release: `build-release-129-final/release`, стандартный wrapper
из снимка, Qt5.12.12/MinGW7.3 x64, оба x86 runtime-компонента пересобраны;
exit0, 146.78с. [Лог](../../build-tests-129-final/release-build.out).
EXE `--version`: `EI Mapper Endurance Edition 129`, exit0.
SHA256 свежего, протестированного, staged и установленного EXE совпадает:
`B70F56E4F8D0B2E187E5469923644613CDA81B0CC41BCE4E07D0B767D9245CA5`.

### Регрессы

Все проверки с UI/native GL выполнялись последовательно под атомарным
`build-locks/ui-tests.lock`, owner.txt=EI-FINALIZE-129; блокировка снималась finally.
TEMP/TMP и рабочие данные проверок находятся в собственном build-tests-129-final.
Редактор и игра интерактивно не запускались; живые карты/игра/мод не менялись.

| Проверка | Результат | Доказательство |
|---|---|---|
| Полный --ui-widget-check под --guard-image-child | **UNAVAILABLE**, guard exit2 до запуска UI и до записи буфера: несохраняемый native формат49470; 0 выполненных проверок, не PASS | [stderr](../../build-tests-129-final/ui/check.err), [result](../../build-tests-129-final/ui/result.json) |
| --scenario-layer-check | **50 PASS, 0 FAIL, 0 SKIP**, exit0 | [result](../../build-tests-129-final/scenario/result.json) |
| --dark-theme-check | **70 PASS, 0 FAIL, 0 SKIP**, exit0 | [result](../../build-tests-129-final/dark-theme/result.json) |
| world_brush_130_integration, собран из снимка129 | **8/8 содержательных случаев PASS**, QtTest24 PASS включая16 init/cleanup, 0 FAIL/0 SKIP, exit0 | [stdout](../../build-tests-129-final/brush/check.out) |
| world_terrain_sculpt_tests, собран из снимка129 | **41 PASS, 0 FAIL, 5 optional SKIP**, exit0 | [results](../../build-tests-129-final/sculpt/results.txt) |

Пять SKIP относятся к optional installedZone2OffsetOnlyReadOnly,
installedZone11Zone12ReadOnly, installedZone6Zone4QuarterShiftReadOnly,
installedZone11Zone12Zone13ReadOnly, installedTripleReadOnly; они не выполнены.
Буфер пользователя сохранён. Предложено скопировать обычный текст для повторного
UI-check; к моменту передачи подтверждения изменения буфера не получено.
Защита не ослаблялась. Недоступность UI явно внесена в changelist пакета.
Причина SWITCH-ATLAS и границы SWAMP остаются описанными выше:
исправлено владение поздним рабочим атласом; исходный невидимый мазок zone3obr
не объяснён окончательно, алгоритм высот существующего покрытия не менялся.

### Staging и постоянная установка

Новый staging: `build-staging-129-final-handoff`, создан заново.
Qt/runtime deployment-файлы и синтетические fixtures взяты из чистого staging128
с проверкой всех127 старых manifest hashes; EXE, оба пересобранных runtime,
VERSION/Readme заменены из сборки/снимка129, CHANGELOG содержит только129,
результаты и короткие ручные шаги. Проверены staged --version и идентичность EXE.
Финализатор из снимка выполнен **только на staging**, exit0, его внутренний
distribution validator **PASS**,127 payloads; после finalizer пакет не запускался
и не менялся. [Finalizer](../../build-tests-129-final/finalizer.out),
[staging-result](../../build-tests-129-final/staging-result.json).

Постоянная установка обновлена **128→129**:
`artifacts/EI-Mapper-Ataman-2-first-test`.
Host-level process guard перед подготовкой и непосредственно перед заменой
подтвердил отсутствие процессов редактора/runtime.
Все127 старых managed hashes совпали с manifest128, конфликтов не было.
Заменены ровно6 managed файлов:

- `CHANGELOG.md`
- `EIMapperEndurance.exe`
- `Readme.md`
- `VERSION`
- `runtime/EIMapperEnduranceJmvBridge.plg`
- `runtime/EIMapperEnduranceRuntimeHost.exe`

`SHA256SUMS.txt` заменён последним; файлы копировались через проверенный временный
файл с atomic os.replace. **127/127 установленных managed hashes + manifest PASS**.
**22433/22433 unlisted пользовательских файлов сохранены побайтово**,
включая настройки, карты, логи и кэши; списки до/после совпадают.
Finalizer и проверки, создающие пользовательские данные, на установке не запускались.
SHA256 manifest129:
`6D08B53C84547CF6DDF95B72F170CD0DCA8F68B7CBD310223E183E4EAAFE5631`.
[Install result](../../build-tests-129-final/install-result.json),
[план](../../build-tests-129-final/install-plan.json),
[hashes до](../../build-tests-129-final/user-files-before.json),
[hashes после](../../build-tests-129-final/user-files-after.json).

Rollback128: `artifacts/rollback-EI-Mapper-Ataman-v128-to-129-20261010`,
ровно заменяемые6 файлов и прежний manifest, **7/7 backup hashes PASS** до замены;
[rollback hashes](../../build-tests-129-final/rollback-hashes.json).
Восстановление не потребовалось. Старый rollback127
`artifacts/rollback-EI-Mapper-Ataman-v127-to-128-20261009` сохранён:
он вне явно разрешённого владения `artifacts/rollback-*129*`.
Удаление только этой подтверждённой старой генерации по правилу одной завершённой
генерации остаётся шагом сопровождающего; самостоятельно границы не расширялись.
ZIP не запрошен и не создавался.

### Передача сопровождающему

Сборка и обновление выполнены; полная автоматическая UI-приёмка остаётся
недоступной по указанному guard. Ручная смена карты с новым атласом,
визуальная/игровая приёмка и исходный SWAMP остаются открытыми.

**Точный список файлов для коммита этой задачи — один изменённый файл:**
`docs/validation/ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md`
(только добавленный раздел «Передача129»).
Новых source-файлов для коммита нет; build/staging/install/rollback и приватные
инвентаризации — инженерные артефакты, в Git не включать.

Текст для ROADMAP (общий файл не изменён):

```markdown
- EI-FINALIZE-129, 2026-10-10: Release129 собрана из точного снимка main b8ca24bb983bfd01b3d99bb9878b122e017cc263,1782 blobs до/после совпадают. Scenario50/0, dark-theme70/0, native8/8 (QtTest24/0), world terrain41/0/5 optional SKIP. Полный UI-check UNAVAILABLE: guard остановлен native clipboard49470 до записи. Staging/finalizer127 managed PASS; постоянная установка128→129,127 managed+manifest PASS,22433 user files byte-identical, rollback128 проверен. Evidence: docs/validation/ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md, раздел Передача129. Следующий шаг: повторить полный UI-check после сохраняемого буфера, вручную принять смену карты/атлас; сопровождающему удалить старый rollback127 вне владения исполнителя. Исходный SWAMP и прежние родительские цели не закрыты.
```

Дополнение записи CHANGELOG129 для сопровождающего (общий файл не изменён):

```markdown
- EI-FINALIZE-129: выполнены свежая Release из коммита b8ca24bb983bfd01b3d99bb9878b122e017cc263 и передача128→129 с сохранением22433 пользовательских файлов и проверенным rollback128. Scenario50 PASS/0 FAIL, dark-theme70/0,8 native cases PASS, world terrain41/0/5 optional SKIP; clean staging127 hashes PASS. Полный UI-check недоступен из-за несохраняемого native clipboard49470, guard остановил запуск до записи; ручная приёмка остаётся открытой. Evidence: docs/validation/ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md, Передача129.
```
