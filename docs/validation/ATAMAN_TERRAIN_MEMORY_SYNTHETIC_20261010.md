# EI-TERRAIN-MEMORY-SYNTHETIC — бюджет памяти атласа

Дата: 2026-10-10. Поручение владельца через Claude: заменить ручную игровую
проверку памяти синтетической, без запуска игры. Исходная база:
`main`, VERSION **131**, HEAD `6fbcd4e918a84d90460e0675b9777df08327c5d7`.
При продолжении по новому рендеру сопровождающий принял параллельный этап 1B;
конечный HEAD `99bc5b720dadf3f34c548fd2d23e5334c245c6d0`, main, VERSION 131.
Этот коммит меняет документацию и stage1b.py; программа, VERSION и AGENTS.md
не изменились. Его изменения сохранены, Git этой задачей использован только для чтения.
AGENTS.md проверен при входе и перед передачей; SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`,
совпадает с этапом 1. Учтены CONTRIBUTING, PROJECT_CONTRACT,
PARALLEL_THREAD_WORKERS, TEST_BUILD_HANDOFF и активный пункт этапа 1 в ROADMAP.

## Результат

Все четыре подготовленных комплекта структурно корректны по загрузчикам
Ataman. В первом этапе выполнены **104 независимых запуска x86-процесса: 90 PASS, 14
ALLOC_FAIL, 0 ERROR, 0 TIMEOUT**, сохранены **11 828 снимков VA**.
ALLOC_FAIL — ожидаемый результат стресс-пробы, а не успешная загрузка.
Во всех снимках обход VA завершён, occupied + free равны пределу обхода.
Продолжение «Новый рендер»: **24/24 PASS** в профилях D3D11 wrapper и две
связанные регрессионные пробы (1 PASS / 1 ALLOC_FAIL). Общий итог — **130 запусков,
115 PASS / 15 ALLOC_FAIL / 0 ERROR / 0 TIMEOUT, 15 901 снимок VA**.

Рекомендуемый бюджет: **128px, 2048 слотов по умолчанию; 4096 слотов как
верхний предел планирования**. 4096×128 выдержали даже фон 1536 МиБ private
и 512 МиБ reserve с двумя RGBA+mip копиями и единым RGBA-буфером.
Оставалось 1004,1 МиБ свободного VA, крупнейший блок 890,9 МиБ.
8192×128 при той же нагрузке не смогли выделить единый буфер 528 МиБ.
Для исходного профиля с постоянными RGBA-копиями при 256px **1024 слота — условная верхняя рекомендация**, 2048 уже не дают
устойчивого запаса, 4096/8192 не рекомендуются. У 1024×256 в тяжёлом сценарии
свободно 1004,5 МиБ, крупнейший блок 549,9 МиБ: запас меньше, чем у 4096×128.
Это ограничение модели RGBA/assembly, а не универсальный предел нового рендера:
сжатый профиль прошёл вплоть до 8192×256. Условия и итоговая рекомендация —
в разделе «Новый рендер» ниже.

Высока уверенность в структуре комплектов, PE-фактах и измерениях этого
синтетического процесса. Умеренная уверенность в консервативном бюджете 128px;
перенос точного порога отказа на DejaVu имеет низкую уверенность. Это не
игровая приёмка и не гарантия для произвольного renderer/карты/сохранения.
Ручной игровой тест по последнему поручению не является оставшимся этапом
этой задачи; прежние художественные и прочие production-цели не закрываются.

## Границы и сохранность

Запись только в новые `memory_probe32.*`, `memory_validate.*`, собственный
`build-tests-terrain-memory-synthetic/` и этот отчёт. README прототипа,
программа, VERSION, CHANGELOG, Readme, ROADMAP и translations этой задачей не изменялись.
Git использован только для чтения; коммит выполняет сопровождающий. Чужой
`tools/terrain_bake_prototype/stage1b.py` и правки README прототипа, появившиеся
параллельно, сохранены и не входят в список коммита этой задачи.

Сети, установки зависимостей, подагентов, игры и интерактивного редактора не
было. Ни один инструмент не открывает окна, GL-контекст или clipboard:
валидатор использует QCoreApplication/QImage, probe — Win32 console.
Поэтому UI-lock не требовался. Живые файлы, карты, исходный Endurance и
постоянная установка маппера не были целями записи. Прочитан существующий
toolchain Endurance, но сборки выполнялись в собственном build-tests.
Приватные engine skills недоступны; приватные исходники движка не читались,
неизвестные внутренние алгоритмы обозначены гипотезами.

Повторное чтение подтвердило исходные hashes: 12 MPR/RES/MOB файлов четырёх
комплектов, 2 baseline-файла, 12 установленных PE-файлов, живые source MPR/MOB.
[Receipt и fingerprints](../../build-tests-terrain-memory-synthetic/receipt.json).
Игровые payloads остаются в private build-tests; в Git идут только алгоритмы
инструментов и этот отчёт. Это исследовательские executables, не выдаваемая
сборка маппера: package finalizer и обновление установки не применялись.

## 1. PE и доступное адресное пространство

Путь получен из актуального `gameRoot` профиля
`artifacts/EI-Mapper-Ataman-2-first-test/endurance-options.json`:
`C:/GOG Games/PZ`. Все EXE/DLL непосредственно в Engine прочитаны как байты;
DLL не загружались. [Полные импорты и hashes](../../build-tests-terrain-memory-synthetic/pe-facts.json).

| Компонент | PE / разрядность | Characteristics / LAA | Графические импорты |
|---|---|---|---|
|Engine/game.exe|PE32, IMAGE_FILE_MACHINE_I386 (0x014c), 32 bit|0x012f, бит 0x20 включён|DDRAW.dll|
|Engine/EIRebuild.exe|PE32, I386, 32 bit|0x0122, LAA включён|D3DCOMPILER_47.dll|
|Engine/ddraw.dll|PE32, I386, 32 bit|0x2102, LAA выключен|d3d11.dll, D3DCOMPILER_47.dll|
|Engine/RDDraw.dll|PE32, I386, 32 bit|0x2102, LAA выключен|d3d11.dll, D3DCOMPILER_47.dll|

SHA256 game.exe:
`be0fc330035e03983cce590038cc7f881f1f5d1bcf082717f31776dd4dee94e2`.
SHA256 EIRebuild.exe:
`4d98790a990b46b3f50ee66445750f65acc40dc76c97bb6245939f9c5f9e11fe`.
SHA256 ddraw.dll:
`271df834174701f6743199b77934a243b4ff03008cd4ccff816eb504501b83d1`.

Импорт game.exe устанавливает DirectDraw ABI; точный Direct3D interface/version
из одного имени DDRAW не следует. Импорты локального wrapper указывают на
D3D11 backend. Наличие рядом D3D8.dll/D3D9.dll само по себе не означает их
использование. Фактический выбор EXE, DLL search order, динамические LoadLibrary
и residency без процесса не измерены: цепочка game → локальный ddraw → D3D11
остаётся выводом из установки/импортов, а не подтверждением live renderer.

На 64-bit Windows под WoW64 обычный 32-bit EXE без LAA имеет 2 ГиБ user VA;
LAA даёт до 4 ГиБ. LAA в главном EXE определяет доступный диапазон, отсутствие
этого бита в DLL само по себе не уменьшает процесс до 2 ГиБ. Это VA, а не
гарантированные 4 ГиБ RAM, свободного commit или непрерывного блока. Образы,
стек, heaps, mappings, драйверные ресурсы и фрагментация уменьшают запас.
У probe проверены PE32/I386 и LAA как у game.exe; фактический предел
GetSystemInfo/VirtualQuery — **4 294 901 760 байт** (4 ГиБ минус 64 КиБ),
полностью просканирован во всех 11 828 снимках.

## 2. Структура четырёх комплектов

Отдельная утилита линкует **текущие исходники Ataman**:
CMprDocument, CTerrainEditModel, CResArchiveDocument, inspectMmp,
decodeDxtImage и decodeMmpDxt1AtlasTile. Она не вызывает renderer.
Сверены hashes с manifest; MPR и RES открыты; каждая страница разрешена по
имени production RES-loader, каждый mip действительно декодирован, каждый
из 64 слотов страницы отдельно прочитан tile decoder. Проверены отсутствие
trailing/missing mip bytes и полная цепочка до 1×1.

| Комплект | Страницы / декодированные слоты | Декодированные mip | SEC | Land refs / active overlay refs | Уникальные land / все slots со ссылками | DXT+mip МиБ |
|---|---:|---:|---:|---:|---:|---:|
|2048×128|32 / 2048|352 (11/page)|96|24576 / 1558|1917 / 1941|21,334|
|4096×128|64 / 4096|704 (11/page)|96|24576 / 1558|3965 / 3989|42,667|
|2048×256|32 / 2048|384 (12/page)|96|24576 / 1558|1917 / 1941|85,334|
|4096×256|64 / 4096|768 (12/page)|96|24576 / 1558|3965 / 3989|170,667|

Для каждого комплекта все страницы имеют SEC-ссылки. Проверены все 24576
land-клеток: slot в диапазоне, predecessor соответствует manifest, class и
два бита поворота сохранены. Проверены overlay packed values/material indices
в каждой клетке; 1558 активных overlay-ссылок валидны. **Class mismatch=0,
rotation mismatch=0, overlay mismatch=0** во всех четырёх комплектах.
Секторные байты вне изменяемого land-массива совпадают; MP materials и animation
records совпадают; unknown archive members и MOB сохранены.

Stock MP имеет 19 физических страниц (1216 мест), но 1193 записи class table:
23 padding-места исходного хвоста без class трактуются как 0 согласно packer и
production tileType. Это отличается от 107 физических мест без ссылок в новых
комплектах. Первая версия валидатора ошибочно индексировала class table по всем
физическим местам; она завершилась отказом. Исправлен **новый валидатор**, данные
и программа не менялись. Итоговая полная проверка PASS.

Всего: 192 страницы, 12288 слотов, 2208 mip-уровней. Подробные per-page hashes:
[structural.json](../../build-tests-terrain-memory-synthetic/structural.json),
[stdout](../../build-tests-terrain-memory-synthetic/structural.log).
Это проверка структуры/сохранности и читаемости декодером, не художественная
оценка новых mip, звуков, проходимости или shader/overlay в игре.

## 3. Модель памяти и калибровка

Единица ниже — **МиБ = 2^20 байт**. Пусть N — slots, p — tile pixels,
P=N/64, side=8p. D — сумма DXT1 всех mip всех страниц, R — одна RGBA8-копия
всех mip. Для каждого mip DXT1: `8*ceil(width/4)*ceil(height/4)` с минимумом
один блок; RGBA: `4*width*height`. Цепочка заканчивается в 1×1.
C — единый **base-level** RGBA-буфер со страницами в near-square grid:
columns=ceil(sqrt(P)), rows=ceil(P/columns). Это layout исторического
`tests/modern_atlas_real_map_builder.cpp`, не установленный факт о движке.
76-byte MMP headers и RES directory малы, но присутствуют в реальной пробе.

| Slots×px | D | R | C | 2D+R | 2D+2R+C |
|---|---:|---:|---:|---:|---:|
|1024×128|10,667|85,333|64|106,667|256|
|2048×128|21,334|170,667|144|213,334|528|
|4096×128|42,667|341,333|256|426,668|1024|
|8192×128|85,335|682,667|528|853,336|2064|
|1024×256|42,667|341,333|256|426,667|1024|
|2048×256|85,334|682,667|576|853,334|2112|
|4096×256|170,667|1365,333|1024|1706,668|4096|
|8192×256|341,335|2730,667|2112|3413,336|8256|

Варианты и существенные ограничения модели:

1. **Только compressed GPU:** GPU cost D, process VA — неизвестный driver
   bookkeeping/mappings. VRAM не обязательно занимает равное количество VA
   процесса. Probe `dxt` консервативно представляет одну D-копию обычным
   process allocation; это не измерение драйвера.
2. **GPU + system copy:** тестируется 2D в process VA (`system`). Реальный VA
   может быть меньше/больше в зависимости от драйвера и lifetime resources.
3. **Streaming decode:** 2D постоянно, плюс D(page)+R(page) временно (`stream`).
   Размер R(page) 5,333 МиБ при 128px и 21,333 МиБ при 256px.
4. **Retained RGBA:** 2D+R, плюс входная compressed page при загрузке (`rgba`).
5. **Amplified:** 2D+2R постоянно, плюс D(page)+R(page) временно (`amplified`).
6. **Assembled:** вариант 5, затем единый C с реальным копированием base page
   rows (`assembled`). Peak модели — max(2D+2R+D(page)+R(page), 2D+2R+C).

Если движок держит целый RES в RAM, прибавить приблизительно D ещё раз;
probe читает directory и страницы потоково и этого предположения не тестирует.
Если создаются ещё полные surfaces или GPU staging, прибавить соответствующий
объём/непрерывный request. Такое поведение без приватного кода или live процесса
не установлено. Конечный VA включает фон, резервирование и allocator rounding.
[Машиночитаемая модель](../../build-tests-terrain-memory-synthetic/memory-model.json).

### Исторические измерения

Источник: [EXPANDED_TERRAIN_ATLAS_RUNTIME.md](EXPANDED_TERRAIN_ATLAS_RUNTIME.md),
**исправленная** HD-кампания 2026-07-29. Старые результаты с дефектным
GameplayReady predicate не использованы как PASS. Raw r23 evidence в этом
checkout и проверенном исходном Endurance недоступно: числа взяты из отчёта,
не повторно измерены. Это JMV Zone18 и прежняя установка; совпадение fingerprints
с текущими DejaVu/Engine и renderer не установлено.

| Slots | Результат прежней игры | Peak private МиБ | Peak committed VA МиБ | Min largest free МиБ |
|---:|---|---:|---:|---:|
|1024|PASS|887,953|1731,340|2044,770|
|2048|PASS|1166,910|1932,465|1855,000|
|4096|PASS|1181,430|1946,961|1831,062|
|8192|CRT crash during load|2441,242|3242,023|319,820|

Private growth 1024→2048 = **278,957 МиБ**, 2048→4096 = **14,520 МиБ**,
4096→8192 = **1259,812 МиБ**; последний committed рост **1295,062 МиБ**.
Простая линейная цена одного слота с постоянным числом копий не описывает эти
точки. Даже один R8192=682,667 МиБ и D8192=85,335 МиБ не объясняют рост
1259,812 МиБ сверх последнего PASS. Возможны разные фазы load/cache, retained
copies, assembly и другие ресурсы; причина crash по этим числам остаётся гипотезой.

**Диагностическая калибровка, не восстановление алгоритма:** из passing 4096
и модели 2D+R получается предполагаемый фон private
`1181,430−426,668 = 754,762 МиБ`. Поэтому 768 МиБ — разумная исходная гипотеза,
1024 — запас; 1536 — отдельно завышенный stress, не оценка реального baseline.
При B=768 модель 4096 даёт 1194,668 МиБ private, на 13,238 МиБ (1,1%) выше
исторического peak. Для 8192 переход к 2D+2R даёт 2304,003 МиБ; недостаёт
137,239 МиБ (5,6%). Гипотеза полного compressed RES (+85,335 МиБ) оставляет
51,904 МиБ дополнительных temporaries/других ресурсов. Это согласование порядка
величин; выбор и lifetime этих копий не доказаны.

Разность historical committed VA и private составляет 765,531 МиБ у 4096
и 800,781 МиБ у 8192. Она не равна MEM_RESERVE: в реальной игре сюда входят
image/mapped/driver/shared allocations. Probe использует **512 МиБ VA-only
reserve** как явный отдельный сценарий и не заявляет совпадение этих типов.
Нельзя приравнивать synthetic occupied (commit+reserve) к historical committed.
Это также причина показывать private, committed, reserved и largest раздельно.

В стресс-профиле B=1536, reserve=512, assembled удалось воспроизвести
**качественную границу** 4096 PASS / 8192 allocation failure. Числа не совпадают
с игрой, и подобранная тяжёлая нагрузка не доказывает историческую причину.
Возможны и другие ограничения renderer/размеров/движка. Без C даже 8192×128
проходят в исходных профилях; поэтому объявлять 8192 безопасными по DXT-size
или объявлять любой 8192 crash доказанным исчерпанием VA нельзя.

## 4. Реальные x86-пробы

Существующий Endurance Qt/MinGW — только x64. Найден существующий MSVC
Hostx64/x86 14.44.35207 и Windows SDK; helper собран /O2 /MT,
/SUBSYSTEM:CONSOLE /LARGEADDRESSAWARE. EXE — PE32/I386, LAA=true как game.exe.
Новые зависимости не устанавливались. Первый запуск VsDevCmd не нашёл vswhere
в PATH; build helper добавляет существующий Installer directory только в
дочернем cmd. Ошибка окружения исправлена, финальная сборка успешна.

Процесс действительно читает RES hash-table/member offsets и payloads;
проверяет bounds/MMP signature/format/dimensions/mip count; выделяет и трогает
каждую committed 4-КиБ страницу. DXT1 декодирует все уровни до 1×1; checksum
подтверждает чтение expanded data. System/copy буферы реально копируются,
единый C заполняется base rows. Нет вызовов game/D3D/GL.

Фон private: chunks до 24 МиБ, между ними временные reserve holes 8 МиБ и
сохранённые separators 64 КиБ. Дополнительный VA reserve — 512 МиБ chunks
по 16 МиБ. После создания фона holes освобождаются, создавая фрагментацию.
Это один воспроизводимый шаблон, не фрагментация измеренной игры. ASLR и
Windows дают небольшие различия между свежими процессами.

GetProcessMemoryInfo и полный VirtualQuery вызываются на baseline, фоне,
пике каждой страницы, после освобождения её temporaries, assembly и при отказе.
Измеряются private/working set, committed/reserved/occupied/free VA, image/
mapped/private VA, число свободных регионов, largest free, request и Win32 error.
Это максимумы **сохранённых снимков в контрольных точках**; максимумы внутри
других функций/между снимками не обещаются. Шаги аллокации существенных atlas
буферов охвачены; baseline construction до освобождения holes отдельно виден.

2048/4096 используют точные подготовленные RES. **1024 — первые 16 страниц
2048-kit, без заявления о валидном 1024 MPR. 8192 — каждый page payload
4096-kit прочитан и выделен дважды** в независимые buffers, без утверждения,
что подготовлен runtime-compatible 8192 MPR. Все данные остаются private.

### Основная матрица: private фон 1024 МиБ + reserve 512 МиБ

В ячейке: **atlas-phase peak occupied VA / minimum largest block, МиБ**.
Это phase peaks, отдельно от временного background-with-holes peak.

| Slots×px | dxt | system | stream | rgba | amplified | assembled |
|---|---:|---:|---:|---:|---:|---:|
|1024×128|1565,4 /2035,3|1576,0 /2039,8|1581,4 /2037,5|1661,4 /2038,2|1752,1 /2042,5|1810,1 /2045,8|
|2048×128|1576,0 /2035,9|1597,4 /2035,5|1602,8 /2033,4|1768,2 /2031,1|1944,3 /2044,0|2082,2 /1896,6|
|4096×128|1597,4 /2037,9|1640,2 /2034,1|1645,5 /2035,0|1981,7 /2042,0|2328,5 /1709,7|2578,5 /1462,6|
|8192×128|1640,2 /2044,8|1725,7 /2035,0|1731,0 /2040,0|2408,7 /1636,5|3097,0 /947,7|3619,0 /423,6|
|1024×256|1599,4 /2034,9|1642,0 /2030,8|1663,4 /2030,8|1983,4 /1801,9|2346,1 /1447,2|2578,1 /1205,2|
|2048×256|1642,0 /2030,9|1727,4 /2039,6|1748,8 /2042,4|2410,2 /1446,0|3114,3 /763,4|3666,2 /204,4|
|4096×256|1727,4 /2044,5|1898,2 /2043,0|1919,5 /2032,4|3263,7 /693,3|FAIL 3957,1 /7,8|FAIL 3935,7 /13,4|
|8192×256|1898,2 /2032,8|2239,7 /1728,5|2261,0 /1684,5|FAIL 3935,8 /20,9|FAIL 3935,7 /13,8|FAIL 3957,1 /4,6|

Число строк — 8, modes — 6. Дополнительно тот же набор проверен при B=768.
Исходная матрица 5 modes × 8 случаев × 2 backgrounds: **74 PASS /6 FAIL**.
Добавленная assembly: **12 PASS /4 FAIL**. Все не помеченные FAIL ячейки PASS.
При B=768 даже 2048×256 assembled имеет лишь 686,4 МиБ суммарно свободного VA
и 567,0 МиБ largest; при B=1024 — 429,7 и 204,4 соответственно. PASS здесь
не означает хороший эксплуатационный запас.

### Тяжёлый сценарий: private фон 1536 МиБ + reserve 512 МиБ, assembled

| Slots×px | Status | Полностью загружено страниц перед итогом/отказом | Peak occupied МиБ | Min largest МиБ | Свободно при итоге/отказе МиБ | Запрос отказа |
|---|---|---:|---:|---:|---:|---|
|1024×128|PASS|16|2324,1|1415,0|1771,8|—|
|2048×128|PASS|32|2595,6|1345,1|1500,4|—|
|4096×128|PASS|64|3091,8|890,9|1004,1|—|
|8192×128|ALLOC_FAIL|128|3610,3|420,5|491,6|528 МиБ combined RGBA|
|1024×256|PASS|16|3091,4|549,9|1004,5|—|
|2048×256|ALLOC_FAIL|32|3627,6|128,5|492,4|576 МиБ combined RGBA|
|4096×256|ALLOC_FAIL|34|3750,3|17,4|345,7|21,333 МиБ temporary RGBA|
|8192×256|ALLOC_FAIL|34|3750,3|17,7|345,7|21,333 МиБ temporary RGBA|

Последние 8 запусков: **4 PASS /4 FAIL**. Все 14 отказов — VirtualAlloc,
Win32 error **8 (ERROR_NOT_ENOUGH_MEMORY)**, контролируемый exit 10;
это не native crash helper. В случае 4096×256 свободно ещё 345,7 МиБ, но
largest всего 17,4 МиБ меньше request 21,333: отдельно подтверждена роль
фрагментации, а не только суммы памяти. Отказ 8192×128 возникает после
чтения всех 128 страниц при запросе C. Даже общий free 491,6 меньше request
528, а largest ещё меньше. При основном B=1024 он проходит, но largest
остаётся лишь 423,6 МиБ, близко по порядку к прежним игровым 319,8 МиБ.

[Все результаты, включая продолжение](../../build-tests-terrain-memory-synthetic/all-probes.json),
[matrix](../../build-tests-terrain-memory-synthetic/matrix/summary.json),
[assembled](../../build-tests-terrain-memory-synthetic/assembled/summary.json),
[pressure](../../build-tests-terrain-memory-synthetic/pressure/summary.json).
Каждый каталог содержит индивидуальные JSONL/stdout и stderr. Начальная
5-mode версия probe SHA256
`7989d9f26a4cd8f5c86e4d5ce2816566c66b5dea3b672402fb4007be59374f0a`;
добавляющая assembly версия
`ad617e9ca2f27d6bb8d98c01a53c1f0ab9cf9f0bc883c2cc98214cf698ec5049`.
Начальный executable заменён при добавлении mode; его hash/результаты
сохранены в matrix summary. Финальный executable и source snapshot сохранены
в pressure. Рабочие executables/снимки/JSON не коммитить.

## Инструменты и воспроизведение

`memory_validate.cpp/.pro` — структурная проверка, production loaders,
baseline conservation, hashes и per-page evidence. `memory_validate.py` —
standard-library PE parser (`facts`), аналитическая модель (`model`), запуск
процессов (`probes`) и контроль полного VA accounting (`summarize`).
`memory_probe32.cpp/.ps1` — x86 console probe и build helper. Не включены
в ei_maper.pro. README прототипа принадлежит параллельной задаче и не изменён.

Команды из корня Ataman, PowerShell (все выходы ниже собственного build-tests):

```powershell
$terrainPython = 'C:/Users/samol/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
$terrainQt = 'C:/GOG Games/maped/ei-mapper-endurance/.tooling/Qt/5.12.12/mingw73_64/bin'
$terrainMinGw = 'C:/GOG Games/maped/ei-mapper-endurance/.tooling/Qt/Tools/mingw730_64/bin'
$env:PATH = "$terrainQt;$terrainMinGw;" + $env:PATH
& $terrainPython tools/terrain_bake_prototype/memory_validate.py facts
& $terrainPython tools/terrain_bake_prototype/memory_validate.py model
New-Item -ItemType Directory build-tests-terrain-memory-synthetic/validator-build -Force
Push-Location build-tests-terrain-memory-synthetic/validator-build
& "$terrainQt/qmake.exe" ../../tools/terrain_bake_prototype/memory_validate.pro CONFIG+=release
& "$terrainMinGw/mingw32-make.exe" -f Makefile.Release -j4
Pop-Location
& ./build-tests-terrain-memory-synthetic/validator-build/release/terrain_memory_validate.exe `
  ./build-tests-terrain-bake-stage-1/memory-kit ./build-tests-terrain-memory-synthetic/structural.json
& ./tools/terrain_bake_prototype/memory_probe32.ps1
& $terrainPython tools/terrain_bake_prototype/memory_validate.py probes `
  --probe build-tests-terrain-memory-synthetic/probe-build/memory_probe32.exe `
  --label repeat-main --backgrounds 768 1024
& $terrainPython tools/terrain_bake_prototype/memory_validate.py probes `
  --probe build-tests-terrain-memory-synthetic/probe-build/memory_probe32.exe `
  --label repeat-assembled --backgrounds 768 1024 --modes assembled
& $terrainPython tools/terrain_bake_prototype/memory_validate.py probes `
  --probe build-tests-terrain-memory-synthetic/probe-build/memory_probe32.exe `
  --label repeat-pressure --backgrounds 1536 --modes assembled
& $terrainPython tools/terrain_bake_prototype/memory_validate.py summarize `
  --labels repeat-main repeat-assembled repeat-pressure
```

Labels должны быть новыми одиночными именами: runner отказывается заменять
существующие результаты. Порог timeout 120s на fresh process; timeout был бы
отдельным TIMEOUT, не PASS. Имеющийся toolchain используется без изменения.
При изменении game.exe LAA runner откажется сравнивать неподходящий helper.
Exit codes helper: 0=complete, 2=invalid arguments, 3=input invalid,
10=allocation failure. Падение/неполный stdout классифицируются ERROR.

## Рекомендация, уверенность и дальнейшее использование

128px сохраняет stock-scale и даёт четырёхкратную экономию площади против
256px. 2048×128 оставляет в тяжёлом synthetic профиле 1500,4 МиБ free /
1345,1 МиБ largest; это консервативный default. 4096×128 остаётся разумным
upper budget с измеренным запасом, но compiler/VA evidence не гарантирует
память driver/runtime. Нужны явные overflow и резервы под pinned/animation/
overlay slots. Если recipe budget карты превышает 4096, это не разрешение
расширять атлас до 8192 молча. Удвоение до 8192 при assembly расходует запас.

256px не добавляют исходной детализации в этих kits. 1024×256 условно
проходят, но уже хуже фрагментация; 2048×256 не рекомендуются как default,
4096/8192×256 отвергаются в исходном профиле с постоянными RGBA-копиями. Синтетический лимит
1024 не является доказанным игровым пределом.

Все разрешённые этапы этой задачи выполнены. Важные ограничения: нет игры,
не измерен её baseline/current residency/реальный driver, не установлен
алгоритм/copies движка и неизменность старых JMV fingerprints. Exact game
capacity и художественная/игровая совместимость не заявлены.

## Новый рендер

Продолжение 2026-10-10 по сообщению владельца о новом рендере. Выполнено
только чтение установленных бинарников, соседних документов/конфига/старого
лога и дополнительные console-пробы; игра и D3D11 device не запускались.
Далее **факт** означает наблюдаемое содержимое файла или измерение probe;
политика времени жизни текстур без её исполнения остаётся **гипотезой**.
[PE, ресурсы, импорты, экспорты, строки и даты](../../build-tests-terrain-memory-synthetic/renderer-facts.json),
[повторное чтение и проверки продолжения](../../build-tests-terrain-memory-synthetic/renderer-receipt.json).

### Идентификация и общий процесс

Основной wrapper — **`C:/GOG Games/PZ/Engine/ddraw.dll`**, EID3D11,
11 401 216 байт, SHA256
`271df834174701f6743199b77934a243b4ff03008cd4ccff816eb504501b83d1`.
PE32/I386, 32 bit, Characteristics 0x2102. **VERSIONINFO отсутствует**:
в дереве ресурсов только тип 10; GetFileVersionInfoSize возвращает 0,
ошибка 1813. Поэтому FileVersion/ProductVersion/FileDescription из ресурсов
указать невозможно. Имя EID3D11 подтверждается строками DLL и INSTALL.md,
а не выдуманным номером версии. В DLL есть отдельная строка revision
`527db396390400596a711e9e596a646c74ffd60e` (offset 0x4bb298);
это встроенный идентификатор, не доказательство чистоты/воспроизводимости исходников.

DLL экспортирует собственные, не forwarded `DirectDrawCreate` (RVA 0x22e90)
и `DirectDrawCreateEx` (0x22fe0), импортирует **D3D11CreateDevice** из
d3d11.dll, D3DCompile из D3DCOMPILER_47.dll. game.exe импортирует DDRAW.dll.
Это архитектура локального **32-битного слоя в процессе игры**. Старый
`Engine/owned_hitches.log` перечисляет в одном списке модулей game.exe
(00400000), DDRAW.dll (517F0000), d3d11.dll (51600000), что поддерживает
этот вывод для зафиксированной в логе сессии. Лог не содержит fingerprint
сопоставляемой DLL, поэтому не удостоверяет исполнение нынешнего hash.
Обычных CreateProcess-импортов wrapper не имеет; ShellExecuteW имеется,
и отсутствие CreateProcess само по себе не исключает динамических вызовов.
В соседней документации и исследованных строках свидетельств отдельного
64-битного render broker не найдено. Наличие такого скрытого пути не доказано.

Итог: новый D3D11 backend **не переводит game.exe в 64 bit**. Для выбранного
32-битного game.exe с LAA всё остаётся в общем диапазоне до **4 ГиБ VA под
WoW64**. LAA=false у DLL не уменьшает этот диапазон: флаг главного EXE
определяет предел процесса. GPU может хранить больше данных, чем CPU VA,
но heaps, CPU-копии, отображения и пользовательская часть драйвера занимают
тот же VA. Импорты подтверждают доступный путь, не выбор DLL в новой сессии.

Рядом существует **`Engine/RDDraw.dll`** — тоже PE32/I386 с D3D11CreateDevice,
11 149 824 байта, SHA256
`898240e388bcec5b7b1adb6cb530f8b851db84b08d51664206de9c2a97e20aaf`.
VERSIONINFO также нет; дополнительный экспорт EI_Renderer_QueryApi
связывает его с EIRebuild. EIRebuild.exe тоже 32-bit LAA. Его выбор/загрузка
не измерялись, и два wrapper не считаются двумя независимыми процессами.

`Engine/INSTALL.md` описывает тестовый EID3D11 **c55fb3f**, полный revision
c55fb3f73d9e15511b6263eccd34aec1728cf234 и DLL SHA
E9D4B92B4A18CE4E2D67A003A587D4ABC143CBD3CEE4D6D13A217E63F0E8ED7D.
Это **другой бинарник**: документ пригоден как описание назначения wrapper,
но не как версия текущего файла. Соседние README.md/manifest описывают
EIRebuild API3r7, и их EXE/RDDraw hashes тоже отличаются от установленных.
`D3DImm.dll.bak` имеет VERSIONINFO dgVoodoo 2.87.5 (ProductVersion 2.8.7.5,
FileVersion 4.7.1.3000); это резервный файл, не подтверждение активного renderer.

Текущий **`Engine/eid3d11.ini`**: overlay mode=hidden,
debug_resource_view=disabled, render_scale=85, terrain_lod=true,
terrain_quality=1, terrain_blend=1, terrain_height_blend=true,
terrain_tessellation=0, neighbour_maps=true; antitile/triplanar/macro включены.
Параметров compression/systemmem/managed/staging/cache lifetime здесь нет.
Quality/render scale не доказывают отсутствие CPU-копии атласа;
neighbour_maps и дополнительные эффекты могут менять прочую нагрузку.

### Где могут находиться текстуры

| Наблюдение в нынешнем ddraw.dll | Что подтверждено | Что ещё неизвестно |
|---|---|---|
|0x412f70: `terrain page has no matching BC1 mip chain`; 0x4b9d88: ошибка reread selected terrain mip chain / terrain slice upload|Есть путь чтения исходной BC1/DXT1 mip-цепочки и загрузки terrain slice|Используют ли его все страницы этой карты и остаётся ли GPU формат BC1 после всех преобразований|
|0x4b9de0/0x4b9e78: mismatch/no shipped chain → one-level texture, terrain material disabled|Есть fallback при отсутствии подходящей исходной цепочки|Формат, число CPU-копий и срок жизни fallback surface|
|0x73ea8c: `CPU surface storage: %.1f MiB`; 0x73eaf8: GPU-only surfaces, CPU freed, pinned|Wrapper различает CPU storage, освобождённые CPU buffers и pinned surfaces|Какие terrain surfaces освобождены, какие pinned, сколько остаётся у игры|
|JSON schema: proxy_surface_cpu_bytes, surface_gpu_bytes, surface_double_resident_bytes, surface_released_bytes/count, pinned_count, surface_restore_readbacks/ms|Есть учёт двойной residency, освобождения и восстановления surface readback|Текущие значения этих счётчиков: в прочитанных файлах измерения атласа отсутствуют|
|owned_immutable_bytes, owned_archive_owners, owned_mapped_views, owned_texture_source_bytes, transient_upload_bytes; MapViewOfFile/CreateFileMappingA imports|Есть механизмы собственных источников, archive mappings и transient upload|Полный размер mapped archive/source и время жизни относительно игровых копий|
|Surface.Lock/Unlock, D3D7.EvictManagedTextures, CompleteCreateSysmemSurface export|Реализован DirectDraw/D3D compatibility слой|Фактические DDSCAPS/DDSCAPS2 каждой terrain surface|
|0x4bb77c staging texture error; 0x4bbbb0 bounded staging ~128 MiB, рядом frame/shadow temporal capture|Есть staging для диагностического захвата кадров|Эти 128 МиБ не доказаны как постоянная staging-копия terrain|

Строки доказывают **наличие механизма**, а не прохождение его ветви. Нет
достаточных данных, чтобы утверждать «весь атлас всегда GPU-only» или
«wrapper всегда распаковывает весь атлас в RGBA». BC1 path и CPU release
дают основание проверить более экономную модель; постоянные R и unified C
для этого пути не установлены. Приватные исходники renderer не читались.

Семантика API сверена без сети по установленным публичным Windows SDK
10.0.22621.0 headers `um/ddraw.h` и `um/d3d11.h`:
DDSCAPS_SYSTEMMEMORY=0x800 требует system memory;
DDSCAPS2_TEXTUREMANAGE=0x10 / D3DTEXTUREMANAGE=0x20000 задают управление
texture в старом DD/D3D API. Наличие этих констант в SDK **не доказывает**,
что игра выставила их для террейна. D3D11 имеет DEFAULT/IMMUTABLE/DYNAMIC/
STAGING, не D3D9-style managed usage. CPU source buffers, surface backing,
archive views, readback/staging и driver mappings потребляют VA процесса;
BC1 payload, размещённый только на GPU, не обязан целиком иметь CPU mapping.
GPU allocation D нельзя автоматически ни прибавить целиком к VA, ни
исключить все драйверные затраты. Точный вклад driver VA здесь неизвестен.

### Относятся ли прежние пробы к этому рендеру

Исправленные 4096=OK / 8192=crash в EXPANDED_TERRAIN_ATLAS_RUNTIME.md —
**2026-07-29**, JMV Zone18, `D:/Evil Islands`, мод JamaisVu135.
Текущая установка — `C:/GOG Games/PZ`, DejaVu. В старом отчёте нет hash,
revision или имени EID3D11; упоминание ddraw_proxy.log не идентифицирует
wrapper. Следовательно, отнести июльские результаты к нынешней DLL нельзя;
точный прежний renderer также не установлен.

Наблюдаемые file times, UTC:

| Файл | CreationTimeUtc | LastWriteTimeUtc |
|---|---|---|
|Engine/ddraw.dll|2026-10-10 04:25:15.584162|2026-10-10 02:50:16|
|Engine/RDDraw.dll|2026-10-06 04:45:46.904506|2026-10-09 20:17:56.444144|
|Engine/INSTALL.md|2026-10-06 03:08 (округлено)|2026-10-05 23:51 (округлено)|
|Engine/eid3d11.ini|2026-10-10 04:31:32 (округлено)|2026-10-10 04:31:42 (округлено)|

Это даты файлов нынешней папки, не неизменяемый журнал первого появления:
копирование/замена может менять или сохранять timestamp. Обе нынешние DLL
имеют PE debug type 16 (REPRO); их COFF timestamp нельзя считать датой сборки
(в частности, интерпретация ddraw.dll даёт 2031 год). Связка июльского пути,
отсутствия renderer identity и октябрьских файлов исключает использование
старого порога как установленной capacity нынешнего wrapper. Исторические
измерения private/committed/largest из раздела 3 сохранены как данные старой
сессии, а её причина crash остаётся гипотезой.

### Пересчёт и профиль x86 D3D11 wrapper

Новая **гипотеза пути**: сохраняем compressed source D, GPU получает BC1 D,
не держим постоянный RGBA+mip R и не собираем единый RGBA C. На время upload
есть read buffer Dpage и compressed staging Dpage. Для driver VA добавлен
явный запас **M=D**; это выбранный сценарий, не измеренное отображение
VRAM один к одному. Вариант `d3d11-rgba` добавляет временный decode **одной
страницы Rpage**, освобождаемый после upload. Также можно задать отдельный
фиксированный staging; в проведённой матрице он равен 0.

Формулы пика без фоновой нагрузки/allocator overhead:
VA(`d3d11`) = D + M + 2Dpage;
VA(`d3d11-rgba`) = D + M + 2Dpage + Rpage.
Private(`d3d11`) = D + 2Dpage. GPU D показан отдельно, он **не выделяется**
синтетическим процессом и не прибавляется второй раз к VA.
[Полная пересчитанная модель](../../build-tests-terrain-memory-synthetic/memory-model.json).

| Слоты × px | D source и условный GPU D, МиБ каждый | Peak VA без/с one-page RGBA, МиБ | Peak private без RGBA, МиБ |
|---|---:|---:|---:|
|2048×128|21,334|44,001 / 49,334|22,667|
|4096×128|42,667|86,668 / 92,002|44,001|
|8192×128|85,335|172,003 / 177,337|86,668|
|2048×256|85,334|176,001 / 197,334|90,667|
|4096×256|170,667|346,668 / 368,002|176,001|
|8192×256|341,335|688,003 / 709,337|346,668|

Probe читает настоящие RES/MMP. 2048/4096 — точные kits; 8192 — повторное
чтение 64 страниц 4096-kit в **128 независимых выделений**, не готовая 8192
карта. D сохраняется через VirtualAlloc; M моделируется реальными
pagefile-backed CreateFileMappingW/MapViewOfFile (MEM_MAPPED), в каждый
mapping копируется DXT payload. Сжатые read/upload buffers освобождаются
после страницы; `d3d11-rgba` действительно декодирует все mips каждой страницы.
Checksum=0 у `d3d11` ожидаем: этот режим не декодирует RGBA.
Это CPU memory/VA simulation, **не исполнение D3D11 и не замер GPU**.
MEM_MAPPED commit отличается от PrivateUsage, поэтому оба измерены отдельно.

Новая сборка PE32 LAA сохранена отдельно в `probe-build-wrapper`, SHA256
`979e1490aa6b877987a619b9d0092f83ee9df6734f9e3da43982bb01e46de9a8`.
Пробы: 3 budgets × 2 sizes × 2 modes × 2 backgrounds (1024/1536 МиБ private),
всегда reserve=512 МиБ, та же схема дыр/разделителей, что раньше.
**24 PASS, 0 ALLOC_FAIL/ERROR/TIMEOUT; 3680 полных VA snapshots**.
Сканировано 4 294 901 760 байт в каждом снимке; итоговые source/mapped bytes
каждого запуска точно равны D. Общий peak при создании фоновых дыр иногда
выше atlas peak; ниже приведена именно фаза загрузки атласа.

Тяжёлый B=1536, reserve=512. Значения peak occupied VA указаны для
`d3d11` / `d3d11-rgba`; private, committed, largest и final free — для второго,
более тяжёлого варианта. Всё в МиБ; largest — минимум за atlas phase,
final free — после освобождения временных буферов.

| Слоты × px | Peak occupied VA без/с RGBA | Peak private | Peak committed VA | Min largest | Final free |
|---|---:|---:|---:|---:|---:|
|2048×128|2111,40 / 2116,74|1568,47|1593,52|1472,44|1985,87|
|4096×128|2154,15 / 2159,49|1589,94|1636,27|1485,69|1943,12|
|8192×128|2239,65 / 2244,99|1632,97|1721,78|1494,06|1857,62|
|2048×256|2243,40 / 2264,74|1652,81|1741,52|1467,48|1857,87|
|4096×256|2414,15 / 2435,49|1738,57|1912,28|1462,60|1687,12|
|8192×256|2755,65 / 2776,99|1909,97|2253,77|1130,10|1345,62|

[Wrapper matrix и исходные JSONL](../../build-tests-terrain-memory-synthetic/wrapper/summary.json).
Дополнительная проверка изменённого helper сохраняет прежнюю качественную
границу RGBA/assembly при B1536+reserve512: **4096×128 PASS** (largest909,9),
**8192×128 ALLOC_FAIL** на contiguous528 МиБ (largest416,8, Win32 error8).
Это ещё **393 снимка**, не второй PASS для отказавшей загрузки.
[Связанная регрессия](../../build-tests-terrain-memory-synthetic/wrapper-regression/summary.json).
Изменение largest относительно прежней сборки ожидаемо из-за layout/ASLR;
новые числа не заменяют исторические измерения.

Команды воспроизведения (новые labels и отдельная output directory для build):

```powershell
$terrainPython = 'C:/Users/samol/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'
& tools/terrain_bake_prototype/memory_probe32.ps1 -OutputDirectory build-tests-terrain-memory-synthetic/probe-build-wrapper-repeat
& $terrainPython tools/terrain_bake_prototype/memory_validate.py renderer --files 'C:/GOG Games/PZ/Engine/ddraw.dll' 'C:/GOG Games/PZ/Engine/RDDraw.dll' 'C:/GOG Games/PZ/Engine/game.exe' 'C:/GOG Games/PZ/Engine/D3DImm.dll.bak' --documents 'C:/GOG Games/PZ/Engine/INSTALL.md' 'C:/GOG Games/PZ/Engine/README.md' 'C:/GOG Games/PZ/Engine/eid3d11.ini'
& $terrainPython tools/terrain_bake_prototype/memory_validate.py model
& $terrainPython tools/terrain_bake_prototype/memory_validate.py probes --probe build-tests-terrain-memory-synthetic/probe-build-wrapper-repeat/memory_probe32.exe --label wrapper-repeat --slots 2048 4096 8192 --backgrounds 1024 1536 --modes d3d11 d3d11-rgba
& $terrainPython tools/terrain_bake_prototype/memory_validate.py probes --probe build-tests-terrain-memory-synthetic/probe-build-wrapper-repeat/memory_probe32.exe --label wrapper-regression-repeat --slots 4096 8192 --pixels 128 --backgrounds 1536 --modes assembled
```

Параметр `--wrapper-staging-mib` допускает отдельную CPU staging нагрузку.
Он не применялся в этой матрице: упомянутые в DLL ~128 МиБ относятся к
debug capture и не объявлены обязательной нормальной стоимостью terrain.
Дополнительная постоянная CPU RGBA-копия всего атласа дала бы +R, а не
+Rpage; для 8192×256 это +2730,667 МиБ и уничтожает запас выбранного профиля.
Такую ветвь уже покрывают исходные RGBA/amplified/assembled модели.

### Обновлённая рекомендация

Новый рендер **может существенно снять прежнюю амплификацию памяти**, если
реально использует BC1 mip upload и освобождает CPU RGBA surfaces.
Синтетика показывает: **8192×128 и 8192×256 помещаются с запасом** в выбранной
схеме D+M+постраничные buffers даже при B1536+reserve512. Поэтому прежние
4096=OK / 8192=crash **не являются универсальным пределом нового renderer**.
Уверенность в этих измерениях высокая; в соответствии выбранной схемы всей
игровой загрузке — низкая/умеренная, поскольку строки не показывают residency.

Практическая рекомендация для бюджета, который должен выдерживать также
неизвестный fallback/старый RGBA путь, **сохраняется: 128px default2048,
upper4096; 256px conditional upper1024**. Это консервативное планирование,
не доказанный предел игры. Для **явно выбранного сжатого EID3D11 пути**
8192×128 — обоснованный синтетический кандидат; 8192×256 тоже прошёл, но
расходует в четыре раза больше D и имеет меньший запас (1345,6 free /
1130,1 largest), поэтому 128px предпочтительны. Объявлять 8192 безопасным
универсальным production budget по этим данным нельзя.

Не установлены DDSCAPS конкретных terrain surfaces, pinned/restore поведение,
полное archive mapping, скрытые постоянные RGBA/C, actual UMD VA и baseline
DejaVu с neighbour_maps. Ни renderer identity июльской сессии, ни точная
capacity нынешней игры не подменены гипотезами. Требование владельца
«проверить синтетически» выполнено; ручной запуск не оставлен обязательным
этапом и не запрошен. Реализация бюджета/новых terrain функций вне владения.

## Точный список для коммита сопровождающим

- `tools/terrain_bake_prototype/memory_probe32.cpp`
- `tools/terrain_bake_prototype/memory_probe32.ps1`
- `tools/terrain_bake_prototype/memory_validate.cpp`
- `tools/terrain_bake_prototype/memory_validate.pro`
- `tools/terrain_bake_prototype/memory_validate.py`
- `docs/validation/ATAMAN_TERRAIN_MEMORY_SYNTHETIC_20261010.md`

## Предлагаемый пункт ROADMAP (файл не изменялся)

### EI-TERRAIN-MEMORY-SYNTHETIC — синтетический бюджет атласа

- Поручение владельца через Claude 2026-10-10: заменить ручной игровой memory gate синтетикой; проверить PE/LAA/API, комплекты загрузчиками Ataman, модель, x86 VA/fragmentation и влияние нового D3D11 wrapper. База main VERSION131 `6fbcd4e918a84d90460e0675b9777df08327c5d7`; после параллельного этапа1B HEAD `99bc5b720dadf3f34c548fd2d23e5334c245c6d0`, VERSION131.
- Границы: только новые memory_probe32.*, memory_validate.*, private build-tests-terrain-memory-synthetic и отчёт; без программы/версии/установок/живых данных/сети/игры/UI. Git только чтение, коммит сопровождающим; чужая параллельная задача сохранена.
- Статус: завершено в согласованных синтетических границах. Четыре kits:192 страницы/12288 слотов/2208 decoded mips, по96 SEC, classes/rotations/overlay mismatch0. Всего130 x86 запусков:115 PASS/15 ALLOC_FAIL/0 ERROR/0 TIMEOUT,15901 VA snapshots. Новый wrapper — Engine/ddraw.dll EID3D11 PE32, in-process D3D11, без VERSIONINFO; game.exe LAA даёт общее до4GiB VA. Строки подтверждают BC1 mip path и механизм CPU release, но не actual residency. Новый профиль D+условный mapped D+постраничные upload/decode:24/24 PASS для2048/4096/8192×128/256 приB1024/1536+reserve512. 8192×256 в тяжёлом варианте:free1345.6MiB/largest1130.1MiB. Связанный RGBA regression:4096×128 PASS/8192×128 ALLOC_FAIL на528MiB.
- Evidence: `docs/validation/ATAMAN_TERRAIN_MEMORY_SYNTHETIC_20261010.md`, раздел «Новый рендер»; private structural.json,all-probes.json,renderer-facts.json,renderer-receipt.json. Июльские JMV пробы относятся к другому пути/установке, identity renderer неизвестна; они не доказывают предел нынешней DLL. GPU не измерялся, simulated occupied не приравнен к historical committed.
- Решение/следующий шаг: консервативное планирование с неизвестным RGBA fallback —128px default2048/upper4096,256px conditional upper1024. Для сжатого EID3D11 пути8192×128/256 — прошедшие синтетические кандидаты, не универсальная игровая capacity;128px предпочтительны. Ручной memory gate отменён владельцем. Не менять программу/бюджет автоматически; production terrain цели и их остальные критерии сохраняются, реализация по отдельному поручению.
