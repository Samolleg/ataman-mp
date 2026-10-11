# EI-TERRAIN-MIXED-EDGE-POLISH

Дата: 2026-10-11. Назначенная база: `main`, VERSION 138,
`57b998d3ee1f7b207cc866ca68879687eca97e49`.
AGENTS.md SHA256: `f4b7fdd5ea82fcdf43d002d23d64ec36be853650f054edf70376527cd6432ec3`.

Результат: реализация проверена и передана сопровождающему без коммита,
назначения новой версии и установки. Проверяемый EXE собран из снимка HEAD
`73b9712da03ab95a16eddbf309017abdb2a632cb` плюс только файлов этого владения.
Между назначенной базой и этим HEAD изменились лишь отдельный отчёт и Python
stage-C прототипы; исходники приложения/VERSION не изменились.
[Снимок и hashes](../../build-tests-mepolish/snapshot.json).

EXE: `build-release-candidate-mepolish/release/EIMapperEndurance.exe`, VERSION 138,
SHA256 `cad322b00ccf1be98e7b3265cea2e958fed769d6b5c83efadd1baab7bbe1cc56`.
Это проверочный raw build, не новая постоянная установка/пакет распространения.

## Изменение и причина

- Однородные источники для тела mixed-edge выбираются среди paint-selectable,
  opaque, non-animated физических вариантов того же класса исходного
  однородного anchor. Вес относится
  к физическому слоту; четыре его rotation не умножают вес. Четыре метки рецепта
  выполняют четыре детерминированных взвешенных выбора для каждого материала.
  Используется сохранённый `randomPaintSeed` модели; для карт без сохранённого
  зерна — канонический 0. Per-stroke nonce не используется в теле mixed-edge.
  Мировой origin участвует в выборе метки. Общий краевой anchor остаётся
  независимым от метки/зерна: полный случайный выбор разных источников прямо
  на общей границе нарушил бы её пиксельное равенство.
  Класс исходной картинки и переданный reviewed класс итоговой клетки
  различаются в некоторых старых профилях: Zone6 Stone/Lime имеет четыре
  uniform источника класса 2, а профиль передаёт класс 0. Выбор картинки
  ограничен классом 2; прежнее правило классификации клетки по coverage/tie
  сохраняется. Профиль/MP классификация не исправлялись этой задачей.
  Ground/Dead: из пяти физических источников выбран только один класса 1,
  четыре класса 4 исключены из вариативного тела. Ownership recovery использует
  весь прежний stock bank для сохранения доверенной RGB fit.
- Причина вытянутых flecks в ownership: прежнее `along * fade` замораживало
  координату вдоль контура на границе клетки. Теперь координата продвигается
  с постоянной плотностью и периодом 128 SD-сэмплов; геометрия и RGBA края
  остаются общими для соседей. Shape labels продолжают менять внутреннюю
  геометрию, а не фазу общего края.
- Причина повторяемой полосы источника: cropping 1/16 с последующим wrap,
  возврат к первому источнику через широкий fade, сдвинутый/повёрнутый
  полупрозрачный альтернативный слой. Полный исходник теперь сохраняется;
  узкий симметричный collar делает противоположные стороны периодическими.
  Тело использует выбранный исходник без смещения/поворота и без искусственного
  brightness modulation; только collar 1/16 связывает его с общим anchor.
- Бюджет: четыре метки на упорядоченную подпись/runtime-класс/зерно,
  а не декартово произведение вариантов материалов. Число меток не растёт.
  Подготовка растров ограничена общим anchor и максимум четырьмя выбранными
  источниками; одинаковые выборы разделяют QImage. Старые слоты/Undo не
  удаляются. Новый `periodic-stock-v2` ключ предотвращает reuse старых
  `crisp-stock-v1` pixels. Ограничение 4096 и атомарное staging сохраняются.
  При переходе v1→v2 соседний generated mixed-edge тайл включается также через
  однородное общее ребро. Authored stock через такое ребро сохраняется:
  расширять обработку на все соседние stock-тайлы нельзя, поскольку это
  затрагивает посторонние corner-only материалы без локального uniform.
- Новая параметризация включается только явным `polishedMaterialEdges=true`
  из `CTerrainMixedEdge::resolve`. Default baker точно сохраняет v1 transport,
  crop, offset/rotation, blending и modulation. Это необходимо для HD138
  воспроизведения старых `crisp-stock-v1` рецептов перед trusted HD replacement.
- Размер генерации — фактический размер исходника. Тестовые HD источники
  извлечены production `inspectMmp` из сохранённого результата пересборки 138,
  а не увеличены из SD: 1216 × 256×256. Синтетический integration check теперь
  использует 256 по умолчанию; fixture может явно задать другой размер.

## Проверки финального результата

| Проверка | PASS | FAIL | SKIP | Evidence |
|---|---:|---:|---:|---|
| Ядро, exhaustive shared-edge grammar + weighted HD | 17451 | 0 | 0 | [log](../../build-tests-mepolish/core-run.log) |
| `--terrain-mixed-edge-check`, synthetic 256 px, map seed 138 | 67 | 0 | 0 | [log](../../build-tests-mepolish/synthetic.log) |
| Тот же check, копия Zone6, трава | 55 | 0 | 0 | [log](../../build-tests-mepolish/grass-after.log) |
| Тот же check, копия Zone6, камень | 55 | 0 | 0 | [log](../../build-tests-mepolish/stone-after.log) |
| Полный `--ui-widget-check` | 169 | 0 | 0 | [log](../../build-tests-mepolish/ui.log) |
| Полный `--scenario-layer-check` | 49 | 0 | 0 | [log](../../build-tests-mepolish/scenario.log) |
| Modern OFF / Classic / Manual, exact bytes vs HEAD138 | 3 | 0 | 0 | [receipt](../../build-tests-mepolish/off-comparison.json) |
| Default v1 baker, 128/256 px × balanced/stable | 4 | 0 | 0 | [receipt](../../build-tests-mepolish/legacy-comparison.json) |

Ядро: **18394 ребра, 2396804 RGBA channel comparisons**, без различий.
Все `4^6` совместимые конфигурации шести вершин проверены по обеим осям; дополнительно
четырёхматериальный HD grid, weighted HD пара независимых областей с мировым
origin, seed-independent boundary bytes, повторяемость и container order.
512 детерминированных map seeds при физических весах 1:3:6 дали 51/139/322
выбора. Нулевые/неполные веса отклоняются. Новая параметризация сохраняет
две чередующиеся ownership строки непосредственно у клеточной границы:
stock row больше не застывает в вытянутую полосу.

Synthetic: **22 mixed/mixed ребра, 22528 RGBA comparisons**, 2 triple и 1 quad;
также 32 стыка с неизменёнными плоскими uniform. Миграционная синтетика
охватывает 32 клетки двух parallel mixed полос через homogeneous edge,
обновляя существующий recipe neighbor и сохраняя authored stock соседа.
В настоящем hidden CView проверены release/1 Undo entry, exact Undo/Redo
SEC/двух версий поля/atlas fingerprint, GL upload и installed picker pixels.
Shared brush refusal оставляет fingerprint неизменным. Проверены animation,
отсутствующий источник, budget refusal и предупреждение >4096 с default Cancel.

Zone6: **128×192 = 24576 клеток**, реальная исходная 6×6 vertex patch,
4×4 painted vertices без undercoat. Для каждого материала **16 mixed/mixed
рёбер, 16384 RGBA comparisons**. Трава: 6 Dark +8 Common uniform источников
одного класса. Камень: 4 Lime; Dead —1 подходящий физический класс.
Оба мазка: **1216→1230 рабочих слотов; 1136 после dense publication**,
как у baseline138 для того же теста — роста слотов в этом случае нет.
Synthetic: 64→82 рабочих, 29 опубликованных. Отдельная 5×5 чистая выпечка
17 recipe slots /16 unique vs 17/17 у исходного алгоритма.
Повтор до сохранения и после fresh disk reopen: **0 новых слотов, 0 SEC deltas**.
Проверены все SEC references, все MMP payloads, exact semantic field, map seed
и каждый recipe alias. Равенство payloads не означает lossless DXT1 edges.
Реальные raw authored uniform края не объявляются общей canonical грамматикой:
однородная живопись Modern и её исходная картинка сохраняются.

OFF: по **319029944 bytes** в каждом из трёх режимов; сравнивались сами байты,
а не только SHA. Поля current/baseline, все land SEC, классы и все pixels
совпали с неизменным HEAD138. Default legacy baker: **5900164 bytes**,
SHA256 `efbf46cdc4104053fb3b4ff7b07b24db30213c1b53161c1f9dcbd3623be1d84f`,
побайтно совпали с HEAD138 в четырёх вариантах.

Все UI runs выполнены скрыто под атомарным `build-locks/ui-tests.lock`;
занятый lock ожидался с интервалом20 с, чужой lock не удалялся. Каждый
финальный CLI receipt ссылается на указанный SHA EXE. Clipboard — in-memory,
`nativeAttempts=0`, sequence303→303, formats6→6, unchanged=1. System clipboard
не открывался и не блокировался. Собственный lock освобождён;
последующий lock другого исполнителя сохранялся.

Промежуточные FAIL не считаются PASS: слишком широкий обход authored stock
включал corner-only Winter; фильтр источников по выходному family class
отклонял Lime с физическим классом2. Причины устранены, финальные 67/55/55
выше относятся к пересобранному EXE после обеих правок. При подготовке PNG
исправлен такой же неверный фильтр. Default v1 режим отдельно сохранён,
чтобы не нарушить HD138 trusted witness. Проверки не ослаблялись.

## PNG 5×5: было / стало / родные переходы

- [Трава](../../build-tests-mepolish/grass-5x5-before-after-native.png)
- [Камень](../../build-tests-mepolish/stone-5x5-before-after-native.png)
- [Размеры, SHA и происхождение](../../build-tests-mepolish/png-receipt.json)

Каждый PNG3840×1344: три панели1280×1280 =5×5 тайлов по256 px и подписи64 px.
Для before/after использованы действительные `--terrain-mixed-edge-check`
растризации model SEC +working atlas; native — исходные физические SEC ссылки
на тех же HD источниках. Область before/after содержит полный5×5 переходный
обод мазка. Изображения просмотрены. Это штатный CPU preview, не framebuffer
игры или доказательство ручной визуальной приёмки. На before видны длинные
колонные flecks травы; after имеет дробную кромку и вариативные текстуры.
Исходная повторяемость художественных мотивов stock texture остаётся видимой.

## Соседние карты

Отказ оставлен. Побайтный детерминизм ядра по мировому origin проверяется для
одинакового банка. Он не согласует разные локальные текстуры, weights, stock
ownership и map seeds. Отдельная проверка показывает несовпадение общего края
при замене локального anchor с теми же материалами/координатами.

Безопасное разрешение требует общего проверенного source/ownership bank для
всего cohort, общего edge contract, транзакционного восстановления двух
атласов/SEC/Undo/GL при отказе любого участника, а также изменения dispatch
одиночной кисти и «Исправить переходы». Текущий блок находится в
`source/render/view.cpp`, `CView::resolveMixedEdgeTerrain`; этот файл запрещён
в поручении. Нужная будущая правка: заменить single-map refusal на общий
cohort planner только после проверки одинакового пиксельного edge bank;
`paintWorldTerrainAt` в `view_world_terrain.cpp` должен использовать этот
план вместо независимых Modern resolver stages. Просто удалить refusal
небезопасно. Реальная межкартовая кисть и реальная пара не объявляются PASS.

## Границы

Git только читался. VERSION, CHANGELOG, ROADMAP, Readme, translations в checkout
не менялись. Постоянная установка, живые карты/RES, игра, Base и Endurance
не менялись. Retained toolchain Endurance использован только для чтения.
Сеть, новые зависимости, подагенты и интерактивный запуск не использовались.
Специализированные private-engine навыки недоступны; приватный код не читался.
Обычная автоматическая проверка не заменяет визуальную/игровую приёмку владельца.

## Файлы для scoped-коммита сопровождающего

1. `source/documents/terrain_mixed_edge.cpp`
2. `source/documents/terrain_recipe_bake.cpp`
3. `source/documents/terrain_recipe_bake.h`
4. `source/app/terrain_mixed_edge_check.cpp`
5. `tests/terrain_recipe_bake_tests.cpp`
6. `docs/validation/ATAMAN_TERRAIN_MIXED_EDGE_POLISH_20261011.md`

Все `build-*-mepolish` и `build-tests-mepolish` — приватные игнорируемые артефакты,
не файлы для коммита. Версию назначает сопровождающий вместе с параллельными задачами.

## Готовый текст ROADMAP для сопровождающего

> EI-TERRAIN-MIXED-EDGE-POLISH — реализация и автоматическая проверка завершены.
> Исходная проблема: первый uniform источник и вытянутые штрихи mixed-edge135.
> Результат: четыре взвешенные метки одноклассных исходников, сохранённое зерно,
> периодический край без along×fade, HD256, selective v1→v2 recipe migration,
> default v1 baker/OFF совместимы с138. Проверки: ядро17451/0/0,
> synthetic67/0/0, Zone6 трава55/0/0 и камень55/0/0, UI169/0/0, scenario49/0/0,
> OFF3/3, legacy4/4; повтор0 слотов/дельт. Соседние карты сохраняют отказ:
> нужен единый проверенный pixel bank и cohort planner с правкой view.cpp.
> Разрешение: только выделенные файлы; без Git write/установки/live данных.
> Evidence: docs/validation/ATAMAN_TERRAIN_MIXED_EDGE_POLISH_20261011.md.
> Следующий шаг: scoped commit шести файлов сопровождающим и согласованная
> нумерация; затем owner visual/game acceptance экспериментальной опции.

## Готовый текст CHANGELOG для назначенного номера версии

> EI-TERRAIN-MIXED-EDGE-POLISH: mixed-edge выбирает взвешенные варианты
> однородной картинки одного физического runtime-класса по сохранённому зерну
> карты; четыре метки сохраняют конечный бюджет. Устранено растяжение stock
> flecks на клеточных границах, введён периодический collar и выбор исходной
> текстуры в теле, поддержаны HD256 и безопасная миграция старых соседних
> рецептов. Default v1 baker и Modern OFF/Classic/Manual побайтно совпадают
> с138. Ядро17451, integration67/55/55, UI169, scenario49 — PASS без FAIL/SKIP;
> OFF3/3, legacy4/4. Общая межкартовая кисть пока отклоняется. Визуальная и
> игровая приёмка владельца остаётся открытой. Evidence: указанный отчёт.
