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
