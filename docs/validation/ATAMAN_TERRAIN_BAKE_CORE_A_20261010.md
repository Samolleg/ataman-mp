# EI-TERRAIN-BAKE-CORE-A — ядро рецептов mixed-edge

Дата: 2026-10-10. Поручение владельца через Claude; этап A, без подключения к Modern.
Ветка `main`, база `df91a69413b0c924e06bbfe2126b9e4d31c307c2`, VERSION 131.
Номер следующей версии назначает сопровождающий при интеграции; 132 в имени
private build-каталога не является новым номером программы.

## Результат

Добавлен автономный `CTerrainRecipeBaker`. Он принимает поле вершин Modern,
проверенные runtime-классы материалов, непрозрачные однородные изображения
профиля и pair-specific ownership banks. Возвращает рецепты каждой клетки,
RGBA-тайлы, ссылки клеток на дедуплицированные тайлы, диагностику и бюджет.
Данные входа не изменяются, генератор не выполняет файловых записей.

Ключ конечной grammar — упорядоченная сигнатура NW/NE/SE/SW, runtime class и
interior variant. В рецепте явно записаны четыре упорядоченных ребра и state=0:
единственное поддержанное состояние, midpoint crossing и фиксированная pair
phase. По умолчанию N=4, у однородных групп максимум 4. До min(N, число клеток)
вариантов назначаются сбалансированно через SHA256 координат и суммы углов,
как в прототипе. Диапазон N — 1..256, raster size — 2..1024, default 128.
Рецепты сохраняют ориентацию: поворот сигнатуры сам по себе не позволяет
вращать/объединять разные пиксели при фиксированной фазе общей границы.

Внутри клетки используются bilinear scores, low-frequency shape perturbation,
численный градиент с подавлением off-edge компонента, stock-полоса шириной
65 отсчётов и длиной 512, normal scale=1.15, ownership levels 0/.25/.5/.75/1.
Текстурная смесь и цветовая поправка совпадают со схемой stage1b mixed-edge.
Из однородных исходников удаляется рамка width/16, выборка периодическая;
локальные координаты крайних пикселей точно включают 0 и 1.

Общий шов обеспечен значением `16*x*(1-x)*y*(1-y)=0` на границе. Endpoint
scores, tangent gradient, pair phase и базовая периодическая текстура зависят
только от общего ребра. Внутренние варианты не меняют его bytes. Это C0;
касательные/C1 и произвольный мировой контур не обещаются.

Runtime-классы 0..15 разрешаются отдельно от semantic IDs: majority по
покрытию углов **классами**, при tie нужна предпочтительная/рисуемая material
class либо явная preferred class из числа tied. Неизвестный класс, материал,
источник или pair bank вызывает отказ. Отдельный явно включаемый режим
`preserveReviewedCellRuntimeTypes` соответствует MVP exact-predecessor route:
каждый класс клетки должен быть предоставлен и находиться в 0..15.

Дедупликация использует SHA256 полных RGBA bytes без scanline padding +class.
Одинаковые pixels разных классов остаются разными physical identities.
Семантика сохранена в рецептах клеток, включая случаи alias одного pixel slot.

Бюджет содержит finite recipe count, measured pixel count, резерв caller'а
под pinned other-layer/animation slots, итог и округление страниц по 64.
Пороги строго `>2048`, `>4096`, `>8192`; по умолчанию slotLimit=4096.
Превышение лимита даёт success=false и явную диагностику, сохраняет
полученные тайлы; материалы не заменяются. На отвергнутом плане без raster
поле pixelDedupComplete=false отличает conservative recipe estimate от
измеренной pixel deduplication. Пороги не являются гарантией игровой памяти.

## Stock-источники и сложные узлы

В C++-исходниках нет игровых изображений/масок/ID материалов Zone6.
`estimateStockOwnership` оценивает RGB projection относительно пары uniform
tiles и residual. `extractStockStrip` для supplied 128px profile tile определяет
физическую ориентацию по corner samples, извлекает coarse contour и вдоль его
arc length переносит **неразмытые** ownership samples. Рассматриваются все
четыре поворота straight/1:3/alternating topologies. Неоднозначные corners,
RGB RMSE >=14 или отсутствие чистых сторон приводят к отказу.
`makeBrush` собирает strips в deterministic periodic bank с pure-side taper.

Это оценка RGB ownership, не восстановленная авторская alpha. CPU Gaussian
coarse guide задан явно; он не претендует на побайтную эквивалентность Pillow
GaussianBlur. Отбор лучшей uniform-пары и reviewed pooling нескольких stock
slots остаются обязанностью будущего profile adapter: API предоставляет
fit/strip/bank операции и не загружает глобальные профили самостоятельно.

Трёх- и четырёхматериальные узлы по умолчанию явно отвергаются; возвращаются
unsupportedCells и исходные recipes, без raster и подмены углов. Только
`diagnosticJunctionPreview=true` разрешает исследовательский top-two preview
как в stage1b. Такой результат сохраняет unsupported diagnostics,
previewOnly=true и **success=false**, даже если все preview pixels получены.

## Проверки

Qt 5.12.12 win64_mingw73 / MinGW 7.3 x64 из существующего kit; C++11 Release.
Зависимости не устанавливались. `.pro` линкует только новый модуль и тест;
`terrain_edit_model.h` используется как read-only договор сигнатуры.

- Финальный synthetic test: **466 PASS / 0 FAIL / 0 SKIP**.
  Все 64 поля из шести бинарных вершин по каждой из двух осей; uniform,
  straight, corner и alternating соседи. Полная случайная сетка 32x32 с
  всеми 16 сигнатурами и N4/N8. **4096 shared edges**, **405504 RGBA channel
  comparisons**, различий 0. Проверена повторяемость label/bytes/dedup order,
  независимость от порядка контейнера материалов, majority/tie, rotation
  independence, класс 15, typed dedup, неверные классы/материалы/пары/NaN,
  плохие размеры и N, overflow без подмены. Проверены 9 точек вокруг трёх
  бюджетных порогов и насыщение quint64 arithmetic.
- Runtime extraction synthetic: известная RGB смесь даёт alpha=.5/RMSE=0;
  повторное извлечение strip совпадает; pure tails bank проверены.
  Все **14** неодноцветных двухматериальных stock-сигнатур, включая повороты
  1/3 и обе alternating branch orientations, извлекаются без ошибки.
- Финальный Zone6 diagnostic run: **5 PASS / 0 FAIL / 0 SKIP**;
  **439 recipe slots / 439 typed pixel tiles**, 7 страниц по 64.
  **1984 shared edges**, **1015808 RGBA channel comparisons**, различий 0.
  Все **250** unsupported cells (249 triple +1 quadruple) явно перечислены.
  Это диагностический preview с сохранением reviewed predecessor classes.
- Independent PNG readback: **439** typed RGB tiles, **1984** edges,
  **761856 RGB channel comparisons**, max seam difference **0**.
  Против Python mixed-edge одинаковы **16777119/16777216** пикселей:
  отличаются 97, mean absolute channel error **0.0000075896581**, max **17**.
  Побайтная эквивалентность Python не заявлена; различия находятся в редких
  пороговых отсчётах при переносе численных вычислений в C++.
  Общая сравнительная картинка и одинаковый closeup 5x5 просмотрены.
- После успешной WIP-компиляции полные `.h/.cpp` перенесены в source;
  их SHA256 совпали с проверенным WIP. `.pro` собран повторно с default
  source path, финальные проверки относятся к этому binary.
- qmake **текущего `ei_maper.pro`** и компиляция только
  `release/terrain_recipe_bake.o` с настоящими Release-флагами проекта:
  **exit 0**, без compiler warnings/errors. Полная сборка не выполнялась.

Zone6 (88,136), 32x32 использует runtime-пересозданные stage1b banks из
profile atlas в собственном private каталоге. Перед этим проверены source
MPR SHA и decoded RGBA fingerprint всех **1216** slots; 4 source snapshots
сохранили SHA после обработки. Это проверка C++ renderer против прототипа;
полный C++ stock-selection adapter на живом профиле этим опытом не проверен.
Неоднородные classes одного semantic material в stock-каталоге не угадываются:
Zone preview явно сохраняет predecessor classes. Synthetic generation
отдельно проверяет обычное правило majority/tie.

Evidence только в `build-tests-132-bakecore/`:

- [compile-source-tests.log](../../build-tests-132-bakecore/compile-source-tests.log),
  [synthetic-final.log](../../build-tests-132-bakecore/synthetic-final.log),
  [zone-final.log](../../build-tests-132-bakecore/zone-final.log);
- [compile-project-object.log](../../build-tests-132-bakecore/compile-project-object.log);
- [zone-readback.json](../../build-tests-132-bakecore/zone-readback.json),
  [zone-input-receipt.json](../../build-tests-132-bakecore/zone-input-receipt.json),
  [receipt.json](../../build-tests-132-bakecore/receipt.json);
- [zone6-cpp-mixed-edge.png](../../build-tests-132-bakecore/zone6-cpp-mixed-edge.png),
  [zone6-comparison.png](../../build-tests-132-bakecore/zone6-comparison.png).

Промежуточные технические отказы сохранены отдельно и не считались PASS:
qmake CLI split absolute WIP path с пробелом; Qt slots macro в имени
test-переменной; нормализация пути в private fixture; неоднозначный source
class uniform slots; неверный relative путь первого final qmake. Исправлены
собственные инструменты/тесты, правило majority/tie не ослаблено.

## Границы и передача

AGENTS.md при входе и перед передачей: SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`,
совпадает с предоставленными инструкциями. Учтены CONTRIBUTING,
PROJECT_CONTRACT, PARALLEL_THREAD_WORKERS, TEST_BUILD_HANDOFF, active terrain
ROADMAP, Modern MVP, этапы 0/1/1B и synthetic-memory evidence.

Запись только в назначенные новые файлы и собственный build-tests.
Git использован только для чтения. ROADMAP, CHANGELOG, VERSION, Readme,
translations, существующий код и чужая параллельная задача не редактировались.
Сети, новых агентов/чатов, UI, clipboard, GL, игры и интерактивного редактора
не было. QCoreApplication/QImage не требуют ui-tests.lock; никакие проверки
из защищённого UI/scenario/dark/gallery/clipboard набора не запускались.
Постоянная установка, Endurance kit и живые карты/мод/игра не изменены.
Private-engine/modding skills недоступны; приватный engine-код не читался,
работа использует только обычный код Ataman и разрешённые profile pixels.

Ограничения этапа A: balanced labels могут переназначиться при изменении
состава группы; этап B должен сохранять label либо выбрать стабильную
координатную политику. Тут нет Modern wiring, profile loader/selection,
publication, DXT/gutters/mips, overlay/animations, streaming full-map bake,
save/reopen/Undo integration или игровой/художественной приёмки. Все
deduplicated RGBA tiles сейчас остаются в памяти результата; production
streaming/memory policy нужна при интеграции. Эти пункты не закрываются
source-object compilation или PNG seam checks.

Точный список для scoped-коммита сопровождающим:

1. `source/documents/terrain_recipe_bake.h`
2. `source/documents/terrain_recipe_bake.cpp`
3. `tests/terrain_recipe_bake_tests.cpp`
4. `tests/terrain_recipe_bake_tests.pro`
5. `docs/validation/ATAMAN_TERRAIN_BAKE_CORE_A_20261010.md`

`build-tests-132-bakecore/` и все игровые fixture/PNG/binary в Git не включать.
Версию, roadmap/changelog/readme сопровождающий интегрирует отдельно.

## Предлагаемый текст ROADMAP

### EI-TERRAIN-BAKE-CORE-A — ядро рецептов mixed-edge

- Поручение владельца через Claude 2026-10-10; база main df91a69413b0c924e06bbfe2126b9e4d31c307c2, VERSION 131. Этап A: только автономный C++ модуль и тесты, без Modern/UI wiring, живых данных, установки и Git-записей; версия назначается при интеграции.
- Результат: finite N4 recipes по shared vertex field с фиксированным edge state, mixed-edge raster, class-aware pixel dedup, majority/tie 0..15, runtime stock fit/strip/bank API, явные unsupported junctions и бюджеты >2048/>4096/>8192 без замены материалов.
- Подтверждение: synthetic 466 PASS/0 FAIL/0 SKIP; 4096 рёбер побайтно совпали. Zone6 diagnostic 439 tiles, 1984 бесшовных рёбра; 250 сложных узлов явно unsupported, success=false. Default .pro и объект нового модуля с флагами ei_maper.pro компилируются. Отчёт docs/validation/ATAMAN_TERRAIN_BAKE_CORE_A_20261010.md.
- Статус: реализация и локальная проверка этапа A готовы к scoped-интеграции сопровождающим. Следующий шаг — отдельный этап B: profile adapter, Modern wiring и сохранение labels. Родительская цель production bake, сложные узлы, DXT/mips/overlay/animation/save и художественная/игровая приёмка остаются открыты.

## Предлагаемый текст CHANGELOG

- EI-TERRAIN-BAKE-CORE-A: добавлено автономное ядро рецептов N4 с кромкой mixed-edge, общими побайтно одинаковыми рёбрами, runtime class majority/tie, typed pixel dedup и явными бюджетами 2048/4096/8192. Stock ownership извлекается из переданных profile pixels; неизвестные классы/источники и 3/4-material production junctions отвергаются без подмены. В Modern пока не подключено. Проверки: 466 synthetic PASS, Zone6 diagnostic 439 tiles/1984 бесшовных рёбра, source object с флагами проекта компилируется; docs/validation/ATAMAN_TERRAIN_BAKE_CORE_A_20261010.md.
