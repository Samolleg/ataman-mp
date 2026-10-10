# EI-WORLD-SCULPT-TEST-FAIL-20261011

Дата: 2026-10-11. Задание владельца через Claude: исследовать World terrain
45 PASS / 1 FAIL, найти первое неверное состояние, проверить историю на снимках,
исправить минимальный программный дефект с регрессом либо доказать ошибку данных/теста.

## Разрешение и изоляция

- Вход: `main`, `b7f7bd8d67bde3075ac350688f1edf5049272e48`; AGENTS.md SHA256
  `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
  Действующие CONTRIBUTING, PROJECT_CONTRACT и TEST_BUILD_HANDOFF прочитаны;
  карта 138 прочитана, не изменена. Приоритет имеют ограничения этого задания.
- Git только для чтения: status/rev-parse/log/show/ls-tree/cat-file/diff;
  без add/commit/fetch/push/веток/worktree. Номер версии назначает сопровождающий;
  VERSION в базе — 138, данный результат ещё не является новой версией/коммитом.
- Все сборки, снимки, фикстуры и harness находятся в `build-tests-worldfail/`.
  Qt 5.12.12 / MinGW 7.3 x64 читаются из существующего toolchain Endurance;
  сеть, установка зависимостей, подагенты и интерактивный запуск отсутствуют.
  Навыки evil-islands-modding/private-engine-reference недоступны и не понадобились:
  исследовались обычный код этого проекта и явно разрешённые копии MPR,
  приватные исходники движка не читались.
- Проверяемые EXE собраны из точных Git blobs назначенного HEAD с наложением
  только двух собственных файлов. Исторические снимки собраны отдельно.
  Генерация переводов/release wrapper не применялись; `source/app/main.cpp`
  и все файлы параллельных задач сохранены.
- Постоянная установка, Base, Endurance и живые карты не изменялись.
  Чтение текущих Dejavu MPR использовано только для сверки разрешённых фикстур.

## Что падает и на каких данных

`WorldTerrainTests::installedZone11Zone12ReadOnly`, режим
`EWorldSculptMode::BoundaryFill` (число 4, «Заполнение по границе»).
При отсутствии `EI_WORLD_SCULPT_MAPS` весь этот случай даёт SKIP.

Тест заново загружает карты для каждого из шести режимов. Размещение:
`zone11` origin=(0,0), `zone12` origin=(512,324.5). Общая кисть:
center=(512,350), radius=7, стандартный Smooth falloff, worldDelta=0.5,
worldFlatten=15, strength=0.7, seed=11. Для BoundaryFill delta/flatten/seed
не определяют целевую поверхность: она интерполируется по внешней окружности.

Измеряется разница **видимых кусочно-линейных высот реальных смещённых рёбер**
при x=512 и y=344…356 с шагом0.125: 97 проб. Это проверка согласованности
стыка общей кисти, а не сравнение сырых uint16 или цветов текстур.
Порог не менялся: `max(maxZ11,maxZ12)/65535 + 1e-7`.
В исходниках тест затем проверяет сериализацию/reopen в памяти, Undo и
неизменность исходного файла; до исправления FAIL прерывал эти шаги для mode4
и не давал дойти до mode5 этой пары.

Для воспроизводимости взяты **новые приватные копии прежних фикстур135**
из `build-tests-135-bake/world-maps`, первоначально скопированных с
`C:/GOG Games/PZ/MODS/DejaVu/Maps`. Все тесты работают с
`build-tests-worldfail/world-maps`, без сохранения в live.

| Карта | SHA256 использованной копии |
|---|---|
| zone11.mpr | `0ce97df95e1d5d9f0a422976a5049b81c8a1bd6d157f823d03bdfaf16b82a415` |
| zone12.mpr | `135a8e359a99d3545852536021247807ba2a2b3192971ae0be0c54e33c98dde1` |

Обе копии точно совпадают с текущими live MPR по полному SHA256. Для всего
world-набора также скопированы zone1, zone2, zone3obr, zone4, zone6, zone13.
Отдельные optional случаи используют zone2; zone6/4; zone11/12/13;
zone1/2/3obr. Все пять выполнены, SKIP0. Синтетика создаётся через существующий
`particle-flat-7x5.mpr`; новые игровые бинарные данные в Git не добавлены.

Zone2 и Zone3obr в fixture135 отличаются от нынешних live по полному SHA
после ранее принятого semantic refresh137. Они не участвуют в данном FAIL;
это не подмена входов135. Все8 копий и текущие8 live hashes сверены до/после.
[Полные источники и SHA](../../build-tests-worldfail/maps.json).

## Первое неверное решение и числовая цепочка

Программный дефект, порог корректен. `knitLocalRims` сначала задаёт один
логический узел и общую мировую высоту, затем представляет её в разных
uint16 масштабах карт. Небольшое различие после этого округления допустимо.
`boundaryFill` ошибочно брал **отдельный** `height(t,x,y)` как baseline смеси
каждой карты в одном и том же узле. Одинаковые target и blend поэтому давали
две различные вещественные высоты, а повторное округление увеличивало зазор.
Это первое неверное решение; подготовительная погрешность сама по себе
ещё укладывается в точность MPR. Геометрия узлов и целевая поверхность здесь совпадают.

Худшая точка: world=(512,346); zone11 vertex=(512,346), zone12 vertex=(0,22).
На обеих сторонах actual XY=(512,346), не quarter-step геометрическая погрешность.

| Величина | zone11 | zone12 |
|---|---:|---:|
| maxZ | 30.16309928894043 | 28.250459671020508 |
| Квант высоты maxZ/65535 | 0.0004602593925221703 | 0.0004310743827118411 |
| Подготовленный raw | 27683 | 29558 |
| Подготовленная мировая высота | 12.74136076319124 | 12.7416966041966 |
| Общий target | 13.097945057998945 | 13.097945057998945 |
| Общий blend | 0.27551020408163268 | 0.27551020408163268 |
| Ошибочная смесь до final round | 12.839603375026016 | 12.83984668840745 |
| Итоговый raw до исправления | 27896 | 29786 |
| Итоговая мировая высота до исправления | 12.839396013798462 | 12.8399815634549 |

Финальный gap=0.000585549656438, порог=0.0004603593925221703.
До мазка максимум на измеряемом участке=0.247598828.
После общего baseline=12.74152868369392 обе смеси дают
12.839725031716734. Итоговые raw=27897/29785; gap=0.000305784118797
(совпадает с максимальным зазором исправленного mode4).

Диагностические QINFO добавлялись только в собственный исследовательский
снимок; в предлагаемый коммит они не входят.
[Числа до округления](../../build-tests-worldfail/diagnostic-values-qtest.txt),
[арифметический receipt](../../build-tests-worldfail/rounding.json).

## История main: что удалось установить

Проверены Git-снимки с **одними и теми же fingerprinted входами135**;
checkout/индекс/ветка не переключались. Команда выбора случая:
`world_terrain_sculpt_tests.exe installedZone11Zone12ReadOnly -o <log>,txt`
с `EI_WORLD_SCULPT_MAPS=build-tests-worldfail/world-maps`.

| Снимок | SHA | Результат mode4 на нынешних фикстурах |
|---|---|---|
| 65, родитель66 | база перед66 `563b794b1695b5d316e0396b5a3efe0d9b9a7e43` | данного installed-теста ещё нет; не PASS и не good endpoint бисекции |
| 66, первое появление installed-теста и knitting | `6c2e63372a7932d50ec10e963c5fdf26537be200` | FAIL, gap0.000585550 |
| 67, исправление T-junction | `715968b8c6b4686c3a515753c44ef2e00ddd211f` | FAIL, gap0.000585550 |
| 71, quarter-shift | `0b03ac599e154b23ad014b07e97dd7cb48718193` | FAIL, gap0.000585550 |
| HEAD, точный снимок без исправления | `b7f7bd8d67bde3075ac350688f1edf5049272e48` | полный набор45/1/0, gap0.000585550 |

После71 `world_terrain_sculpt.cpp` по истории main не менялся. В66 добавлено
предварительное knitting/округление; отдельная per-map смесь существовала раньше.
Таким образом, **на фиксированных нынешних данных дефект присутствует с первого
коммита66, где введён нынешний seam-контракт и installed-тест**, задолго до134–137.
[66](../../build-tests-worldfail/v66-qtest.txt),
[67](../../build-tests-worldfail/v67-qtest.txt),
[71](../../build-tests-worldfail/v71-qtest.txt),
[полный HEAD](../../build-tests-worldfail/baseline-recorded-qtest.txt).

Обычная бинарная бисекция PASS→FAIL здесь не имеет достоверного good endpoint:
до66 теста нет, а первый доступный его снимок уже FAIL на фиксированных входах.
Исторический [отчёт66](ATAMAN_UNIFIED_TERRAIN_66.md) утверждает32/0/0,
initialGap0.052095 и finalGap0.000277–0.000386 на прежних installed-данных.
Их прежний hash/копии и указанные старые build-test-world-tools журналы
в нынешнем checkout отсутствуют. Современный запуск того же66 имеет
initialGap0.247598828. Это свидетельство несовпадения входов с тем старым
прогоном, но не доказательство конкретной правки владельцем и её даты.
Определить первый **фактический исторический** FAIL по дате изменения данных
без прежних MPR нельзя. [Отчёт130](ATAMAN_WORLD_BRUSH_SWAMP_SWITCH_130_20261010.md)
фиксировал41 PASS / 5 SKIP: installed-пара тогда не выполнялась и не является PASS.

Существующее несовпадение высот live до мазка не объявляется повреждением MPR:
редактор намеренно должен согласовывать стык общей кистью. Дефект доказан
отдельно на синтетике, поэтому изменение порога или ремонт live данных не нужны.

## Минимальное исправление и регресс

- `boundaryFill` собирает подготовленные высоты только для назначенных
  `rimKeys` и использует их упорядоченное среднее как общий baseline.
  Целевой контур, blending/falloff, замороженные anchors, limits и uint16
  округление остаются прежними. Остальные вершины используют прежний baseline.
  Clipped fill уже смешивал от общего mean; его код не менялся.
- Один итоговый общий world value с округлением по каждому maxZ ограничивает
  разницу величиной `(quantumA+quantumB)/2`, не превосходящей прежний max quantum.
  Порог installed-теста не изменён ни на один символ.
- Новый `boundaryFillPartialStrengthSharesMixedScaleRim`: 18 комбинаций
  shift0/0.5 × strength0.2/0.7/1 × Constant/Linear/Smooth, 1458 проб стыка.
  Для каждой проверены reversed targets, serialization/reopen36 sector payloads,
  дальние точки и cardinal contour, точный обратный apply, Undo/Redo.
  На неисправленном HEAD регресс действительно FAIL:
  shift0, strength0.2, Constant, y21, gap0.000490692277209 > quantum0.000460259392522.
  [До исправления](../../build-tests-worldfail/regression-before-qtest.txt).
- Новый `boundaryFillOverlappingMapsRetainSeparateBaselines` проверяет,
  что совпадающие interior XY перекрывающихся карт не становятся seam:
  half-strength center остаётся25000/35000, contour20000/40000; Undo точен.
  Этот случай сохраняет исходное поведение, без исправления также PASS.

## Проверки

| Проверка | PASS / FAIL / SKIP | Evidence |
|---|---:|---|
| Исходный полный World HEAD | 45 / 1 / 0, exit1 | [baseline](../../build-tests-worldfail/baseline-recorded-qtest.txt) |
| Новый синтетический дефект без исправления | 2 / 1 / 0, exit1 (init/cleanup включены) | [negative control](../../build-tests-worldfail/regression-before-qtest.txt) |
| Полный World, финальная правка | **48 / 0 / 0**, exit0 | [qtest](../../build-tests-worldfail/fixed-world-final-qtest.txt) |
| Terrain virtual atlas | **146 / 0 / 0**, exit0 | [qtest](../../build-tests-worldfail/terrain_virtual_atlas_tests-qtest.txt) |
| Terrain resolve audit | **5 / 0 / 0**, exit0 | [qtest](../../build-tests-worldfail/terrain_resolve_audit_tests-qtest.txt) |
| Terrain recipe core | **17425 / 0 / 0**, exit0; edges18241, RGBA2240132 | [stdout](../../build-tests-worldfail/terrain_recipe_bake_tests.out) |
| world_brush_130_integration | **8/8 cases**, Qt24 / 0 / 0, exit0 | [stdout](../../build-tests-worldfail/integration.out) |
| --scenario-layer-check | **50 / 0**, exit0 | [stdout](../../build-tests-worldfail/scenario.out) |

Atlas/audit/recipe не зависят от изменённого world sculpt; их успешные результаты
не повторялись после сужения baseline до rimKeys. World и integration пересобраны
после относящейся правки; финальный полный World повторён по изменившемуся поведению.
Сборка интеграции включает неизменённый production main из назначенного снимка,
а не параллельные незавершённые изменения checkout.
Для standalone UI-случаев добавлен только временный entry-point wrapper в
`build-tests-worldfail/instrumentation`: штатный TestClipboardSession создаётся
перед QApplication, устанавливается до виджетов и проверяет teardown.
Production/test sources снимка не редактировались; production scenario уже
имеет собственную изоляцию. [Wrapper](../../build-tests-worldfail/integration-instrumentation.json).
Первая сборка wrapper ошибочно включила одновременно production main.o и
тестовый main: linker exit2, multiple definition of main; этот FAIL исправлен
явным исключением main.cpp из временного .pro. Конечная сборка exit0.

Финальная provenance-сверка:1839 tracked blobs,1837 точных неизменённых blobs
назначенной базы и2 собственных overlays, совпадающих с checkout. Все8 копий
и8 исходных live MPR сохранили hashes. EXE интеграционного harness после изоляции:
SHA256 `fbd56f7f410a889bb470c90e741596cd99fa08bee52a9c018c204494d496c9a8`.
Это инженерный тестовый EXE, без выдачи/установки пакета.
[Source/data receipt](../../build-tests-worldfail/final-audit.json).

Все UI-случаи и scenario последовательно выполнены под атомарным
`mkdir build-locks/ui-tests.lock` с собственным owner.txt, после освобождения
чужого lock138. Собственный lock снят в finally с проверкой токена.
Системный буфер не открывался, не блокировался, не очищался и не записывался:
8/8 child-процессов integration и scenario выдали PASS sentinel,
`backend=in-memory nativeAttempts=0 hookedImports=14`, sequence36501→36501,
formats7→7, unchanged1. Родитель integration — только QCoreApplication driver.
Использован штатный sentinel базы, без его изменения/ослабления.
Scenario также подтвердил очистку своего synthetic recovery snapshot в Qt test mode.
[Lock acquisition](../../build-tests-worldfail/ui-lock.json),
[release](../../build-tests-worldfail/ui-lock-release.json).
Non-exposed-window OpenGL swapBuffers warnings integration сохранены в логах;
случаи завершились PASS, без таймаутов/SKIP. Это не ручная/игровая приёмка.

Предварительная полная World-сборка до ограничения rimKeys дала47/0/0;
окончательный результат —48/0/0, она не является выдаваемым артефактом.
Первый baseline-запуск QtTest не писал в redirected stdout; отчётный прогон
выполнен с явным `-o ...,txt`. Ошибка подготовки snapshot из-за Git quoted UTF-8
path исправлена использованием `ls-tree -z`; неуспешный qmake до полного снимка
не объявлен PASS. Все конечные build/test exits проверены отдельно.

## Точный состав для scoped-коммита сопровождающим

1. `source/documents/world_terrain_sculpt.cpp`
2. `tests/world_terrain_sculpt_tests.cpp`
3. `docs/validation/ATAMAN_WORLD_SCULPT_TEST_FAIL_20261011.md`

Никакие build outputs, приватные MPR, VERSION, Readme, ROADMAP, CHANGELOG,
translations, main.cpp или чужие файлы в этот список не входят.

## Предлагаемый текст ROADMAP (не записан в ROADMAP.md)

```text
EI-WORLD-SCULPT-TEST-FAIL-20261011 — исправление повторного округления World BoundaryFill
Проблема: installedZone11Zone12ReadOnly/mode4, gap0.000585550; полный результат — причина, история на фиксированных копиях, минимальное исправление и регресс без изменения данных/порога.
База main b7f7bd8d67bde3075ac350688f1edf5049272e48; разрешены только выделенные world-файлы, тесты и отчёт; без Git записи, live/установки/сети/подагентов.
Причина: частичное смешивание от двух отдельно квантованных baseline одного rim knot; теперь общий mean используется только для rimKeys.
Проверки: World 48/0/0, mode4 gap 0.000305784; 18 синтетических сочетаний, отрицательный контроль FAIL; overlap-regress; atlas 146/0/0, audit 5/0/0, recipe 17425/0/0; UI integration 8/8 (Qt 24/0/0), scenario 50/0 под lock; все 9 sentinel nativeAttempts=0 / metadata unchanged.
История: воспроизводится на66/67/71/HEAD с нынешними входами; старые фикстуры66 отсутствуют, дата первого реального FAIL не установлена; SKIP130 не PASS.
Статус: FIXED AND VALIDATED, готово к интеграции сопровождающим. Следующий шаг: назначить согласованный номер и scoped-коммит трёх файлов; установка/live/push этим заданием не разрешены.
Evidence: docs/validation/ATAMAN_WORLD_SCULPT_TEST_FAIL_20261011.md.
```

## Предлагаемый текст CHANGELOG (номер назначает сопровождающий)

```text
EI-WORLD-SCULPT-TEST-FAIL-20261011: устранён лишний зазор на стыке карт с разными maxZ при частичной силе «Заполнения по границе». Общие узлы смешиваются от единой мировой высоты перед финальным округлением; контур, Undo/Redo, сохранение и раздельные baseline перекрывающихся карт сохранены. Порог теста и игровые данные не изменены.
Проверки: World 48 PASS / 0 FAIL / 0 SKIP; zone11/12 mode4 gap 0.000305784 вместо 0.000585550; 18 synthetic combinations и negative control; terrain atlas 146, audit 5, recipe 17425; world_brush_130 8/8 (Qt 24/0/0), scenario 50/0 под lock, все 9 clipboard sentinel nativeAttempts=0 / metadata unchanged.
```

Ручная работа мышью/художественная и игровая приёмка не выполнялись.
Установка и коммит этим поручением исключены. Разрешённая реализация и полный
заданный набор проверок завершены; результат готов к интеграции сопровождающим.
