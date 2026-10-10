# EI-TERRAIN-BAKE-B2-MODERN-135 — Modern mixed-edge

Дата: 2026-10-10. База: `main`, `b87df6f91ae486a8fe49943180d8cd72ea3887af`, VERSION 134.
Результат: VERSION **135**, реализация подготовлена к ревью сопровождающего.
Постоянная установка не обновлена; push, сеть, новые зависимости, подагенты и интерактивный запуск редактора не использовались.

## Поведение и интерфейс

В панели «Ландшафт», инструмент покраски, режим Modern («Современное рисование», Z),
раздел **«Дополнительно»**: **«Плавные границы (mixed-edge)»**. По умолчанию выключена;
`ui/landscape/modernMixedEdge` сохраняется в настройках. В Classic/Manual настройка не действует.

При включении drag-preview использует существующую uniform-preview ветвь Modern; отпускание мазка строит
mixed-edge по ядру B1. «Исправить переходы…» / Shift+R обрабатывает изменённое поле
земли текущей карты. Связанный переходный контур дополнительно обходится по смешанным
общим рёбрам, чтобы не соединять половину нового края со старым resolver-тайлом.
Поэтому затронутая область может расширяться по связанному контуру.
Однородные клетки используют прежний Modern; уже соответствующая полю однородная
картинка/rotation сохраняется, включая uniform-preview текущего мазка.

Источники — только непрозрачные неанимированные однородные тайлы и подтверждённые
двухматериальные stock-кромки текущего физического атласа/профиля. Для stock выбирается
наилучшая допустимая RGB ownership fit, из неё строится brush bank, без общего substitute mask.
Нет источника, stock-кромки или runtime-класса — понятный отказ; мазок откатывается.

`CTerrainMixedEdge` работает на staging-копии и выпекает клетки по одной, сохраняя
детерминированные coordinate labels. В рабочий атлас вставляет через существующий
`importPhysicalTile`, с typed pixel/signature dedup. Ключ рецепта включает упорядоченные
угловые материалы, variant и runtime-класс и связан с проверенным профилем/пикселями карты.
Все recipe aliases сохраняются при compaction и записываются в optional профильное
`mixedEdgeRecipes`; обычные профили без списка сериализуются прежним способом.

Одна команда Undo владеет полем вершин/SEC и before/after рабочим атласом.
CView предварительно проверяет кандидат модели, устанавливает классы/атлас,
обновляет GL и CTileForm, затем обновляет физический предпросмотр. Отказ upload
восстанавливает модель и atlas stage; генерируемые исходные тайлы не меняются.
Публикация остаётся существующей: рабочий атлас → dense finalizer/pin/remap →
MPR/RES/HDPack; сохранённые источники stock и recipe aliases сохраняются для дальнейшей покраски.
Нового пути записи пользовательских карт нет.

Индикатор — **«Диагностика и атлас»** той же панели, существующий `terrainAtlasCapacity`.
Mixed-edge показывает фактически занятые/4096 и свободные слоты, учитывает logical end
неполной страницы, а не сумму двух независимых счётчиков. Зелёный ≤2048,
жёлтый 2049–4096, красный >4096. Повторно использован и проверен контракт
`expandedAtlasCapacityIsYellow`. Новая mixed-edge вставка выше4096 отказывает;
8192 по умолчанию не разрешается. Сохранение существующего атласа >4096 показывает
предупреждение с **«Отмена» по умолчанию**; публикация выше4096 также требует явного preview review.
Никакой подмены материала для попадания в бюджет нет.

## Автоматические проверки

Все UI-процессы финальных прогонов запускались последовательно под атомарно созданным
каталогом `build-locks/ui-tests.lock` с owner receipt. По окончании lock освобождён.
Изолированный clipboard: in-memory, nativeAttempts=0, sequence33622→33622, 8→8 форматов,
unchanged=1. CView скрыт через WA_DontShowOnScreen; интерактивной приёмки не было.

| Проверка | PASS | FAIL | SKIP | Подтверждение |
|---|---:|---:|---:|---|
| `--terrain-mixed-edge-check`, synthetic |60|0|0|[log](../../build-tests-135-bake/synthetic.log)|
| тот же check, копия Zone6 |49|0|0|[log](../../build-tests-135-bake/zone6.log)|
| `terrain_recipe_bake_tests` |17413|0|0|[log](../../build-tests-135-bake/terrain_recipe_bake_tests/result.log)|
| `terrain_virtual_atlas_tests` |144|0|0|[QtTest](../../build-tests-135-bake/terrain_virtual_atlas_tests/qtest.txt)|
| `terrain_resolve_audit_tests` |5|0|0|[QtTest](../../build-tests-135-bake/terrain_resolve_audit_tests/qtest.txt)|
| `endurance_document_tests`, весь корпус |192|0|18|[QtTest](../../build-tests-135-bake/endurance_document_tests/qtest.txt)|
| `world_terrain_sculpt_tests`, все случаи, копии установленных карт |45|1|0|[QtTest](../../build-tests-135-bake/world_terrain_sculpt_tests/qtest.txt)|
| `--ui-widget-check`, полный |169|0|0|[log](../../build-tests-135-bake/ui.log)|
| `--scenario-layer-check` |50|0|0|[log](../../build-tests-135-bake/scenario.log)|
| `--dark-theme-check` |70|0|0|[log](../../build-tests-135-bake/dark.log)|
| save122 `--terrain-height-res-save-check` |29|0|0|[log](../../build-tests-135-bake/save122.log)|
| `res_reader_drain_tests`, production objects135 |23|0|0|[QtTest](../../build-tests-135-bake/res_reader_drain_tests/qtest.txt)|

Числа CLI здесь не включают отдельный PASS clipboard receipt. Gallery — **PASS**,
497 моделей /7165 текстур, поиск/выбор/Shift/Ctrl/закрытие и thumbnail worker regression:
[log](../../build-tests-135-bake/gallery.log). [Receipts и SHA финального exe](../../build-tests-135-bake/final-summary.json).
SKIP18 — отдельные внешние/private corpus probes без назначенных фикстур; они **не PASS**.
Обязательные новые mixed-edge и save122 проверки не пропускались и не ослаблялись.

Synthetic: 16×16 клеток, 4 материала, 2 тройных и 1 четырёхматериальный узел;
22 mixed/mixed ребра и11264 RGBA channel comparisons без различий,
ещё32 стыка mixed/неизменённый uniform без различий. Ядро:16257 рёбер,
1978244 сравнений каналов. Новая синтетическая пара независимых областей с
мировым origin(-19,27) совпадает побайтно с общей выпечкой и имеет общий exact край.
Это подтверждение ядра; общая кисть UI пока ограничена, см. ниже.

Проверены настоящие begin/release CView и QUndoStack: ровно1 запись,
Undo/Redo совпадают по SEC сектора, обеим версиям поля и atlas fingerprint;
CTileForm содержит установленные pixels, GL upload выполнен.
Проверены typed dedup (одинаковые pixels, разные классы остаются разными слотами),
recipe aliases, отсутствие live stage mutation при отказе бюджета, отсутствие источника,
анимация и исходная animated SEC-ссылка, уже заменённая uniform drag-preview.
Однородный мазок также отказывает, если имеется только foreign substitute и отсутствует local uniform источник.
Общая кисть с соседней картой явно отказывает без мутации.
Проверены2048/2049/4096/4097, неполная исходная страница и настоящий save warning>4096
с «Отмена» по умолчанию.

Zone6:128×192=24576 клеток, исходный6×6 uniform vertex patch(113,99),
мазок4×4 вершины начинается(114,100), без подготовительного undercoat.
16 mixed/mixed рёбер,8192 RGBA comparisons без различий. Рабочий атлас1216→1230;
после dense публикации1162 слота. Synthetic:64→81 рабочих,28 опубликованных.
Сохранение/чтение с диска проверило **все SEC-ссылки**, поле и **все MMP payloads**;
DXT1 decoding и свежий профиль выполнены обычными production loaders.
Повторное разрешение этих же клеток до сохранения и после открытия —0 новых слотов,
0 SEC-дельт; опубликованные28/1162 не растут. Все recipe aliases пережили compaction.
Atlas equality здесь — точность опубликованных payloads, не утверждение о lossless DXT1.

### Выключенная опция = версия134 побайтно

`tests/terrain_mixed_edge_off_regression.cpp/.pro` собран дважды: с исходниками
назначенного HEAD134 (приватный `build-tests-135-bake/baseline-134`) и текущими135.
На одной копии Zone6, одном мазке/seed17, сериализованы **все land SEC-ссылки, current/baseline
поля, runtime-классы и pixels рабочего атласа**. Сравнивались сами байты, не только hashes.
На каждый режим79954616 bytes, три совпадения:

- Modern OFF: `823432af35b7de7294ae4d6e814c594d9ce6f879ed26ef2139e56a0504d8832e`.
- Classic: `81792e8669278ac936f3b94ff90151e4b45f96ffd30328190f1c53620081bc5e`.
- Manual: `9a5714d25f8801436f7e977042e514d7fd77ad26a1caa2dbc5e00945d1885379`.

[Byte comparison receipt](../../build-tests-135-bake/off-comparison.json),
[134 run](../../build-tests-135-bake/off-baseline/run.log),
[135 run](../../build-tests-135-bake/off-current/run.log).
Общая подсветка бюджета и новое предупреждение сохранения — явно порученные изменения UI;
байты алгоритмов покраски при выключенной опции сохранены.

### Унаследованный world-terrain FAIL — не PASS

`installedZone11Zone12ReadOnly`, BoundaryFill(mode4): gap-after0.000585550,
превышен существующий строгий предел теста. На **исходном назначенном HEAD134**,
том же Qt/MinGW и тех же изолированных copies — тоже45 PASS/1 FAIL/0 SKIP,
тот же mode4 и тот же gap. [Независимый baseline134](../../build-tests-135-bake/world-baseline/qtest.txt).
Исходники world sculpt/session не менялись; причина этого старого расхождения высот
не исследована в B2 и не объявляется установленной. Порог не расширяли,
случай не выключали, отсутствие исходных фикстур в первом baseline harness исправлено.
Это открытый вопрос регресса для сопровождающего, а не доказательство исправности этого случая.

## PNG и границы визуальной проверки

- [До](../../build-tests-135-bake/zone6/before.png), [после](../../build-tests-135-bake/zone6/after.png):2048×2048.
- [Сравнение](../../build-tests-135-bake/zone6-before-after.png):2048×1090.
- [Крупно](../../build-tests-135-bake/zone6-closeup.png):1792×946.
- [Dimensions/SHA receipt](../../build-tests-135-bake/png-receipt.json).

Это эквивалентный top-down renderer физического предпросмотра: читает реальную SEC-ссылку,
slot/rotation и pixels рабочего атласа; не произвольная иллюстрация.
PNG открыты и визуально осмотрены. Новая граница мягкая; внутри остаётся видна периодичность
stock-текстур. Новые узлы могут быть мягче исходной крошки, как уже отмечено в B1.
Точное совпадение mixed RGBA-рёбер проверено **до DXT1 упаковки**. Не заявляется lossless
стык с каждым старым вариативным uniform-stock PNG или C1-гладкость после DXT1.
Это не игровая/художественная приёмка и не desktop-проверка пользователя.

## Ограничения и передача

- Только Ground одной карты. Overlay, вода/underwater и анимированные материалы —
  отключить опцию для прежнего Modern; неподдержанный mixed-edge запрос явно отказывает.
- **Общая кисть соседних карт не поддерживается в B2**, отказ проверен.
  World-coordinate детерминизм ядра подтверждён парой, но end-to-end граница двух
  пользовательских карт с разными atlas/profile ещё не принимается за бесшовную.
- Источники из других maps/глобального artwork registry автоматически не подставляются.
  Для отсутствующих local uniforms/stock нужно исправить/подтвердить профиль либо отключить опцию.
- 4096 — предел новых mixed-edge вставок,8192 не включён. Пробы128px на synthetic/Zone6
  не устанавливают память игры или других размеров тайлов.
- Унаследованный world FAIL и внешние SKIP остаются открыты. Ревью Claude,
  окончательная сборка/установка сопровождающим и ручная/игровая приёмка не выполнены этим заданием.

Исходная Zone6 не изменена; все карты, PNG, профили, RES, baseline export и test TEMP —
только в `build-tests-135-bake/`. Источник/copy MPR SHA256:
`0676f3f559f4623b344b2773bdbae97dce02ec508965e6ba8cfba256613e3535`.
[Source receipt](../../build-tests-135-bake/source-receipt.json).
Приватные engine reference/modding skills в этой сессии недоступны; закрытый код не исследовался.

Release wrapper успешно собрал135 с имеющимися Qt5.12.12/MinGW7.3.0;
`--version`: EI Mapper Endurance Edition135. Это проверенный **shadow build**,
не финализированная распространяемая сборка:
`build-release-135-bake/release/EIMapperEndurance.exe`.
Exe SHA256: `2afaa335501620c4127167ce89a6552d7937094c51d1649c49f2cf2251a3be5d`.
[Build log](../../build-tests-135-bake/build.log), [exit0 receipt](../../build-tests-135-bake/build-result.json).
В wrapper есть прежние диагностические сообщения vswhere lookup; фактические runtime
компоненты собраны, итоговый child exit0. QM пересобран существующим lrelease.
Git identity — только env процесса команды; scoped-коммит в main с VERSION135,
без push. Полный commit SHA сообщается сопровождающему в итоговом ответе.


## Чёткая кромка — продолжение ревью 135, 2026-10-10

Поручение: исправить мягкий ореол на grass-паре, сохранив швы, транзакции, бюджет,
публикацию и OFF=134; показать grass и stone/ground на копии Zone6 рядом с родными
переходами. База: main `d94e3a4bea3ecf8899df0751da051c26479bc793`.
VERSION остаётся135 по прямому решению владельца: версия ещё не передана.
Ни установка, ни live-данные, ни Endurance не менялись; push не выполнялся.

### Измеренная причина

Гипотеза о дробном RGB ownership подтвердилась, но причина шире одного низкого
контраста: adapter брал **первые четыре** допустимых stock-тайла, игнорируя более
надёжные и рассыпчатые источники. На grass это33/34/35/36: RGB RMSE6.693–9.728,
доля ownership в(.1,.9) в извлечённых полосах40.35–42.73%. Ошибочный абсолютный
floor знаменателя100 смещал владение к первому материалу на7.92–8.50% пикселей
этих fits. Контрастные stone/ground также не защищены от first-four bias.

Все1216 исходных PNG имеют128×128: SmoothTransformation **не выполнял ресайз**
в этом конкретном воспроизведении и не объявляется его установленной причиной.
На других размерах он мог бы сгладить бинарные детали, поэтому заменён nearest.
Gaussian остаётся только в coarse-guide геометрии; перенос ownership не размывается.

| Пара | Прежние stock-слоты | Промежуточные уровни после округления к5 | Новые stock-слоты | Промежуточные веса нового банка |
|---|---|---:|---|---:|
| Grass / Dark + Common |33,34,35,36|12164/33280 =36.55%|57,52|0/33280|
| Ground / Dead + Stone / Lime |507,552,553,557|11590/33280 =34.83%|588,615,848,507|0/33280|

До округления прежние банки имели17333 и17076 дробных samples из33280.
Теперь fit нормируется истинным RGB контрастом без floor100; неразличимый пиксель
помечается неопределённым ownership=.5. Контрольный low-contrast случай с
RGB delta(6,4,2), denominator56, истинным alpha=.5 раньше дал бы.28; новый нативный
тест подтверждает.5 и RGB RMSE0.

Новые выбранные grass-источники: RGB RMSE2.031/3.580, contrast RMS30.456/30.940,
81/146 отдельных4-connected фрагментов площадью2–64px в полосах.
Stone/ground: RMSE1.501–3.732, contrast RMS38.449–40.562,54–73 фрагмента.
Приоритет рассыпчатых sources применяется лишь при relative RMSE<.15 и≥16
фрагментов; остальные sources допускаются только при relative RMSE<.2.
Source rank дополнительно включает бонус за фрагменты и может быть отрицательным;
это не отрицательная ошибка RGB. После makeBrush ownership округляется к пяти
уровням0/.25/.5/.75/1 и переводится в бинарное покрытие детерминированным density
dither в координатах периодического банка. Чистые stock-пиксели сохраняются.

Измерения получены отдельными нативными probes со старым и новым core на одних
копиях128px PNG; float strips и JSON сохранены приватно в task-каталоге.
[Сводка измерений](../../build-tests-135-bake/sharp-edge/measurement.json),
[старый probe](../../build-tests-135-bake/sharp-edge/probe-old/stock-measurement.json),
[новый probe](../../build-tests-135-bake/sharp-edge/probe-new/stock-fragments.json),
[grass diagnostics](../../build-tests-135-bake/sharp-edge/grass/stock-diagnostics.txt),
[stone diagnostics](../../build-tests-135-bake/sharp-edge/stone/stock-diagnostics.txt).

### Исправление и сохранность

- Проверяются все stock-источники **той же пары** текущего атласа/профиля; выбирается
  надёжная рассыпчатая кромка, а не первая четвёрка и не самый чистый HD-контур.
  Материалы не подменяются. Банк другой пары не потребовался и не внедрялся;
  без надёжного собственного источника остаётся явный отказ с русской причиной.
- Nearest sampling, пять уровней + density dither исключают дробную мягкую маску
  на двухматериальной кромке. World phase/recipe grammar не менялись; общий банк
  используется обеими сторонами ребра, без отдельного pixel noise в каждой клетке.
- Recipe hash содержит `crisp-stock-v1|`: старые мягкие135 keys не блокируют
  перестроение. Старые слоты/aliases не уничтожаются; первое обновление может
  потребовать новых слотов в пределах прежнего4096, повтор crisp —0 новых слотов.
  Нативный check подставляет все четыре прежних variant keys и проверяет regeneration.
- Проба stone выявила существующий `terrain.recipe.class_tie`: на границе
  Ground / Dead(класс1)/Stone / Lime(0) старый SEC-слот853 имел класс4.
  При равном покрытии покраска теперь предпочитает класс нарисованного материала,
  если он среди максимальных покрытий; иначе остаётся предпочтение predecessor.
  Repair сохраняет reviewed predecessor как раньше. Добавлен отдельный tie check.
- Публикация, atlas-aware Undo/Redo, slot budget, option/UI paths не изменены.

### Финальные проверки продолжения

| Проверка | PASS | FAIL | SKIP |
|---|---:|---:|---:|
| terrain_mixed_edge_check synthetic |63|0|0|
| terrain_mixed_edge_check Zone6, основной fixture |53|0|0|
| Zone6 grass, дополнительный native5×5 |54|0|0|
| Zone6 stone/ground, дополнительный native5×5 |54|0|0|
| terrain_recipe_bake_tests |17425|0|0|
| полный --ui-widget-check |169|0|0|
| --scenario-layer-check |50|0|0|
| --dark-theme-check |70|0|0|
| save122 --terrain-height-res-save-check |29|0|0|

UI/App-проверки выполнены под атомарным directory-lock `build-locks/ui-tests.lock`,
вне интерактивного редактора; clipboard backend=in-memory, nativeAttempts0,
sequence33622→33622, formats8→8. Lock снят после child exit0.
Core проверяет18241 общих ребра/2240132 RGBA-channel comparisons, в том числе
sharp4-material grid и независимые соседние карты с sharp bank/мировым origin.
Обе реальные пробы:24576 SEC cells,1216→1230 working slots,1162 published;
16 mixed edges/8192 channel comparisons, save→disk fresh reopen, Undo/Redo,
старые cache keys, runtime tie; повтор до и после открытия0 новых слотов/0 SEC deltas.
Synthetic:22 mixed edges/11264 comparisons,32 границы с unchanged uniform,
тройные2/четверной1 узлы, одна настоящая QUndoStack entry и GL preview.

OFF-regression заново собран/выполнен после изменения extraction: Modern OFF,
Classic, Manual побайтово совпали с назначенным HEAD134 —79954616 bytes на режим,
все три SHA остались указанными в исходном разделе отчёта.
[OFF receipt](../../build-tests-135-bake/sharp-edge/off-comparison.json),
[core receipt](../../build-tests-135-bake/terrain_recipe_bake_tests/receipt.json),
[core totals](../../build-tests-135-bake/terrain_recipe_bake_tests/result.log),
[финальная матрица](../../build-tests-135-bake/sharp-edge/final-summary.json).

Первый stone прогон действительно отказал на class_tie и был исправлен;
первый новый sharp-grid тест не задавал валидную tie preference — fixture
исправлен без ослабления core. Эти попытки не считаются PASS; таблица выше
относится к окончательному состоянию. Незапрошенные gallery/reader/world/document
наборы из первоначального этапа здесь не объявляются новым прогоном:
world45/1/0 и document192/0/18 остаются ранее зафиксированными ограничениями.

### PNG и честная оценка

- [Grass до/после](../../build-tests-135-bake/sharp-edge/grass-before-after.png),
  [stone/ground до/после](../../build-tests-135-bake/sharp-edge/stone-before-after.png):2048×1090.
- [Grass новая/native кромка5×5](../../build-tests-135-bake/sharp-edge/grass-edge5x5-vs-native.png),
  [stone новая/native кромка5×5](../../build-tests-135-bake/sharp-edge/stone-edge5x5-vs-native.png):2560×1346,
  целочисленное nearest увеличение2×. Есть варианты1:1:1280×706.
- Исходные before/after renders:2048×2048; отдельные5×5:640×640.
  [PNG dimensions/SHA](../../build-tests-135-bake/sharp-edge/png-receipt.json).
- Grass: исходная неизменённая6×6 vertex patch(113,99), paint4×4(114,100),
  native central cell(112,100), slot55. Stone:patch(50,122), paint(51,123),
  native central cell(55,121), slot848. В обоих случаях без подстилающей перекраски.
- Канонические zone6-before-after.png/zone6-closeup.png обновлены; прежний мягкий
  review сохранён в sharp-edge/soft-review-*.png. Крупный канонический PNG1792×962.

PNG открыты и визуально осмотрены. Широкий мягкий ореол исчез; у обеих пар есть
чёткие отдельные включения, крошка и рваность, ближе к родным stock-переходам.
**Не идентично родной кромке:** grain местами мельче, на stone заметны вытянутые
штрихи из переноса полос, периодический банк и вариативные uniform textures
всё ещё могут повторяться. Геометрия пятна остаётся гладкой/скруглённой на масштабе
клеток. Плавный центр3/4-узлов B1 не заменён бинарным рисунком. Это честная оценка
для ревью владельца, не художественная/игровая приёмка. RGBA-швы доказаны до DXT1;
lossless/C1 после сжатия не заявляются. Ограничение Ground одной карты сохраняется.

Проверенный shadow exe: `build-release-135-bake/release/EIMapperEndurance.exe`,
SHA256 `f6f201663fbb3da15d0102e00a5fde8442cfa371080637d17bddb2d8bdadf104`.
Русский QM пересобран existing lrelease:3309 finished/0 unfinished.
Все новые данные и PNG — только `build-tests-135-bake/`; окончательную сборку,
установку и ручную приёмку делает сопровождающий. Scoped-коммит main без push,
полный SHA сообщается в итоговом ответе.

## Передача 135

**EI-FINALIZE-135, 2026-10-10: установка 134→135 ВЫПОЛНЕНА после уточнения
базы скорости сопровождающим: действующая база — установленная 134.**
Первоначальный остановочный результат ниже сохранён как история прогона;
последующее решение и текущий результат установки приведены в заключительном
подразделе «Решение сопровождающего и установка 134→135».
Cold unmo: медианы 132 **80,988 мс**, установленной 134 **95,164 мс**,
135 **96,301 мс**; 135 к132 **+15,313 мс / +18,91%**, к134 +1,137 мс / +1,19%.
Условие «не хуже132 более чем на10% или ≤+15мс» не выполнено: превышены оба
допуска. Cold stbu прошёл: 132 30,537 мс →135 31,984 мс (+1,447 мс/+4,74%).
Повтор замера для обхода полученного порога не выполнялся. Это состояние первоначальной
передачи до уточнения; оно отменено решением сопровождающего ниже.

### База и свежая сборка

- Поручение владельца через Claude 2026-10-10; уточнения имеют приоритет:
  состав сравнивается с staging134; в ROADMAP изменён только пункт135.
  Ветка main, исходное дерево чистое, source HEAD
  `3ce012182db78e38290ab109af48c0e99a932ef5`, VERSION135.
  AGENTS.md прочитан, SHA256
  `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
  Применён docs/TEST_BUILD_HANDOFF.md; все старые rollback сохраняются.
- Свежий git archive → `build-release-135-final/src`, shadow
  `build-release-135-final/release`; 1820 tracked blobs,
  1609 архивных текстов приведены к точным LF-байтам Git.
  До и после штатной Release/lrelease/обеих x86 runtime-сборок: 0 blob-mismatches.
  Qt5.12.12/MinGW7.3 из существующего toolchain только для чтения.
  Wrapper exit0, 90,937 с. `--version` после развёртывания runtime:
  `EI Mapper Endurance Edition 135`, exit0.
- Первый raw `--version` до windeployqt завершился с exit3221227010 без вывода;
  не PASS. После штатного развёртывания compiler/Qt runtime тот же EXE успешно
  подтвердил135 и выполнил регресс; этот исходный сбой сохранён в version.{out,err,json}.
  EXE SHA256 **`12C10B81846545510BF8465C725BDF978F8CDC66F0EB383968897900D08AFB43`**.
  [Provenance](../../build-tests-135-final/provenance.json),
  [сборка](../../build-tests-135-final/release-build.log),
  [version после deploy](../../build-tests-135-final/version-deployed.out).
- Параллельных задач/подагентов, сети, установки зависимостей и интерактивного
  редактора не было. Навыки приватного движка недоступны; приватный код не читался.
  Игру/мод/живые карты/Endurance не изменяли; приватные fixtures вне Git/пакета.

### Регресс изолированного буфера и terrain

Все UI/clipboard/reader/terrain/benchmark процессы запускались последовательно
под атомарным `build-locks/ui-tests.lock` с owner receipt; lock освобождён.
Обязательный набор tests/clipboard_isolation_regression.ps1 exit0, включая
полный UI без фильтра. **Native guard-тесты не запускались.** Три отрицательные
пробы сторожа входят в11 обязательных clipboard случаев: ожидаемый exit2,
реальное обращение блокируется до чтения/записи, это не native guard suite.

`PASS: harness clipboard metadata sequence=33629->33629 formats=2,8,17,49470,49794,49795->2,8,17,49470,49794,49795 unchanged=1`

У обычных проверок nativeAttempts=0. Сохранены sequence **33629→33629** и точные
6 format IDs:2,8,17,49470,49794,49795. Benchmark отдельно:33638→33638, те же8
форматов; synthetic и Zone6:33647→33647,8→8, unchanged=1.
Числа CLI ниже не включают отдельный PASS clipboard receipt.

| Проверка | PASS | FAIL | SKIP | Evidence в build-tests-135-final |
|---|---:|---:|---:|---|
| полный --ui-widget-check |169|0|0|ui-widget.stdout.log|
| --scenario-layer-check |50|0|0|scenario.stdout.log|
| --dark-theme-check |70|0|0|dark-theme.stdout.log|
| обязательные verified_clipboard_tests |11|0|0|clipboard/release/clipboard-results.txt|
| reader-drain, точные production objects135 |23|0|0|reader-drain/fixtures-resdrain/reader-drain-results.txt|
| --terrain-mixed-edge-check, synthetic |63|0|0|terrain-ready-synthetic.out|
| тот же check, копия Zone6 |53|0|0|terrain-ready-zone6.out|
| terrain_recipe_bake_tests |17425|0|0|recipe.out|
| world_terrain_sculpt_tests |45|**1**|0|world_terrain_sculpt_tests/qtest.txt|

Gallery PASS,497 моделей/7165 текстур: open7мс/select3мс/Shift2мс,
close во время600мс worker1мс; cache/queue/stale/parallel/all-N checks PASS.
Zone6:24576 клеток,1216→1230 working slots,1162 published,16 shared edges/
8192 RGBA comparisons; сохранение/диск/reopen, Undo/Redo, повтор0 новых слотов
и0 SEC deltas. Синтетика63/0 и core17425/0 закрывают новые mixed-edge сценарии.

Первый synthetic вызов: FAIL isolated fixture directory, exit1 — каталог вывода
в test setup не был создан. Исправлена только подготовка собственного каталога,
без изменения программы/фикстуры/бинарника; повтор63/0/0. Неисправный preflight
сохранён как terrain-synthetic.{out,err,json}, не скрыт как PASS.

OFF/Classic/Manual: по **79954616 bytes** совпали с назначенной базой134:
Modern OFF `823432AF35B7DE7294AE4D6E814C594D9CE6F879ED26EF2139E56A0504D8832E`,
Classic `81792E8669278AC936F3B94FF90151E4B45F96FFD30328190F1C53620081BC5E`,
Manual `9A5714D25F8801436F7E977042E514D7FD77AD26A1CAA2DBC5E00945D1885379`.
Новые OFF и world binaries собраны из снимка135; база сравнения — сохранённые
проверки b87df6f91ae486a8fe49943180d8cd72ea3887af (134), build-tests-135-bake.
575 файлов сохранённой базы сверены с Git blobs134, включая исходники world.
[OFF](../../build-tests-135-final/off-comparison.json).

World **45/1/0, exit1**: прежний
WorldTerrainTests::installedZone11Zone12ReadOnly / BoundaryFill(mode4),
gap-after0.000585550; имя/условие FAIL совпадают с134, порог не ослаблен.
Это известный **FAIL, не PASS**, код дефекта не изменяли и причину не исследовали.
[Сравнение](../../build-tests-135-final/world-comparison.json),
[134](../../build-tests-135-bake/world-baseline/qtest.txt),
[135](../../build-tests-135-final/world_terrain_sculpt_tests/qtest.txt).
Первичная postprocessing-сверка остановилась из-за отсутствующего импорта re
в локальном runner. После исправления завершена отдельным audit.py по уже
полученным логам; успешные тесты повторно не запускались. Эта ошибка runner
не меняет world exit1 и не превращает его в PASS.

### Скорость unmo/stbu: 5 повторов

Одинаковый harness `build-tests-133-perf/real-benchmark.cpp` связан отдельно с
точными Release objects132/134/135, исключён только main.o. EXE134 из этой
сборки совпадает SHA256 с установленным134. Использован прежний read-only
профиль129 DejaVu/Base:unmo36/stbu71,8 видимых плиток.
В каждом повторе новая app-папка, первый процесс — cold и повтор памяти,
второй — warm disk и повтор памяти. Порядок132/134/135 чередуется.
Cold означает новый процесс + miss thumbnail memory/disk; OS file cache
не очищали. Кэши владельца и установки не использовались и не очищались.
Всего30 процессов/120 измерений, все exit0, visible8/ready8/unavailable0/
complete1; provider calls8 для cold,0 для disk/repeat. Нет SKIP/таймаутов/сбоев.

| Fill, мс (min / median / max), n=5 | 132 | 134 установленная | 135 | Δ135 к134, мс / % |
|---|---|---|---|---|
| unmo, cold | 79,439 / 80,988 / 82,769 | 92,270 / 95,164 / 97,892 | 92,980 / 96,301 / 104,657 | 1,137 / 1,19% |
| unmo, тёплый диск | 29,230 / 31,371 / 32,953 | 30,247 / 32,931 / 33,357 | 30,645 / 31,507 / 33,333 | -1,424 / -4,32% |
| unmo, повтор после cold | 5,050 / 5,305 / 5,562 | 5,049 / 5,516 / 5,805 | 5,260 / 5,599 / 5,750 | 0,083 / 1,50% |
| unmo, повтор после диска | 5,245 / 5,357 / 5,579 | 5,066 / 5,206 / 5,610 | 5,139 / 5,397 / 6,065 | 0,191 / 3,67% |
| stbu, cold | 30,257 / 30,537 / 31,609 | 30,600 / 31,787 / 39,311 | 29,467 / 31,984 / 34,612 | 0,197 / 0,62% |
| stbu, тёплый диск | 9,672 / 10,671 / 10,972 | 9,553 / 10,467 / 13,385 | 9,998 / 10,329 / 11,766 | -0,138 / -1,32% |
| stbu, повтор после cold | 5,755 / 5,886 / 6,216 | 5,885 / 5,965 / 7,865 | 5,789 / 5,900 / 5,992 | -0,065 / -1,09% |
| stbu, повтор после диска | 6,030 / 6,366 / 7,489 | 5,750 / 6,149 / 6,325 | 5,811 / 6,146 / 6,274 | -0,003 / -0,05% |

Первоначально gate применялся к cold медианам135 против132: unmo **FAIL**,
stbu PASS. Сопровождающий исправил ошибочную базу на134; текущий gate PASS
по неизменным измерениям, см. заключительный подраздел.
[Raw samples](../../build-tests-135-final/benchmark-raw.json),
[итог](../../build-tests-135-final/benchmark-summary.json),
[clipboard metadata](../../build-tests-135-final/benchmark-clipboard.json).

### Состав, staging и сохранённая установка

- Новый собственный `build-staging-135-final`, staged EXE=проверенный EXE по SHA256,
  staged --version135 до finalizer. Finalizer из git archive применён **только** к
  staging, внутренний validate_distribution exit0;127/127 payload hashes,0 missing,
  0 extras. После финализации staging не запускался и не изменялся.
- Состав совпадает с `build-staging-134-final`:0 добавленных/удалённых paths.
  Различаются ровно6 версионных файлов:EXE, оба runtime-компонента,
  VERSION, Readme.md, CHANGELOG.md; manifest закономерно пересоздан.
  Qt/compiler runtime, synthetic fixtures, LICENSE, NOTICE и manual неизменны.
- TRAVEL_EDITOR.md уже возвращён финализатором134; совпадает с source135 после
  нормализации и со staging134:SHA256
  `945D3E7B5905944D8735D07E518063DBC04BCDA15887CFD5B119F463EF1ACC83`.
  Политика133 сохранена:UTF-8 без BOM, CRLF для всех текстов пакета и manifest,
  Git blobs не менялись. Правки упаковочных скриптов **не потребовались**.
  SHA256 manifest135:`8AC0A85ABF127BB83932AD572455991D647B0D3D3922A4CC9F2F76FA81DAA319`.
  [Package receipt](../../build-tests-135-final/package-receipt.json),
  [finalizer](../../build-tests-135-final/finalizer.out).
- **134→135 не выполнялось из-за speed gate.** Постоянная папка
  `artifacts/EI-Mapper-Ataman-2-first-test` остаётся134;
  все127 managed hashes соответствуют её manifest/staging134,
  TRAVEL_EDITOR.md присутствует. EXE134 SHA256
  `EA881BD6647F5FD792F7E41197A4EC49332321F5047C9E665FE8BB897A149B56`;
  manifest134:`1FD79984B7A79447A308FD947193094D6D3426AB402A667FEC2E9BE380B90945`.
  Пользовательские файлы/кэши не затрагивались, installationWrites=0,
  rollback134 не создавался; все6 прежних rollback сохранены.
  [Receipt](../../build-tests-135-final/preserved-installation.json).

Опция остаётся **экспериментальной, ВЫКЛ по умолчанию**. Известны вытянутые
штрихи вдоль границ клеток на траве и кромке камень/земля:
`build-tests-135-bake/sharp-edge/*5x5*.png`. Ground одной карты, shared brush
явно отказывает; художественная/ручная/игровая приёмка открыта.
Следующий шаг: решение сопровождающего по превышению speed gate или отдельное
поручение на причину/исправление производительности. После нового основания —
необходимый замер и разрешённая установка с verified rollback134; до этого135
не установлена. Scoped-коммит только этого отчёта и статуса пункта135 в ROADMAP,
без push/изменения глобальной Git identity.


### Решение сопровождающего и установка 134→135

**2026-10-10, новое явное поручение владельца через Claude: правильная база
скорости — установленная134; исходная база132 была указана ошибочно.**
Накопленная разница132→134 уже принята при передаче134;135 не меняет код
галереи (обоснование сопровождающего). Сравнение ранее полученных пяти повторов:

| Cold fill, median n=5 | 134, мс | 135, мс | Δ, мс | Δ, % | Допуск |
|---|---:|---:|---:|---:|---|
| unmo |95,164|96,301|+1,137|+1,19%|PASS|
| stbu |31,787|31,984|+0,197|+0,62%|PASS|

Оба случая удовлетворяют ≤+10% или ≤+15мс относительно134. Сырые samples
не изменялись, успешные функциональные тесты и замеры повторно не запускались.
Прежний benchmark-summary.json хранит первоначальный thresholdExceeded=true
для132 как исторический факт, а не текущий запрет. Эта новая явная инструкция
разрешила установить именно ранее проверенный и финализированный staging135.

- AGENTS.md SHA256 прежний F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3;
  договор docs/TEST_BUILD_HANDOFF.md применён. Исходная main перед установкой:
  0de984b981c18bc35c5e29715653fd8e5b83ff0d, дерево чистое; source программы
  остаётся3ce012182db78e38290ab109af48c0e99a932ef5, VERSION135.
- `build-staging-135-final` не пересобирался, не менялся, не запускался;
  повторный finalizer не выполнялся. Перед использованием проверены полный
  состав и127/127 payload hashes, прежний manifest SHA256. После установки
  снова установлено, что исходный staging сохранил ровно те же bytes.
- Win32_Process проверен дважды: до операции и непосредственно перед первой
  заменой. Процессов из постоянной папки нет; пользовательские процессы не
  останавливались. Old manifest134:127/127 hashes PASS, конфликтов нет;
  old EXE SHA256 EA881BD6647F5FD792F7E41197A4EC49332321F5047C9E665FE8BB897A149B56.
- Новый собственный rollback: `artifacts/rollback-134-to-135-final`.
  Скопированы точные6 заменяемых managed файлов134 и SHA256SUMS.txt, всего7;
  hashes подтверждены до любых замен и повторно после установки. Старый
  manifest в backup:1FD79984B7A79447A308FD947193094D6D3426AB402A667FEC2E9BE380B90945.
  Все6 прежних rollback сохранены, ничего из них не удалялось.
- Атомарно заменены только EXE, два runtime-компонента, VERSION, Readme.md и
  CHANGELOG.md. Каждый временный файл сверялся с source SHA256 до os.replace;
  **SHA256SUMS.txt заменён последним**. Добавлений/удалений paths нет;
  TRAVEL_EDITOR.md остался побайтно прежним. При ошибке был предусмотрен
  откат только выполненных замен из verified backup; откат не понадобился.

- После установки: **135,127/127 managed hashes PASS**,
  manifest совпадает со staging. **23878 пользовательских файлов**
  вне manifest (настройки/карты/сохранения/логи/кэши и прочие файлы)
  хешированы до/после:0 расхождений. Финализатор на установке не запускался;
  редактор/игра интерактивно не открывались.

| Установленный файл | SHA256 |
|---|---|
| EIMapperEndurance.exe |`12C10B81846545510BF8465C725BDF978F8CDC66F0EB383968897900D08AFB43`|
| SHA256SUMS.txt |`8AC0A85ABF127BB83932AD572455991D647B0D3D3922A4CC9F2F76FA81DAA319`|
| runtime/EIMapperEnduranceJmvBridge.plg |`22C7088C51CD1C60B3263E92FB020F45869F0EA25F8813A30F34368A3D550C83`|
| runtime/EIMapperEnduranceRuntimeHost.exe |`F8277298B271D493EF64A11ED5FF0533F5D5F0F9CFC303119D2F0ED905CDB518`|
| TRAVEL_EDITOR.md |`945D3E7B5905944D8735D07E518063DBC04BCDA15887CFD5B119F463EF1ACC83`|

Установка: `artifacts/EI-Mapper-Ataman-2-first-test` относительно корня.
[Install receipt](../../build-tests-135-final/install-receipt.json),
[verified backup](../../build-tests-135-final/install-backup-receipt.json),
[process guard](../../build-tests-135-final/install-prewrite-process-guard.out).
Точный состав127, CRLF и UTF-8 без BOM сохранены. CHANGELOG.md установки
наследует прежнюю запись о блоке из неизменяемого staging; актуальное решение
и завершённая установка зафиксированы здесь и в ROADMAP.

Статус: **передача/установка135 завершены**. Mixed-edge экспериментальная,
**ВЫКЛ по умолчанию**; известные вытянутые штрихи на траве/камень-земля
`build-tests-135-bake/sharp-edge/*5x5*.png` остаются. World45/1/0 — прежний
FAIL134, не PASS; ручная/художественная/игровая приёмка остаётся владельцу.
Scoped-коммит main только этого отчёта и статуса135 в ROADMAP, identity Codex
через переменные окружения команды, trailer Claude Opus5.5; без push и
изменения глобальных Git settings. Следующий шаг — ручная приёмка владельцем.
