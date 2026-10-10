# EI-TERRAIN-BAKE-B1-JUNCTIONS — композиция узлов mixed-edge

Дата: 2026-10-10. Поручение владельца через сопровождающего Claude:
«запускай этап B». Здесь выполнен только B1. Ветка main, база
`9114a68111b68946cc0ebb2f83a80231057bfd9c`, VERSION **134 без изменения**.
AGENTS.md SHA256 `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`
совпадает с переданными правилами. Учтены CONTRIBUTING, PROJECT_CONTRACT,
TEST_BUILD_HANDOFF, текущая ROADMAP, отчёты этапов A/0/1B и существующий код.
Параллельных работ/подагентов нет; исходное рабочее дерево чистое.

## Результат и границы

`CTerrainRecipeBaker::bake()` по умолчанию поддерживает все упорядоченные
сигнатуры из 1–4 материалов. На Zone6 (88,136), 32×32:

| Показатель | A, legacy diagnostic | B1 |
|---|---:|---:|
| Unsupported | 250 (249 тройных + 1 четверной) | **0** |
| Success / previewOnly | false / true | **true / false** |
| Recipe slots / typed unique RGBA | 439 / 439 | **439 / 439** |
| Страниц по 64, без резерва | 7 | 7 |
| Общих рёбер, различий | 1984 / 0 | 1984 / **0** |

Поддержка предполагает корректные материалы, uniform sources, классы и
brush banks **всех** пар внутри клетки. Отсутствующая пара по-прежнему даёт
отказ; generic replacement материалов не добавлен. Legacy
`diagnosticJunctionPreview=true` сохранён специально для сравнения: он
использует старый top-two renderer, помечает узлы unsupported и не может
вернуть success=true. Это явный opt-in исследовательский режим.

Изменены только назначенные ядро/header, тест и prototype evidence tool;
добавлены этот отчёт и верхний пункт ROADMAP. `.pro` теста менять не требовалось.
Ядро **не подключено** к программе; VERSION, CHANGELOG, Readme и ei_maper.pro
не изменены. Сборка программы/передача установки не выполнялись: запрошены
автономный тест и object compilation. Игра, мод, карты, пользовательские
настройки/установка и Endurance не изменены. Qt/MinGW kit только прочитан.
Сеть, установка зависимостей, интерактивный редактор и новые чаты не использованы.

## Подход: все пары, конечная мировая фаза и центр

Сохранены bilinear corner scores этапа A с low-frequency perturbation,
затухающей через `f=16*x*(1-x)*y*(1-y)`. Для каждой пары a<b используется
прежняя приближённая signed metric distance:

`d_ab = (score_b-score_a) / max(length(gradient_ab),0.15) * 127/1.15`.

Это first-order distance, **не точный Euclidean SDF**. Численный градиент
использует eps=1e-4 и прежнее подавление компоненты, поперечной общему ребру.
Stock bank пары остаётся 512×65, ownership quantization 0/.25/.5/.75/1.
Полоса имеет фиксированную pair phase и tangent projection сигнатуры.
Функция pairOwnership выделена из прежнего renderer; для двух материалов
формула и порядок накопления RGB сохранены побайтно.

В B1 нет переключения идентичности двух крупнейших материалов в центре:
для каждого присутствующего материала вычисляется произведение его
попарных долей владения (`a:1-alpha_ab`, `b:alpha_ab`), после чего веса
нормализуются. RGB — выпуклая композиция тех же uniform textures, с прежними
внутренними texture mixing/color modulation. Материал с нулевым score
не участвует. Если квантованные banks создали цикл побед и сумма весов
≤1e-12, используются нормализованные scores, без случайного выбора и NaN.

Чтобы центральная конкуренция трёх/четырёх материалов не превращала
stock-фрагменты в лучи, каждая пара ослабляет высокочастотный ownership
при появлении сравнимого третьего score. Коэффициент:

`centre = smooth((third/max(min(score_a,score_b),1e-12)-0.35)/0.55) * smooth(4*f)`.

Pair ownership плавно смешивается с `smooth((d_ab+12)/24)` только внутри.
Это компактная центральная полоса ±12 единиц normal distance, без morphology
после raster. Не применяется случайный pixel noise или новый edge state.
Цена — более мягкий, иногда мутный центральный стык вместо авторских обломков.
Глобальная гарантия отсутствия любых тонких деталей произвольного stock bank
не заявляется; финальные реальные узлы просмотрены, ограничения ниже.

Запрошенный подход в мировых координатах принят с ограничением конечной
grammar: x/y — дробные координаты мирового grid cell, **не абсолютная
непериодическая world phase**. Как в A, абсолютные origin/координаты выбирают
interior variant; на ребре phase зависит только от общей пары. Если добавить
абсолютную фазу шума в ключ, одни и те же signature/class/variant перестанут
обозначать одно изображение, и измеренный конечный N4-бюджет станет неверным.
Поэтому реализовано pair distance/weight field с совместимыми граничными
условиями этапа A, а не произвольный непрерывный глобальный контур.

На каждом общем ребре f=0: остаются только его endpoint scores, общая
тангенциальная производная, одна pair phase и базовые периодические textures.
Чужие два угла, runtime class, label и степень сложности соседей не входят
в итоговые boundary bytes. Центральное сглаживание там нулевое, renderer
использует исходную двухматериальную композицию. Это **C0**, не C1.
Тот же контракт сохраняется в diagonal 2+2 и 1+1+2: отдельно назначать
центральную пару для них не требуется.

## Runtime class: majority/tie

Правило этапа A сохранено: считают **четыре угла по runtime class**, а не
число разных semantic materials. Алиасы одного класса складываются.
Уникальный максимум побеждает даже при иной preferred class.

- Три разных материала с тремя различными классами имеют покрытие 2+1+1:
  побеждает класс повторённого материала, включая диагональную пару.
- Четыре различных материала/класса дают 1+1+1+1: нужна preferred class
  или preferred material, чей класс входит в tied set. Без неё — class_tie;
  неизвестный/не участвующий в максимуме класс не угадывается.
- **Tie между ровно тремя максимальными классами невозможен** при четырёх
  равно взвешенных углах: 1+1+1 оставляет четвёртый голос, который либо
  делает majority=2, либо создаёт четвёртый tied class. Если будущая модель
  будет считать pixel area/другие веса, её tie policy потребуется отдельно.
- Preferred runtime class имеет прежний приоритет перед preferred material.
  Классы 0..15; режим preserveReviewedCellRuntimeTypes явно сохраняет
  проверенный predecessor class каждой клетки, независимо от majority.

Zone6 preview использует последний режим, как этап A/MVP. Классы uniform
slots этого профиля неоднозначны для некоторых semantic IDs; их автоматическая
интерпретация не выдаётся за новый reviewed profile adapter. Majority/tie
отдельно проверены на синтетике, включая class aliases.

## Бюджет Zone6 и 41 карты

439 этапа A были **полным диагностическим N4**, уже включая unsupported
узлы, а не только двухматериальными recipes. Поэтому против опубликованного
числа 439 рост B1 — **0 тайлов / 0%**. Разложение нового измерения:

| Часть участка | Клетки | Typed recipes/pixels |
|---|---:|---:|
| Однородные + двухматериальные | 156 + 618 | 270 |
| Тройные + четверной | 249 + 1 | 169 |
| Всего | 1024 | 439 |

Относительно поддерживаемого двухматериального подмножества прибавка узлов
**169 / 270 = 62,59%**. Без этих клеток A вообще отказывал на полном поле;
270 не означает прежний успешный полный bake. Все 774 простые клетки
побайтно совпали с A, все 250 сложных изменились.

Пересчитаны finite signature counts всех **41** exact-semantic снимков
DejaVu этапа 0. Для земли использовано shared vertex field, key включает
NW/NE/SE/SW + исходный runtime class; для overlay оставлена его N4 оценка
этапа 0. Перед расчётом проверены SHA живых исходных MPR против экспортов;
снимки/пути и все результаты находятся в corpus-budget.json.

| Grammar estimate | Карт ≤2048 | Карт ≤4096 | Максимум |
|---|---:|---:|---:|
| Исходная N4 статистика этапа 0, SEC signatures | 33/41 | 40/41 | 5207 |
| B1 N4, shared-field land + N4 overlay estimate | **33/41** | **40/41** | **5207** |
| Предложение: junction N2, прочие N4 | 35/41 | 40/41 | 4704 |
| Предложение: junction N1, прочие N4 | 35/41 | 40/41 | 4159 |

Совпадение общей ёмкости не означает совпадение каждой строки: у 16 карт
бюджет отличается от этапа 0 из-за перехода SEC signatures → shared field.
Например zone17: 515→700, zone19: 2217→2274, bz15h:263→295.
Это учитывает восстановленное поле, а не скрытую запись/ремонт карты.

Полная Zone6: **3401 land + 4 overlay estimate = 3405**, из land 1696
простых +1705 узловых recipes (+100,53% к простой части). Выше 2048 остаются
zone19, zone3obr, zone4, zone6, zone6_2, zone7, zone8, zone9; выше4096
только zone9 (5207). Если игнорировать узлы, 40/41 и 41/41 укладывались бы
в 2048/4096 соответственно, но это неполные карты.

Предлагается для ограниченного budget caller сначала снизить N только для
узлов: их labels не влияют на boundary bytes. N2 помогает двум картам у
порога2048, но не решает zone9; даже N1 остаётся4159 до pinned reserve.
Для zone9 нужны дополнительно selective уменьшение других групп, реальная
pixel dedup или отдельный бюджет. В B1 такая policy **не включена**:
default N4 и существующие label assignment сохранены.

Это **корпусная grammar estimate**, не полный raster bake, не physical atlas
packing и не доказанная ёмкость игры. Не измерены дополнительные animation/
pinned slots и реальная публикация overlay. При production надо считать
их резерв и pixel dedup с реальным profile adapter; generic majority route
может иначе группировать runtime classes. Одних этих оценок для разрешения
публикации конкретной карты недостаточно.

## Проверки и воспроизводимость

Qt5.12.12 win64_mingw73 / MinGW7.3 x64, Release C++11; существующий kit.
Python: `C:/Users/samol/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
Использованы ранее установленные numpy/Pillow, без установки.

1. До правок исходный test binary A: **466 PASS /0 FAIL /0 SKIP**.
   В обновлённом наборе сохранены 466 проверок A; только два ожидания отказа
   3/4-узла изменены на ожидания поддержанного bake. Legacy-preview refusal
   и остальные 464 проверки сохранены.
2. Финальный расширенный тест: **17399 PASS /0 FAIL /0 SKIP**.
   Все **8192** adjacent fields (4^6, обе оси) при tile9: каждый допустимый
   сосед uniform/2/3/4 и все диагональные configurations; **256** ordered
   cell signatures при tile33 с majority/tie и повторяемостью bytes.
   Random N4 grid32×32 всех четырёх материалов при tile128, container-order
   independence, N8 grid при tile33, preferred aliases и missing-pair refusal.
   Всего **16256 shared edges**, **1978112 RGBA channel comparisons**,
   различий0. Прежние strict budgets/overflow, strip extraction и bad inputs
   остаются в этом наборе. SKIP0, таймаутов/падений теста нет.
3. Zone6 B1: **5 PASS /0 FAIL /0 SKIP**; success=true, unsupported0;
   1984 edges /1015808 RGBA comparisons, различий0; 439 typed unique.
   Legacy A comparison: **5 PASS /0 FAIL /0 SKIP**, unsupported250 и
   success=false проверены как ожидаемый diagnostic результат.
4. Независимый PNG readback: **439 typed RGB unique**, **1984 edges**,
   **761856 RGB comparisons**, max difference0. Legacy A полностью совпал
   с прежним A PNG; все uniform/binary клетки B1 совпали с ним.
5. Источники: source MPR/JSON SHA совпали с receipt этапа A, fingerprint
   **1216 RGBA atlas slots** совпал. Потреблённые uniform PNG/banks скопированы
   только в private B1; SHA до/после обработки совпали. Это snapshot-based
   renderer check, не новая загрузка live atlas через adapter.
6. qmake **текущего ei_maper.pro** и target `release/terrain_recipe_bake.o`:
   exit0, **без compiler warnings/errors**, `ENDURANCE_VERSION=134`.
   Полная программа не собиралась. Автономный default test .pro также exit0
   без warnings. Объектные файлы/Makefiles/binary не коммитятся.
7. Final preview **2040×720**, пять comparison closeup **1920×684**, каждый
   фрагмент **5×5 /640×640**, масштаб всех трёх панелей совпал. Полные
   current/A/A-marked/B1 PNG4096×4096. Размеры прочитаны; preview и все пять
   closeup визуально просмотрены. Python evidence tool исполнялся, без .pyc.
8. Scoped diff/whitespace и staged file list проверены перед коммитом.

Промежуточные ошибки не объявляются PASS: запуск shell с ещё не созданным
working directory; первый compile использовал отсутствующий в Qt5.12
iterator constructor QSet (заменён insert-loop); диагностическая команда
Python -c потеряла кавычки в Windows shell (результаты прочитаны через JSON).
Первый failed compile log сохранён отдельно. Повторная генерация PNG после
смены selection closeups нужна для нового framing, renderer не изменялся.

Воспроизведение из корня (с существующими private snapshots):

```text
python tools/terrain_bake_prototype/b1_junctions.py prepare
qmake ../../tests/terrain_recipe_bake_tests.pro  (в build-tests-B1-junctions/test-build)
mingw32-make -f Makefile.Release -j2
build-tests-B1-junctions/test-build/release/terrain_recipe_bake_tests.exe
build-tests-B1-junctions/test-build/release/terrain_recipe_bake_tests.exe --zone build-tests-B1-junctions/zone-fixture.json
build-tests-B1-junctions/test-build/release/terrain_recipe_bake_tests.exe --zone build-tests-B1-junctions/zone-fixture-a.json
python tools/terrain_bake_prototype/b1_junctions.py corpus
python tools/terrain_bake_prototype/b1_junctions.py inspect
```

Для object compile: qmake ../../ei_maper.pro в B1/project-object, затем
`mingw32-make -f Makefile.Release release/terrain_recipe_bake.o`.
Python в этих примерах означает указанный выше полный путь; Qt/MinGW bin
добавлялись только в PATH текущей shell-команды.

Evidence в ignored [build-tests-B1-junctions](../../build-tests-B1-junctions/):
[synthetic.log](../../build-tests-B1-junctions/synthetic.log),
[zone-b1.log](../../build-tests-B1-junctions/zone-b1.log),
[zone-a.log](../../build-tests-B1-junctions/zone-a.log),
[compile-project-object.log](../../build-tests-B1-junctions/compile-project-object.log),
[input-receipt.json](../../build-tests-B1-junctions/input-receipt.json),
[verification.json](../../build-tests-B1-junctions/verification.json),
[corpus-budget.json](../../build-tests-B1-junctions/corpus-budget.json).
Игровые изображения, fixture/banks и binary существуют только в приватных
ignored build directories, не входят в Git/коммит.

## Визуальная оценка и ограничения

[Общий preview](../../build-tests-B1-junctions/preview.png): current / A с
розовыми тройными и голубым четверным / B1. Это оценка исследователя,
не художественная приёмка владельца.

- [Triple (90,138)](../../build-tests-B1-junctions/closeup-1-triple.png):
  центральное схождение спокойнее top-two A, но у current крупная крошка
  читается отдельными компактными кусками. B1 даёт более мягкое смешение,
  часть каменного рисунка вытягивается дугами.
- [Triple (115,164)](../../build-tests-B1-junctions/closeup-2-triple.png):
  жёлтая бахрома сохраняет pair-specific рисунок, однако stock островки
  у current выразительнее; возле центра B1 теряет контраст и детали.
- [Triple (95,161)](../../build-tests-B1-junctions/closeup-3-triple.png):
  рассыпчатая линия current против местами направленных штрихов B1. Новое
  сглаживание центральных весов не исправляет всю деформацию stock strip.
- [Triple (114,141)](../../build-tests-B1-junctions/closeup-4-triple.png):
  компактные родные куски против дугообразного переноса в A/B1. Смена top-two
  устранена, но repeated body texture и фиксированная tangent phase остаются.
- [Quad (111,157)](../../build-tests-B1-junctions/closeup-5-quad.png):
  резкое многопарное схождение A заменено мягкой композицией четырёх материалов.
  В центральной клетке не наблюдается выраженной звёздочки из расходящихся
  тонких полос. Вокруг неё остаётся более мелкая/вытянутая бахрома, чем
  крупная родная жёлтая крошка; центр местами мутнее current.

У выбранных центров специального starburst после сглаживания не наблюдалось;
это не математическое доказательство художественного качества каждого
пикселя произвольной карты. Тонкие дуги в pair кромках вне центральной области
остаются известным ограничением transported brush, C1 не гарантируется.
Сглаживание не меняет рёбра, поэтому не может исправить все унаследованные
штрихи у них. Следующая художественная задача — точный contour transport/
компактные authored junction masks, по отдельному поручению.

Полный production этап B остаётся открыт: profile selection/loader,
стабильные labels при редактировании, Modern wiring, full-map streaming,
DXT/gutters/mips, overlay/animations, save/reopen/Undo и runtime/owner acceptance.
Balanced labels A по-прежнему могут переназначаться при изменении состава
группы; B1 не обещает edit-local stability. Все tiles результата остаются
в RAM. Private-engine/modding skills недоступны; приватный engine-код не
использовался. Игра и интерактивный редактор **NOT RUN**; не PASS/SKIP.

## Scoped-коммит

Только ядро `.h/.cpp`, tests/terrain_recipe_bake_tests.cpp, новый
tools/terrain_bake_prototype/b1_junctions.py, этот отчёт и ROADMAP.md.
Автор/committer задаются command-local env: Codex <codex@localhost>;
trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
Без push и глобальной Git configuration. SHA коммита передаётся в итоге;
ссылка на этот отчёт/ROADMAP входит в тот же scoped-коммит.
