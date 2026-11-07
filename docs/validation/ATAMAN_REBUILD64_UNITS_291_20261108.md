# Ataman291 — EI-REBUILD64-UNITS-291, 2026-11-08

Результат: принятый владельцем перенос персонажей объединён в main поверх
`65b525c7cad2a70f4a8faed5411211b21449978e`. VERSION291. Подготовлен staging;
установки и push нет. Общая миграция всех функций остаётся открытой.

## Слияние с288–290

- ABI14 един для EXE/DLL: output288, query/depth и service289, terrain290 сохранены;
  добавлены semantic appearance и независимый appearance preview.
- CPU-пикер `unit_pick289.inc` из handoff исключён. Персонажи и stst8 используют
  общий native stencil ID/depth; для нового щелчка по анимированному объекту
  запрашивается актуальный кадр. Второго конкурирующего результата клика нет.
- UNIT поступает с настоящими частями, экипировкой, составной текстурой тела,
  idle ANM/ADB и комплекцией. Пять логических слотов остаются в CPU-документе;
  общий перенос/Undo сохраняет их все. MCP edit_units и unit-only edit_objects
  используют ту же валидируемую команду. Форматы MOB/MPR не менялись.
- Подсветка, рамки, активный логический слот и точки идут через worker service289.
  GL-рисунок выбранного персонажа поверх native-мира исключён.
- DB placement согласован с288: снят оставшийся в новой форме запрет Creature.
  Основной вид остаётся Rebuild64. Native preview получает тот же завершённый
  состав частей/текстур; его rest pose не подгоняется под GL. Если внешности
  недостаточно, персонажный preview оставляет сообщение, основной вид не меняется.
  CPU-просмотр текстуры персонажа также не переводит мир на GL.
- UNIT получает абсолютную позу от общего CPU support resolver, включая authored
  мосты. Вторичная terrain-проекция UNIT в DLL исключена; после мазка290 приходит
  актуальная CPU-поза. Для stst8 явно передаётся та же terrain-relative поза.
- Vendor040–042 перебазированы после020/021/060/009/010; порядок и все SHA в
  source.lock.json проверены в рабочем дереве и index. Патчи LF. Новых шейдеров,
  изменений HLSL или таблицы638 нет. Готовые shader resources побайтово равны290.
  Имена units289.inc/check.inc сохранены как происхождение handoff, ID задачи291.

## Скрытая проверка и существенное исправление

Данные заморожены **один раз**:294 настоящие копии,6559772131 байт, включая
Lua tables.lua и отдельный authoring pack маппера. Живые файлы читались только
во время копирования. Все прогоны используют один frozen-корпус. После каждого
процесса его хеши проверены: изменений0. Проверялась настоящая копия staging;
перед первым запуском совпали1366 управляемых SHA. Computer-use, видимых окон,
clipboard, subst/junction, запуска установленного маппера и остановки чужих
процессов не было.

Это **не единый успешный полный прогон окончательного EXE**. Начат один
согласованный сценарий zone1/zone3obr; после остановок использованы контрольные
точки, ранее пройденные действия не повторялись целиком. Всего10 собственных
процессов, включая остановленные проверки. Полные статусы и ошибки сохранены в
`work/rebuild64-units-merge-291-20261108/evidence/summary.json`; остановленные
проходы не объявлены PASS. Матриц и нового полного обхода функций нет.

Остановки: пропущенный запрет Creature в форме288; отсутствие ожидания асинхронного
replay DB-click; тестовая камера stst8 без видимых фрагментов; попытка мазка до
готовности новой сцены; старые соседские/identity-барьеры общего harness при
выключенных соседях. Исправлялись конкретные setup/await-предикаты, продолжение
начиналось с незавершённой операции. Эти ошибки теста не объявлены дефектами D3D.

Затем обнаружено настоящее падение0xC0000005 при MCP edit_units. Дамп и символы
показали `CView::nativeServiceSnapshot → CMob::logicNodes → isMarkDeleted`:
`CLogic::replaceFromJson` удалял patrol/look-точки, а кэш CMob сохранял их адреса.
Общая CAiObjectCommand теперь обновляет кэш до публикации, также при Undo/Redo.
Native snapshot/tick не читают промежуточную MCP-транзакцию; optional snapshot
делается после снятия aiCommitBusy. Данные и сериализация не обходятся.

Focused-продолжение `case-accept291-mcp-fixed`: **exit0, realClose=true**,
frozen differences0. На zone1 MCP edit_units/Undo и edit_objects, Save As/reopen
с точным MOB; затем оставшийся сценарий zone3obr целиком. Падение не повторилось.
Без повторения уже пройденных DB/preview/sculpt на zone1.

Совокупность доказательств: native-клик по видимому персонажу zone1/zone3obr,
перенос всех5 guard slots и точный Undo, поворот/Undo, добавление из базы,
удаление/Undo, native frame/service, мазок под NPC/изменение его позы без MOB drift
и точный Undo позы/MPR, MCP snapshot, native model preview с экипировкой,
CPU texture preview, сохранение копии и повторное открытие с побайтовым MOB.
Кадры персонажей/расстановки/мазков и обоих model preview просмотрены агентом.
Финальное закрытие штатное; ноль видимых окон, неожиданных диалогов, внешних
Qt-write или чтения live replaceable resources. Во всех Renderer selection
только старт OpenGL→Rebuild64; обратных переходов нет. Числа под параллельной
нагрузкой не гейт и не доказательство ускорения.

## Замечания, которые не закрыты

- stst8 на zone1: выбор объекта документа, перенос и Undo сохраняют Rebuild64.
  Прямой щелчок по его native-фрагменту **не подтверждён**: в диагностическом
  кадре видимая геометрия его закрывает, ID-фрагментов нет. Авторскую расстановку
  ради теста не меняли и рендер сквозь поверхность не включали. Это ограничение
  проверки; причина всей видимости stst8 отдельно не исследована.
- Zone3obr имеет исходную коллизию NID249 основной MOB/Autozone3obrclown.mob.
  validate_map выполнен до правок; MCP mutation этой семьи законно запрещён.
  На zone3obr использован get_unit, на zone1 — настоящий edit_units.
- Полного сквозного повторного прогона последнего EXE нет: после исправления
  падения проверен его конкретный сценарий и незавершённая часть. Остальные
  неизменные успешные проверки сохранены как доказательства по контрольным точкам.
- Независимые preview показывают rest pose, анимация проверяется в основном мире.
  Смешанный UNIT+static MCP набор сохраняет прежний отказ по типам; unit-only работает.
- Перекрывающиеся helper-подписи, вид воды/внешних границ и старые замечания290
  не расследовались заново. Картинка GL не использовалась как художественный эталон.
- Semantic paint/auto_paint и прочие ограничения290 сохраняют REQUIRES_OPENGL;
  это не вызовы двух перечисленных ниже методов. Полное закрытие миграции —
  следующий этап владельца.

## Staging и сохранность

`build-staging-291-rebuild64-units-20261108`, canonical stage/finalize.
Состав против290:1365→1366, удалено0; добавлен только этот отчёт.
QM получен свежим lrelease из clean TS; рабочий QM владельца не использован и не
изменён. Его SHA совпадает с290, поскольку TS не менялся.

- EXE SHA256: `52d385be2033f9fe544d760b060f4fa51626e4e76073f75d1e8cc47582d397db`.
- DLL SHA256: `af868720017fd306770f436f0a16c5e26c668247072057a7bd3fcd3f1280aa2d`.
- QM SHA256: `b7e65aad9692940704b3e16e127dc960d9dede4f41d9df7f311b2fee71cab853`.

Отчёт и BUILD_INFO уточняются после проверки; программные файлы staging остаются
побайтово теми же, что в последнем focused-продолжении. После этого выполняется
только финализация метаданных, без повторного запуска staging.

Чужие ROADMAP-строки, landscape.cpp, render_stall_trace.h, owner-строки view.cpp,
view_neighbor_load.cpp, mob_load_pipeline_check.cpp, рабочий QM, .mcp.json,
отчёт277 и прочее исходное изменённое состояние сохранены и исключены из index
и clean build. Подтверждение — logs/owner-preservation.json. Runtime/fixture,
дамп, кеши и промежуточные сборочные копии удалены:16 собственных путей,
remaining0; cleanup-receipt.json=REMOVED_VERIFIED. Evidence и текстовая
символизация падения сохранены без дампа и игровых файлов. Установка и push запрещены.

## Все оставшиеся useLegacyViewport / requireOpenGL

Номера строк взяты из текущего рабочего main, включая сохранённые незакоммиченные строки владельца в view.cpp.22 прямых вызова; определения/декларации не считаются вызовами. Это реестр для следующего этапа, не проверка всех перечисленных функций.

| Файл и строка | Вызов | Причина |
|---|---|---|
| `source/app/main_window.cpp:9791` | `useLegacyViewport` | Параметры изображения OpenGL |
| `source/render/rebuild64_viewport_widget.cpp:1008` | `requireOpenGL` | Просмотр MOB или коллекции без MPR |
| `source/render/rebuild64_viewport_widget.cpp:1109` | `requireOpenGL` | Лепка с сохранением мировой высоты объектов |
| `source/render/view.cpp:2895` | `useLegacyViewport` | Модель освещения OpenGL |
| `source/render/view.cpp:3067` | `useLegacyViewport` | Каналы изображения OpenGL |
| `source/render/view.cpp:5911` | `useLegacyViewport` | Диагностика теней |
| `source/render/view.cpp:13904` | `useLegacyViewport` | Просмотр профиля атласа |
| `source/render/view.cpp:14118` | `useLegacyViewport` | Диагностика покраски |
| `source/render/view.cpp:15253` | `useLegacyViewport` | Новые страницы рабочего атласа |
| `source/render/view.cpp:18000` | `useLegacyViewport` | Настройки рельефа и воды MPR |
| `source/render/view.cpp:19316` | `useLegacyViewport` | Материалы MPR |
| `source/render/view.cpp:19334` | `useLegacyViewport` | Атлас плиток |
| `source/render/view.cpp:21425` | `useLegacyViewport` | Переходы материалов |
| `source/render/view.cpp:23045` | `useLegacyViewport` | Информация о плитке |
| `source/render/view.cpp:24090` | `useLegacyViewport` | Импорт карты высот |
| `source/render/view_atlas_compact.cpp:20` | `useLegacyViewport` | Уплотнение атласа |
| `source/render/view_atlas_compact.cpp:96` | `useLegacyViewport` | Очистка terrain pack |
| `source/render/view_base_hd.cpp:33` | `useLegacyViewport` | Преобразование Base в HD |
| `source/render/view_canvas_resize.cpp:45` | `useLegacyViewport` | Изменение холста |
| `source/render/view_world_terrain.cpp:182` | `useLegacyViewport` | Одновременная правка нескольких MPR мира |
| `source/render/view_world_terrain.cpp:215` | `useLegacyViewport` | Выбор карт для лепки мира |
| `source/render/view_world_terrain.cpp:1412` | `useLegacyViewport` | Новые страницы рабочего атласа мира |

Связь: `source/render/rebuild64_viewport_widget.cpp:541` — Сигнальная привязка legacyViewportRequested → requireOpenGL(action); единственный общий обработчик.

Определение общего посредника: `source/render/view_legacy_capabilities.cpp:5`; обработчик и диагностика: `source/render/rebuild64_viewport_widget.cpp:789`. Аварийный device/resource fallback и пользовательский переключатель OpenGL сохраняются отдельно от этого списка функций.
