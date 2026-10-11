# EI-MAPPER-HD-MEMORY — 2026-10-11

## Результат и границы

Оптимизация готова к scoped-коммиту сопровождающего. На трёх тяжёлых приватных
копиях Dejavu (Zone9, Zone3obr, Zone6) снижены пик открытия и рабочий набор.
Сравнение времени открытия/покраски/сохранения укладывается в +10% по медианам.
Исходные пиксели, профиль, семантические поля и сохранённые MPR подтверждены.
Это проверка программы на указанных сценариях, не игровая или ручная приёмка.

База `main`: `57b998d3ee1f7b207cc866ca68879687eca97e49`, VERSION138. Снимки 480 source-файлов каждого
варианта сверены с этой базой; candidate включает только 5 собственных production
файлов. Чужие параллельные изменения не включались. HEAD общего checkout на
передаче: `061d7219d6d3292a3c48380c57676e9862faee16`; это другая совокупность изменений, не SHA сборки138+hdmem.

AGENTS SHA256 `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`
проверен при входе и перед передачей, неизменен. Прочитаны текущие договоры,
CONTRIBUTING, workflow параллельных работ и действующий участок ROADMAP.
Последнее поручение запрещает Git-запись, изменение общих документов/версии,
живых данных, постоянной установки, Endurance и интерактивный запуск.
Эти запреты соблюдены. Qt/MinGW читались из установленного toolchain; сеть,
установка зависимостей, подагенты и новые чаты не использовались.
Private-engine skills отсутствуют; приватный engine-код не исследовался.

## Причина и изменения

Главные владельцы роста после HD — native CPU-RGBA, полная временная RGBA-сетка
при загрузке рабочего GL-атласа и перекрывающееся время жизни upload-буферов.
Четыре контейнера authored/picker/resolver/working уже разделяли QImage: считать
их как четыре копии 400MiB было бы неверно. Профиль/анализ сохраняют сигнатуры,
не отдельный полный raster. Reserve 16384 рабочих слотов выглядит как 4GiB
логических изображений, но большинство ссылается на один blank-tile.

- Большие immutable decoded atlases (32MiB..8GiB) на Windows используют
  временный file mapping вместо private heap. ФорматRGBA8888, размеры и байты
  прежние. Const-data QImage detaches даже при единственном владельце; правки
  не записываются в backing-file. HANDLE/section живут до последнего QImage;
  DELETE_ON_CLOSE удаляет файл также при завершении процесса. QObject не
  передаётся между потоками. Ошибка подготовки backing-file даёт прежний heap
  путь; небольшие атласы и другие платформы также сохраняют его.
- Одинаковые BC1-кандидаты с точным равенством QImage разделяют один buffer.
  Bucket-хэш не является доказательством равенства; коллизии проверяются.
  Учитывается обратная строка MMP (`7 - slot/8`), как в существующем codec.
  Разные BC1-кодировки равных пикселей могут остаться отдельными буферами.
  Штатный SHA256 атласа сохранён и вычисляется поточно при декодировании.
- После 64 тайлов освобождается исходная BC1+mips страница. Base desktop GL
  уже использовал BC1: формат/содержимое authored mips не менялись. Объединённые
  mip-буферы создаются только для desktop пути; исходные MMP убраны перед GL
  allocation, каждый combined mip — после синхронного upload.
- Рабочий GL preview загружается по одной composePage вместо composeAtlasGrid.
  Для Zone9 CPU staging 400MiB заменён 16MiB страницей. Padding остаётся нулевым,
  GPU mip generation и sampler прежние; старый preview сохраняется при ошибке.
  Затронутые active texture/binding/unpack alignment восстанавливаются.
- Резидентность только собственных mapped диапазонов освобождается до GL
  allocation и после страниц. Первые две страницы остаются тёплыми в жёстком
  общем лимите 32MiB по native image sizes, включая mixed resolution. Это
  cache residency, не новая копия. Heap/чужие mapped диапазоны не затрагиваются.
- Catalog tile cache получил LRU 32MiB. Source body-image cache получает
  прежний лимит 128MiB при вставке, а не только unload, и максимум 2048 записей,
  включая нулевые miss. Внешние QImage остаются валидными после eviction.
  Поддерживаемые сценой GL textures не выселялись.

Существующие prepared-composite 256MiB и layered-GL 768MiB бюджеты сохранены.
Индексные/skin/name кэши содержат metadata, не RGBA страниц. Унаследованный
raw ANM-cache до refresh и кэши живых фигур/обычных GL ресурсов не получили
новой политики: это отдельные ресурсы с контрактом сценовых ссылок. Их память
на тяжёлых MOB-сценах отдельно не профилировалась. MPR-тест не доказывает
отсутствие всех возможных источников роста памяти во всех режимах.

## Метод и артефакты

- Baseline 138: `build-baseline-hdmem/release/EIMapperEndurance.exe`, SHA256
  `e4135f1e4c34082db9c2a4d9550db0750b445237111d1f7644abf034e310d310`.
- Candidate 138+hdmem: `build-after-hdmem/release/EIMapperEndurance.exe`, SHA256
  `be8d430e0bde7a3cb0d9740b781061f5f406e2856afcb1c3aed029b5c0ca6e26`. Номер следующей версии назначает сопровождающий.
- [Integrity](../../build-tests-hdmem/integrity.json):480/480 source-файлов
  каждого snapshot; совпадение5 owned-файлов;6 контрольных MPR/RES неизменны.
- [Inputs](../../build-tests-hdmem/inputs.json): пути/хэши6 входов. Рабочие
  копии находятся только в build-tests-hdmem; Base maps не открывались.

Счётчики Windows GetProcessMemoryInfo опрашивались каждые 20ms. PeakWorkingSet
берётся до маркера готовности; workingSetReady — последний sample до него,
перед диагностическим чтением всех пикселей. Это рабочий набор при готовности
редактора, не обещание постоянного idle-RSS. PrivateUsage — committed private
memory процесса, включая driver/Qt, не сумма только QImage. GPU размеры дают
логическую оценку, не отдельный замер VRAM. OS может сохранять backing pages
в файловом кэше вне working set: native пиксели остаются доступны по требованию.

Для opening использованы прямые измерения до ready-marker из cold benchmark:
3 прогона каждой карты для baseline и candidate, всего18 открытий. До этого
маркера normal/cold helper имеют побайтно одинаковый код; benchmark flag не
используется production-кодом. Это проверено в
[opening-prefix-proof](../../build-tests-hdmem/opening-prefix-proof.json).
Время/память действительно измерены, не выведены из времени покраски или сборки.
[Opening rows](../../build-tests-hdmem/opening-from-cold.json) ссылаются на
исходные 20ms memory receipts. Начальные MPR совпали с сохранёнными inputs по SHA;
каждый benchmark начинает с новой побайтной копии до любых действий покраски.
Owner/profile census выполнен отдельной парой на каждую карту, всего6 открытий.
Все окна/открытия под mkdir ui-tests.lock с owner.txt, на скрытом Windows desktop.
Чужие locks/processes не останавливались. Каждый GUI-результат содержит
clipboard isolation PASS/nativeAttempts=0; системный буфер не открывался.

Дополнительная CPU-guarded серия закончила2 пары и снова ждала чужой lock.
Её остановили как избыточную после обнаружения полных18 opening receipts в cold
прогонах; частичные данные сохранены в paired-opening-cpu-guarded-partial.json.
Они не объявлены полной серией PASS и не входят в медианы. Для первого cold
прогона ожидание lock составило примерно49 минут и не превысило60 минут.

Ранее отдельная серия попала под внешние CPU 100%/shader_embed около7–9 ядер:
Zone9 до 85s, Zone3obr до 43s. [Raw after](../../build-tests-hdmem/profile-after.json)
и [CPU evidence](../../build-tests-hdmem/host-cpu-pressure.json) сохранены,
но не смешаны со стабильными парами и не объявлены PASS по времени.

Cold benchmark начинает действительную покраску сразу после готовности,
без прогрева native пикселей owner-инспектором. На вариант 9 прогонов:180 измеренных forward и 180
undo действий CView.applyTerrainTilePreview, 9 сохранений
дополнительных копий и 27 полных GL uploads. MPR перечитаны после сохранения.
[Baseline](../../build-tests-hdmem/benchmark-cold-baseline.json),
[candidate](../../build-tests-hdmem/benchmark-cold-after.json),
[summary](../../build-tests-hdmem/summary.json). Таблицы — медианы 3 прогонов;
для paint/upload сначала медиана действий каждого прогона.

## Память: до → после

| Карта | Peak WS, MiB | WS при готовности, MiB | Peak private, MiB |
|---|---|---|---|
| zone9 | 1654.9 → 1275.6 (-22.9%) | 1342.4 → 772.6 (-42.4%) | 2902.5 → 2053.4 (-29.3%) |
| zone3obr | 1561.5 → 1196.5 (-23.4%) | 1232.2 → 789.3 (-35.9%) | 2785.2 → 2060.5 (-26.0%) |
| zone6 | 1300.8 → 1007.1 (-22.6%) | 992.8 → 682.4 (-31.3%) | 2392.2 → 1821.3 (-23.9%) |

| Карта | Workload peak WS, MiB (paint/save/3 uploads) |
|---|---|
| zone9 | 2063.7 → 1275.6 (-38.2%) |
| zone3obr | 2012.7 → 1196.5 (-40.6%) |
| zone6 | 1760.9 → 1007.1 (-42.8%) |

Исторические 1012→1700MiB из138 не являются парным baseline этого замера.
Новый пик Zone9 сопоставим с до-HD уровнем плюс разумная GPU надбавка:
полный RGBA preview 10240² с mip chain логически≈533MiB, до-HD 5120²≈133MiB.
Это оценка дополнительного объёма по размеру, не измерение VRAM или точная
атрибуция каждой страницы Windows working set.

## Владельцы decoded buffers

| Карта | Слотов | До: MiB / buffers | После: MiB / buffers | Mapped MiB после | Resolver materials, MiB |
|---|---|---|---|---|---|
| zone9 | 1600 | 400.00 / 1600 | 387.25 / 1549 | 387.25 | 5.25 |
| zone3obr | 1472 | 368.00 / 1472 | 327.75 / 1311 | 327.75 | 4.25 |
| zone6 | 1216 | 304.00 / 1216 | 278.00 / 1112 | 278.00 | 3.50 |

У viewAuthored, workingAuthored, resolverAuthored дополнительные native
buffer bytes равны 0: они разделяют picker backing. WorkingSlots добавляет
один blank buffer 0.25MiB, несмотря на 16384 logical entries. Prevalidated после
adoption пуст; globalMaterialSources, resourceCpuImages, catalogTiles в этих
MPR-only открытиях пусты. Heap material sources необходимы генерации и сохранены.
Это разбивка именованных image buffers, не полный heap/driver allocation census.

Дополнительный [baseline census](../../build-tests-hdmem/profile-census-baseline.json)
и [candidate census](../../build-tests-hdmem/profile-census-after.json) учитывают
миниатюры land/underwater/overlay, uniform/transition/hydration caches и
prepared family-stage. Во всех трёх парах дополнительных QImage bytes у этих
владельцев0: thumbnails разделяют уже учтённые native buffers, остальные группы
пусты при данном открытии. Bound-atlas entries содержат metadata, не decoded pages.

У picker созданы32 QIcon на каждой карте, до/после одинаково. Сумма native
availableSizes×4 равна8MiB; это оценка пикселей иконок, не точный объём Qt/platform
storage. QIcon создаётся лениво в source/ui/tile_form.cpp:150–155 и сохраняется
до сброса m_icoList. По мере просмотра всех1600 слотов Zone9 теоретический
native raster объём может достичь400MiB; это статическая верхняя оценка,
полный пользовательский скролл не измерен. Данный файл не входит во владение.
Предлагаемая отдельная правка: бюджет/выброс невидимых QIcon с повторным
созданием из m_tileImages при необходимости. Начальные32 иконки не являются
причиной найденного полного400MiB staging; текущая картинка не менялась.

По стадиям доступны 20ms samples и события resolve/upload/base-atlas-ready/
working-preview/editor-ready в `bench-cold-*-memory.json` и workflow.log
соответствующих папок. Передача decoded → GPU не дублирует весь CPU grid;
после upload освобождается резидентность cold страниц, а данные сохраняются.

## Время, ms: до → после

| Карта | Открытие | Покраска | Первый мазок | Сохранение |
|---|---|---|---|---|
| zone9 | 11937.0 → 10629.0 (-11.0%) | 9.7 → 8.8 (-8.8%) | 10.0 → 9.4 (-5.8%) | 5056.7 → 5236.7 (+3.6%) |
| zone3obr | 10804.0 → 10090.0 (-6.6%) | 9.9 → 8.1 (-18.3%) | 9.9 → 8.4 (-15.1%) | 4500.8 → 4537.7 (+0.8%) |
| zone6 | 9004.0 → 8804.0 (-2.2%) | 8.5 → 8.1 (-4.0%) | 9.3 → 9.2 (-1.6%) | 3760.2 → 3803.7 (+1.2%) |

Все четыре критерия ≤+10% по медианам. Это указанные CView сценарии и аппаратный
desktop GL, не измерение мыши-to-present или всех видов покраски.

## Сохранность, UI и регрессии

- 3 пары native decoded atlas SHA, профилей/семантики/палитр и GL format/dimensions/
  mips: 17 полей сравнения на пару совпали. Пиксели native страниц без изменений.
- 9 пар opening metadata:12 полей совпали (raw MPR/profile/atlas SHA,
  land/overlay fields, palette counts, native/render sizes, slots/families/sectors).
- 9 пар сохранённых MPR побайтно идентичны; все 18 файлов успешно прочитаны.
- 8 CLI checks: UI-widget, scenario, dark, gallery, terrain-height-save,
  terrain-mob-height, real random, mixed-edge. Exit0, FAIL/SKIP не засчитывались.
- QtTest: terrain 146, resolve 5, mixed-resolution 4, hd-memory 7; всего 162 PASS,
  0 FAIL, 0 SKIP. Memory tests проверяют asymmetric duplicates, mapped storage,
  sole-owner detach/lifetime, cache bounds и GL image equivalence.
- Recipe 17425 PASS,0 FAIL, 0 SKIP; 18241 edges, 2240132 RGBA channel comparisons.
- GL compare: 640 slots, mixed 16/32px, 10 pages/4x3 grid, 2 padding pages,
  прозрачность и все 11 mip уровней. NVIDIA RTX 4070SUPER/desktop GL 4.6,
  driver 617.14; это реальный GL, не software substitute.
- [Reference PNG](../../build-tests-hdmem/visuals/reference-gl-atlas.png) и
  [page upload PNG](../../build-tests-hdmem/visuals/page-upload-gl-atlas.png):
  1024x768, побайтно одинаковы; SHA256
  `eb7eccff656c44d4af89239568b104a318554733ffe5a6d24793d9d3cafd965b`.
  Изображение просмотрено. Это синтетический GL atlas, не карта/игровой кадр.
- [Full receipts](../../build-tests-hdmem/regression.json).
- Экспортируемый измеритель прошёл --benchmark на3 картах и opening/census
  на3 baseline +3 candidate картах; все exit0, clipboard isolation PASS.
  Три дополнительных сохранения совпали с baseline SHA.
  [Tool smoke](../../build-tests-hdmem/benchmark-checked-tool-smoke.json).

Подготовочные ошибки исправлены и не объявлялись PASS: недостающая fixture
env/папка, QTemporaryFile retaining native handle (fallback вместо mapping),
квантование цветов в тестовом RGB565 fixture. Тест требующий MEM_MAPPED сохранён.
RGB888 отклонён из-за изменённого QImage equality; двойной SHA обход заменён
BC1 bucket+exact comparison. Первый холодный мазок/сохранение выявили превышение
10% в промежуточном варианте; добавлены ограниченные hot pages и быстрый bucket.
Wrong raw-row bucket пропускал дубли; исправлен и покрыт asymmetric test.
Прежние stage1..stage4 данные сохранены отдельно, не смешаны с финальным EXE.

## Что потребовало бы view.cpp

view.cpp/main.cpp не менялись. Для настоящего compressed-only/lazy CPU atlas
нужно заменить обязательный QVector<QImage> boundary при adoption/picker/
working/resolver на общий immutable BC1 page provider с ленивыми tile views;
сохранить native pixel-SHA/равенство и COW редактирования. Для уменьшения
дополнительного GPU preview нужен отдельный выбор момента primeWorkingTerrainAtlas
с сохранением отображения/mip поведения. Эти изменения описаны, не реализованы
и не заявляются необходимыми для текущего достигнутого снижения.

## Передача

Точный список 11 файлов для scoped-коммита (build/data/logs/PNG не коммитить):

- `source/render/landscape.cpp`
- `source/render/terrain_atlas_archive_binding.cpp`
- `source/render/terrain_atlas_archive_binding.h`
- `source/resources/resourcemanager.cpp`
- `source/resources/resourcemanager.h`
- `tests/terrain_hd_memory_tests.cpp`
- `tests/terrain_hd_memory_tests.pro`
- `tests/terrain_hd_memory_probe.cpp`
- `tests/terrain_hd_memory_probe.pro`
- `tests/terrain_hd_memory_measure.py`
- `docs/validation/ATAMAN_MAPPER_HD_MEMORY_20261011.md`

Коммит делает сопровождающий. Политика единственной main сохранена; Git только
чтение. ROADMAP/CHANGELOG/VERSION/Readme/translations и постоянная установка
не менялись этим заданием. Итоговая совместная сборка нового main после интеграции
параллельных задач отдельно не проверена и не подменяется этой 138+hdmem сборкой.

### Текст ROADMAP

> EI-MAPPER-HD-MEMORY — готово к scoped-интеграции. Причина роста после HD:
> native RGBA и полная CPU preview-сетка/время жизни upload буферов. Результат:
> page upload, immutable Windows mapped/COW storage, BC1 dedup buckets,
> hot pages≤32MiB, catalog LRU 32MiB/source128MiB/2048 keys.3 карты×3 пары:
> пик/рабочий набор снижены, время открытия/покраски/первого мазка/сохранения≤+10%
> по медианам; 3 exact profile/decoded pairs,9 opening metadata pairs
> и9 byte-identical saved MPR pairs;
> full UI/terrain suite без FAIL/SKIP. Evidence:
> docs/validation/ATAMAN_MAPPER_HD_MEMORY_20261011.md. Следующий шаг:
> сопровождающий назначает версию и делает scoped-коммит/совместную сборку;
> установка и игровая/ручная приёмка этим поручением не разрешены.

### Текст CHANGELOG (следующий согласованный номер)

> EI-MAPPER-HD-MEMORY: снижена память HD-карт без изменения native пикселей,
> профилей, mip данных и сохранённого MPR. GL preview загружается страницами;
> большие immutable RGBA используют Windows temporary mapping и COW;
> равные BC1/decoded tiles разделяют storage, cold residency/upload buffers
> освобождаются, hot pages ограничены32MiB. Catalog cache LRU 32MiB,
> source image cache128MiB/2048 entries. Проверки:3 Dejavu карты×3 пары,
> время≤+10% по медианам,9 byte-identical saves,162 QtTest PASS,
> recipe 17425 PASS, full 8 CLI checks; FAIL 0/SKIP 0, 11 GL mip уровней идентичны.

Ограничения: temporary backing требует свободного места≈400/368/304MiB
на native atlas (дедуп уменьшает записанные байты, размер резерва прежний);
при дополнительных одновременно живых atlas увеличивается disk footprint.
На нехватке места/ошибке mapping — heap fallback, эффект памяти не гарантирован.
На Windows-платформе проверены пиксели/COW/cleanup; ANGLE, другие GPU/ОС,
MOB-heavy сцены, manual desktop/game acceptance отдельно не подтверждены.
