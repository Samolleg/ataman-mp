# EI-REBUILD64-RENDERER-PROBE — этап 1, 2026-11-05

## Результат и решение

**Частичный технический успех: общий upstream-декодер и шейдеры настоящего рельефа работают вне игры на аппаратном D3D11 через MSVC DLL, вызываемую MinGW 7.3. Полный production `owned::Renderer` вне игры не запущен.** Это два разных результата, их нельзя объединять в PASS полного Rebuild64.

- У embedded-поставки удалось собрать `OwnedRenderer.lib`, преодолев диагностические заголовки этапа 0. Минимальная DLL с настоящим `Renderer` не линкуется: после `/Gy`, `/OPT:REF` и подключения независимых адаптеров остаются **68 внешних символов**, включая поставщиков из игрового слоя. На этом пути применён стоп-критерий владельца; игровые классы, их заголовки и функциональные заглушки не переносились.
- Отдельный upstream-target **shader-lab** действительно предоставляет независимый путь настоящего рельефа. Для него сделан ограниченный собственный DLL-хост. Это лабораторный потребитель общих декодера/шейдеров, **не тот же полный Renderer, что в EIRebuild64**. Он обходит его scene/presentation/cache orchestration.
- На DejaVu `zone1` получены текстурированный рельеф, два парных ракурса с GL и изменение рельефа в памяти. **MOB-объекты и персонаж не реализованы; речная/морская вода визуально не воспроизводится корректно; игровое освещение/небо/туман на 12:40 не подтверждены.** Частичные кадры подписаны соответственно.
- В лабораторном пути полная смена поколения массивов высот/материалов до готового кадра: **median 545.745 мс, p95 646.848 мс, 50 замен**. Immediate Private Bytes выросли на **289.098 МиБ**; после ещё 30 кадров остаточный рост около **110.34 МиБ**. Такой путь пока непригоден для обновления на каждом движении кисти.

**Продолжать техническое выделение полноценного game-free renderer target — да. Начинать встраивание этой лаборатории как готового вида Rebuild64 — нет.** Перед UI нужен успешно слинкованный и запущенный полный backend с реальными независимыми поставщиками мира и контролируемым освобождением поколений. Этап 1 завершён с частичным результатом и открытым препятствием, а не с приёмкой полного renderer.

## Полномочия, идентичность и сохранность

Применено поручение владельца от 2026-11-05: переключатель OpenGL / Rebuild64 в будущем; текущий OpenGL и инструменты сохраняются. Разрешение Ilufus на использование описано **со слов владельца**; исследование не выдаёт его за отсутствующее, но письменный документ здесь не проверен. Публикации/поставки этой пробы нет.

`AGENTS.md` прочитан с диска и проверен перед отчётом: SHA-256 `2A2F58EF34F63D6C5D67D165D0B809B046D0A519181119D22EA8FDD8781FE3B7`, tracked diff отсутствует. Прочитаны CONTRIBUTING, договоры данных/сборки, относящийся пункт ROADMAP и [этап 0](ATAMAN_REBUILD64_RENDERER_STUDY_20261105.md). Профильных навыков `evil-islands-modding` / `evil-islands-private-engine-reference` нет; неизвестные приватные источники не использовались.

Маппер: `main`, архив исходников для GL — **`761f52765835aaf1922d8a9ce24e7f1270655dc3`**, программная версия в этом архиве **279**. Рабочие изменения EI-SAVE-INTEGRITY-280 не входят в архив. `source/`, `tests/`, VERSION, CHANGELOG, ROADMAP, Readme, `ei_maper.pro`, переводы и чужие документы этим заданием не редактировались. Номер не занимался. Только этот отчёт входит в docs-коммит; SHA коммита сообщается отдельно, чтобы не создавать самоссылку внутри коммита.

Всё остальное находится в `work/rebuild64-renderer-probe-20261105/` (далее **W**), вне Git маппера. В маппере и чужих checkout не создавались worktree/ветки/remotes; issues, push/fetch в чужие checkout и изменения аккаунтов/настроек Git не выполнялись. Upstream клонирован с `--no-checkout` (обычные локальные main/origin самого клона); из него читались зафиксированные объекты Git. Изменяемые эксперименты находятся в отдельных archive-копиях W, не в `upstream/`.

Checkout EIRebuild64 владельца не изменялся: HEAD `d9bdfc1042a939ed48cbd42b13c5ba15735b7775`, `origin/main` `81dbcdd29b41baeb346f167c6dfb511dff05f295`. Чтение выполнено через `git show`/`git archive` соответствующего объекта. Его игравший EXE и установленные renderer/host не запускались.

Игра `C:/GOG Games/PZ` использовалась только для чтения. Маппер владельца, игра, EIRebuild, мосты и computer-use не запускались/не подключались. Чужие процессы не завершались. Изолированные собственные процессы создавали только невидимые окна; снимки — readback собственных render targets, не экран владельца. Не использовались registry writes, clipboard, subst или junction; сборочные пути заданы через `C:/GOGGAM~1/maped/EI-MAP~2`. Постоянная установка Ataman не изменялась. Финальная проверка `subst` должна оставаться пустой.

## Сравнение трёх баз

| База | Что действительно есть | Зависимость и выбор |
|---|---|---|
| upstream `main`, **`d17a6149e5b5d4c2d23873d18e037bc270269050`** | `Host/host.mak` собирает настоящий x64 host; `Host/core_host.cpp`, `core_world.cpp`, `host_bridge.cpp` принимают transport/scene values. Отдельно `Prototypes/shader-lab` читает реальные карты без игры. | `Host/host.cpp:1–8,32–62` — remote-процесс, которому нужны PID/HWND и публикации клиента; это не готовый `load_map(MPR,MOB)` SDK. Штатный CMake по-прежнему ограничен x86. **Выбрана main для успешно запущенной ограниченной пробы shader-lab.** Это реальная независимая точка входа, но не доказательство самостоятельности полного production Renderer. |
| `codex/x64-scene-host-prototype`, **`00461a736e1a5768d8800ba6555c5b5aff3b0eb9`** | Самостоятельные D3D12 host/publisher, fixed-width wire, скрытая presentation-проба. Общий предок с main — `52adb320d1a2c316fe098165591487eb127425af`. | README прямо ограничивает результат синтетическими мешами и не объявляет его EI renderer. Нет загрузки zone1 с её ресурсами. Не выбран для подмены реального кадра синтетической сценой. |
| EIRebuild64 embedded, **`81dbcdd29b41baeb346f167c6dfb511dff05f295`** | `OwnedRenderer/OwnedRenderer.vcxproj`, типизированные E1World/E1Terrain, surfaces, поколения, отдельный shader producer. PROVENANCE указывает upstream `b1ffc736d4792eb36f3528a8d8ee2c358da80d54` и локальные изменения, включая x64/NGX. | Выбран первым для пробы **полного** Renderer, поскольку его typed scene/snapshot ingress ближе к редактору. После фактической link-проверки отклонён как самостоятельная готовая поставка: часть обязательных providers находится в игровом слое. Не заменён игровыми заглушками. |

Наличие установленного `C:/GOG Games/PZ/Engine/eid3d11_x64_host.exe` (44 582 400 байт) подтверждает наличие файла x64-host, но не заменяет проверку исходников/собственного хоста. Он не запускался и не копировался в пробу.

## MSVC build/link и точный стоп полного Renderer

VS2022 BuildTools, v143 (MSVC tools 14.44.35207; MSBuild 17.14), Release/x64, C++17. TEMP/TMP и все outputs — W. В `W/embedded` через `git archive` взяты только `OwnedRenderer`, `external/owned-renderer`, `Game/OwnedRender`, без игровых классов и stdafx движка.

Допустимые собственные диагностические заменители, написанные заново:

1. `embedded/Game/CorePerf.h`: пустые scopes/detail counters, диагностический `frameNumber=0`; не таймер/состояние игры.
2. `embedded/Game/GameJournal.h`: пустой диагностический `Event`, без записи игрового журнала. CorePerf включает его, поскольку `renderer_frame.cpp` рассчитывает на транзитивную декларацию.

Из build-настроек своей копии добавлен только `FunctionLevelLinking=true` (`/Gy`); алгоритмы renderer не правились. Сначала библиотека собрана в штатном режиме после исправления диагностических деклараций, затем с `/Gy` для проверки удаления ненужных функций. Итоговая `.lib`: **141 081 350 байт**, SHA-256 `7584A03ABDDBEFFFBC412F809350CE888B634BEFDD3F6D16A2202D6594F385CD`. Есть предупреждения компилятора и LNK4006 о дублирующемся `bridge_prepare_neighbour_terrain`; это не полный link PASS.

Собственный `probe_host.cpp` создаёт настоящий `owned::Renderer`; экспортированы version/create/destroy с C ABI и перехватом исключений внутри DLL. Для устройства выбран имеющийся путь `adopt_host_device` → `ensure_host`: обычный `ensure()` в embedded безусловно инициализирует NGX (`renderer_device.cpp:631`), а NGX-код создаёт `%LOCALAPPDATA%/EIRebuild64/NGX`. Обычный `ensure()` **не запускался**. Путь adopt/ensure_host тоже не объявляется runtime PASS: DLL не слинкована.

Первая полная link-попытка: **94** unresolved. Следующая — `/Gy` + `/OPT:REF`, с независимыми E1World, E1Terrain, E1Scene, E1Surface, E1TextLifetime, E3Scale, ResourceMapping, NeighbourTerrain: **68** unresolved, `LNK1120`. Среди них остаются не только диагностика:

- `E1CompleteVisualFrame`: поставщик `Game/OwnedRender/E1CompletedVisuals.cpp:4–11,123–126` включает stdafx, WorldClient, Object3DClientSpecific, Unit/UnitClientSpecific.
- `camp_inventory_blit`, `camp_inventory_preview_specular_rgb`, связанные camp-операции: `E4Camp.cpp:1–8,191,215` зависит от игровых RendererDX11/Renderer и stdafx.
- game focus/HUD/input: `E3Input.cpp:1–8,98` включает игровой ErrorDialog и связанную игровую модель ввода.
- Поставщики ambient/cover/footprints, capture и engine material scan также остаются в link-списке. Полный список сохранён в журнале, в отчёт код не переносится.

Это устанавливает **неполноту данного самостоятельного target**, а не математическую невозможность написать новый адаптер или использовать другие интерфейсы upstream. Удаление несвязанных функций и подключение разрешённых независимых producers не устранили игровую границу. Запускать полный EIRebuild64, копировать приватные headers либо писать ответы, притворяющиеся игровыми providers, не стали.

Доказательства: `W/logs/embedded-library-r2.log`, `embedded-library-gy.log`, `host-link2.log`, **`host-link3-gy.log`**, соответствующие `.exit`. Исходники неудавшегося хоста и `.lib` сохранены; успешной DLL полного Renderer нет.

### DLSS/NGX

Штатного no-NGX переключателя у выбранной embedded-поставки нет: `DlssSdk.props` безусловно проверяет SDK и добавляет библиотеку, `.vcxproj` включает `dlss_backend.cpp`. Для compile/link-пробы использован **read-only** SDK 310.9.16 из dependencies EIRebuild64; SDK/NGX runtime в W не копировались.

Upstream full x64 host также штатно требует DLSS SDK, FidelityFX FG, NVAPI и DXC (`Host/build.cmd`, `host.mak`), хотя лабораторному рельефу они не нужны. В штатном shader-lab их нет: **успешный ограниченный target собран и запущен без NGX/DLSS**. Это не новая no-DLSS конфигурация полного Renderer.

## Независимый DLL-хост и вызов MinGW

Успешный `W/bin/terrain_lab_probe.dll` собран MSVC v143 x64 `/MD`. Собственный target содержит shader-lab world/params/image и только четыре общих `.cpp`: owned_terrain_decode, owned_terrain_front_end, owned_asset_loader, owned_resource_store. Сначала опробован старый aggregate recipe shader-lab: на нынешней main он подтягивает runtime/neighbour/retained зависимости. Итоговый target исключает их, а не имитирует их ответы. Промежуточный `Host/host_bridge.cpp` в успешный target **не входит**.

Изменения archive-копии shader-lab ограничены `lab_world.h/.cpp`: удалены неиспользуемые game-root/config discovery entry points и добавлен `load_from_mounts`, принимающий явные mounts и при необходимости независимый CPU-снимок terrain. Общие loader/decode и HLSL не изменены. Собственный `lab_diagnostics.cpp` лишь печатает диагностический `bridge_append_probe_event`; функциональных bridge stubs в успешном target нет. Все промежуточные файлы сохранены локально, окончательный состав — `W/lab_host.vcxproj`.

Узкий ABI v1: `lab_version`, `lab_create`, `lab_load_map`, `lab_set_camera`, `lab_set_time_of_day`, `lab_render_frame`, `lab_capture_png`, `lab_get_terrain_snapshot`, `lab_replace_terrain_snapshot`, `lab_get_statistics`, `lab_destroy`. `lab_api.h` содержит fixed-width fields, HWND, borrowed указатели/размеры, caller-owned buffers и фиксированный error record. Через границу нет Qt/STL/исключений или обязанности освобождать MSVC-allocations в MinGW. Exception handlers остаются внутри DLL. Числовые ошибки: 1 exception, 2 ABI/arguments, 3 device, 4 mounts, 5 source, 6 shader compile, 7 target, 8 map, 9–11 frame/capture, 20–23 snapshot/statistics.

Это **ограниченный ABI пробы**: `load_map` принимает имя MOB, но возвращает явную диагностику `MOB_NOT_RENDERED` и не загружает её в renderer; `set_time_of_day(12,40)` задаёт только animation clock 45 600 секунд, не игровую модель солнца/тумана. Эти функции не считаются завершённой реализацией соответствующих полных игровых возможностей.

`W/bin/mingw_client.exe`: **GCC 7.3.0**, retained kit `qt.tools.win64_mingw730`; `LoadLibraryW/GetProcAddress`, MinGW не линкует MSVC C++-объекты. Обе стороны x64; `sizeof(lab_create_info)=88`, ABI проверен. Собственное окно WS_POPUP создано с WS_EX_TOOLWINDOW/WS_EX_NOACTIVATE в (-32000,-32000), не показывается/не активируется; в логах **`window_visible=0`**. Оно не требуется лабораторному выводному target: фактический рендер полностью offscreen. WARP не использовался.

Драйверный путь — `D3D11CreateDevice(nullptr,HARDWARE,...)`, feature level 11.0. Отдельная проверка того же default-device пути (`W/bin/gpu_identity.exe`, `logs/gpu-identity.txt`) определила **NVIDIA GeForce RTX 4070 SUPER**, vendor 10de/device 2783, dedicated memory 12 579 766 272 байт; Windows driver 32.0.16.2101. Это отдельный default-device probe, а не записанный в каждом кадре LUID. На компьютере также есть AMD iGPU.

Успешные реальные exits 0: `logs/zone1-lab-village.exit`, `zone1-lab-coast.exit`. Compile/link receipts: `lab-build5.exit`, `lab-snapshot-build.exit`. DLL SHA-256 **`9183F81905F9FD1930FFE7E264791D388EA60FA3CF5D2538476AA5884A30F4D2`**, MinGW client **`26BB1D49BAB3595C4E0C4CFB35C85F24C26265F67F61824DDBD46D2FBE5FAFA0`**.

## Zone1 и парные кадры

Данные читаются непосредственно: DejaVu Maps и Base Maps; loose Res roots; texture archives в порядке texture-manager из текущего DejaVu config.reg, затем Base/Res/textures.res. Среди них **HDPack_Terrain.res**. Перечень фактически открытых mounts напечатан в логах. Успешный ограниченный target использует текстурную цепочку; figure archives ему не нужны, поскольку MOB-геометрия не поддержана. Это не проверка полного model mount-chain.

Реальная карта: **11×9 секторов, 352×288 м; 19 текстурных страниц по 2048; 99 секторов, 313 164 vertices, 278 368 triangles, 1065 commands** по логу декодирования лаборатории. Поля «7 objects» этого лога — terrain material objects MPR, **не 7 объектов MOB**.

| Область | Результат на реальных данных |
|---|---|
| Рельеф и настоящие материалы | PASS ограниченной лабораторной пробы: узнаваемая геометрия, дороги, скалы, реальные выбранные MMP из HDPack. Pixel/visual equality с production Rebuild64 не заявлена. |
| Статические объекты MOB | NOT IMPLEMENTED. На правой половине пары отсутствуют дома, ограда, деревья и прочая MOB-геометрия. «Тени объектов» на земле местами уже нарисованы в исходном terrain atlas; это не доказательство наличия объектов или динамических теней. |
| Вода | FAIL визуального воспроизведения. Хотя код лаборатории содержит liquid shaders, море/река в полученных кадрах не воспроизводятся корректно. Также `decode failed: sector 69 material 21` и `sector 70 material 21` — два waterfall commands. Не исправлялось заглушкой/плоскостью. |
| 12:40, небо, свет/туман | Только animation clock задан. В лаборатории собственные environment inputs/defaults, нет полного игрового sky/fog/shadow path. Не PASS соответствия игровому времени суток. GL-эталон действительно сообщает `effective_hour=12.6667`. |
| Персонаж | NOT IMPLEMENTED. Нет полного hierarchy/pose/equipment/appearance. |

GL получен собственным MinGW/Qt target из **неизменённых исходников git archive HEAD**. Добавлена отдельная программа `W/gl_capture.cpp`, использующая существующий friend-check и `captureTopdownPass(None)`; исходники архива не редактировались. QSettings, cwd, automation/locks/backups/TEMP заданы внутри W. Мост не создаётся: штатный app main не включён в test entry point. Загрузка MPR/MOB — напрямую, без пользовательских map leases и Save.

У собственной qmake-пробы shell quoting встроенного nav vendor check не работал. Проверка тех же 19 файлов по lock выполнена отдельно родным PowerShell: PASS, source commit `61e3c0af543e234e50ec6459f8ddf8fe42fce96c`. Собственный `.pro` перечисляет те же production sources/includes/resources; оригинальный `ei_maper.pro` не изменён. Сборка `gl-build3.exit=0`.

Размер обоих backend-кадров **1280×720**, vertical FOV 45°, near/far 1/2000, world Z up. GL lookAt и лабораторный world-to-clip получают одинаковые eye/target; разница D3D/GL clip-Z учтена их штатными projection builders. Две камеры:

| Ракурс | Eye | Target | Файл пары |
|---|---|---|---|
| Сверху на ограждённую деревню zone1 / gz1g | (240,110,175) | (240,171,10) | [zone1-village-pair.png](../../work/rebuild64-renderer-probe-20261105/images/zone1-village-pair.png) |
| Вдоль западного берега | (90,22,52) | (90,140,6) | [zone1-coast-pair.png](../../work/rebuild64-renderer-probe-20261105/images/zone1-coast-pair.png) |

Каталог GL подтвердил zoneId `gz1g` и реальные deploy rectangles: вход1 (53.7326,137.882,1.974,1.37688), вход2 (184.035,103.095,6.97035,7), остальные записаны в `gl-camera-village-details.log`. Кадр деревни направлен на ограждённое поселение, не выдаётся за камеру ровно из центра rectangle входа1.

GL exits обоих ракурсов — 0, PNG сохранены. Однако ожидание готовности **всех** unit appearances MOB достигло лимита 45 секунд (`prepared=0`); эта проверка — **TIMEOUT, не PASS**. На кадрах есть видимые объекты/персонажи, но полная готовность всего MOB не доказана. Таймаут не скрыт и не устранён повторением той же проверки. Художественной приёмки владельца нет.

Оригиналы PNG лежат рядом: `zone1-gl-village.png`, `zone1-lab-village.png`, `zone1-gl-coast.png`, `zone1-lab-coast.png`; обзорные `*-survey.png` — вспомогательные. Пары составлены копированием исходных пикселей, подписи вынесены за кадр. Это не сравнение двух полных renderer-картинок.

## Замена terrain snapshot и измерения

Проверен **собственный ограниченный путь shader-lab**, не E1Terrain replacement полного Renderer. Через C ABI caller получает полный height/material payload всех 99 секторов, **1 266 452 байта**, с ABI/размерами и монотонным generation. Неизменяемая исходная топология, layer/material descriptors и mount selection принадлежат сессии. Произвольная смена топологии/атласа этим ABI не поддержана.

MinGW в своём RAM-буфере меняет сектор **(7,5)**: 961 внутреннюю base-вершину поднимает плавным профилем до **+6 м**, края остаются на месте; у одного неанимированного base-тайла меняется atlas index **810 → 805** в том же material page. DLL копирует целое поколение terrain, обновляет normals/conservative bounds, сбрасывает относящиеся derived caches, полностью строит лабораторные CPU/GPU геометрию/материалы и меняет world между завершёнными кадрами. В старые immutable данные запись не производится. Диск/MPR/RES/MOB в этом пути не сохраняются.

Перед измерением выполнена одна полная warm replacement. Затем **50** чередующихся edited/baseline поколений; каждый timing — QPC от начала replace до завершения нового render + синхронного GPU readback. PNG-сохранение вне timing. p95 — nearest rank (48-й из 50), median — среднее 25-го/26-го. Память сравнивается в одинаковом baseline-состоянии до/после серии.

| Показатель | Факт |
|---|---:|
| Полная replacement → готовый кадр, median / p95 | **545.745 / 646.848 мс** |
| Private Bytes до / сразу после 50 | 655 056 896 / 958 197 760 B |
| Immediate рост Private Bytes | **303 140 864 B / 289.098 МиБ** |
| DXGI local usage до / после | 416 256 000 / 515 842 048 B |
| Private Bytes в конечном console sample после ещё 30 кадров | 770 752 512 B; относительно начала **+115 695 616 B / 110.34 МиБ** |
| Неизменный terrain-кадр с readback, 30 кадров: median / p95 | **11.484 / 31.231 мс** |
| 1000 / median frame time | **87.077 FPS**, offscreen с readback, не Present/game FPS |
| Пять основных color/depth/readback targets 1280×720 | **18 432 000 B / 17.578 МиБ**, без textures/geometry/driver allocations |
| Первый cold shader compile | 38.534 с; последующие запуски используют собственный cache |

Машина общая с обычным рабочим сеансом; чужие процессы ради benchmark не останавливались. Это bounded probe, не выделенный performance стенд. Рост памяти наблюдаем, но причиной **не объявлена доказанная утечка**: часть освобождается после дополнительных кадров. Следующий адресный вопрос — retained resources / allocator reserve / deferred destruction и установившийся уровень при замене поколений, а не повторный запуск тех же чисел без гипотезы.

Сырые данные: **`W/logs/benchmark.json`**, **`snapshot-latencies.csv`**, `zone1-lab-village.log`, `pixel-difference.json`. До/после:

- [Полные кадры terrain до/после](../../work/rebuild64-renderer-probe-20261105/images/zone1-terrain-edit-pair.png).
- [Нативный crop изменившейся области](../../work/rebuild64-renderer-probe-20261105/images/zone1-terrain-edit-crop.png).

В исходных PNG изменились 21 739 пикселей, bbox (558,275)–(720,423). Это подтверждает видимое обновление выбранной области, не визуальную корректность всего renderer или всех производных эффектов. Правки не принимались владельцем.

## Зависимости и лицензии

**Готового комплекта полного Renderer нет.** Ни программа, ни ресурсы из этой пробы команде не передавались.

Для ограниченного воспроизведения: `bin/terrain_lab_probe.dll` 1 396 224 B; `bin/mingw_client.exe` 616 038 B; четыре HLSL feature files и `lab_glue.hlsl`. Всего эти 7 файлов — **2 200 742 B (~2.10 МиБ)**. Полные имена/SHA — `W/runtime-manifest.json`. Опциональный shader cache — 836 912 B / 9 файлов; он генерируется из исходников renderer. Runtime читает именно эти feature HLSL, остальные shader strings скомпилированы в DLL. Собственного shader-resource `.res` для лабораторного пути нет.

По PE import table: Windows D3D11/D3DCompiler_47, ole32, bcrypt, Kernel32/UCRT; MSVC `MSVCP140.dll`, `VCRUNTIME140.dll`, `VCRUNTIME140_1.dll`; клиенту также нужен MinGW **libwinpthread-1.dll (52 224 B)**. Qt не нужен DLL/клиенту; он нужен только отдельному GL reference target. Необходимые runtime DLL использованы из установленного toolchain/system, не оформлены в проверенный redistributable package. Нет NGX/DLSS DLL/imports у успешного target. Игровые MPR/MOB/RES/MMP остаются внешними пользовательскими данными и в поставку не включаются. Объём ресурсов/SDK для **полного** Renderer этой пробой не установлен.

Upstream `LICENSE.md` — **PolyForm Noncommercial 1.0.0**, `NOTICE.md` требует сохранить licence/required notices и credit **EID3D11 renderer by Ilufus**, обозначать модифицированные копии. Это не GPL/MIT и не свидетельство произвольного способа распространения объединённого Ataman. Перед передачей команде нужно зафиксировать **письменное разрешение автора** на точные ревизии, изменения, C-ABI-интеграцию и способ передачи; сейчас разрешение описано только со слов владельца, его письменные условия здесь не проверены. Это условие передачи не отменяет данное владельцем разрешение на настоящую локальную пробу.

Изменённая embedded-поставка требует отдельно учесть права на изменения EIRebuild64; разрешение автора upstream автоматически не проверяет права всего игрового слоя. DLSS/NGX/NVIDIA SDK и runtime имеют **отдельные условия**: лицензия renderer на них не распространяется. У полного upstream host также отдельные FidelityFX/NVAPI/DXC dependencies. Даже если они выключены в UI, это не доказательство исключения из сборки/поставки. Полный licensing/distribution review не закрыт локальным compile/link.

## Что требуется от автора и план этапа 2

Сообщений автору/репозиториям не отправлялось. Для продолжения нужен поддерживаемый **x64 game-free target полного core**, который:

1. Линкуется с маленьким собственным host без Game/*, engine structs/hooks и фиктивных ответов. Содержит полные независимые camera/world/environment/resource/scene providers, либо явно документированные callbacks/capabilities для необязательных функций. Не вынуждает хост реализовывать camp/game input ради terrain renderer.
2. Имеет штатный **build без NGX/FG**, не создаёт AppData/registry/config вне явно заданных путей; содержит воспроизводимый shader/resource recipe и notices.
3. Принимает настоящий активный terrain + MOB scene/assemblies и позволяет заменить поколения с явным retirement. После полной замены обновляются colour/depth/shadows/liquids/bounds/cover; ошибки не оставляют старый кадр под новым generation.
4. Имеет измеренный responsive replacement path. Существующий результат лаборатории не даёт оснований обещать интерактивную кисть; если full replacement дорог, обсуждать author-supported sector/material deltas и отмену устаревших работ.

План интеграционного этапа 2 **после этого рубежа**, без текущего разрешения менять код маппера:

| Область | Предполагаемые файлы / результат |
|---|---|
| MSVC DLL и узкий C ABI | Новые `source/render/rebuild64_host.*` / `rebuild64_viewport.*`, отдельный внешний SDK pin/build, `ei_maper.pro` для adapter-only части. MSVC C++ не линкуется непосредственно в MinGW; error/size/version/capability/lifetime остаются явными. |
| QWidget + native HWND / переключатель | `source/app/main_window.ui`, `main_window.h/.cpp`, при необходимости app startup. Отдельный native child HWND, а не DXGI поверх HWND нынешнего QGLWidget; состояния OpenGL/Rebuild64 и ошибки backend видимы. Resize/DPI/reparent/focus/close/device-loss не должны менять документ. |
| Общая камера | `source/render/view.h/.cpp`, `camera.*`, `view_camera_track.cpp`. Передавать одну camera pose/projection с явными handedness/clip-Z/aspect; не переносить управление камерой/вводом в игру. |
| Read-only scene snapshots | `landscape.*`, `view_world_terrain.cpp`, `source/documents/terrain_edit_model.*`, ресурсы `source/resources/resource_profile.*`; единый редакторский document остаётся источником истины. Сначала реальный полный snapshot с revision, затем измеренные обновления. |
| Environment / capture | `render_environment.*`, `view_topdown.cpp`, позже `source/app/ai_agent_read.cpp`/`topdown_export.cpp`: actual selected backend в capture, согласованный time/environment. Не называть GL export изображением Rebuild64. |

OpenGL и все редакторские инструменты сохраняются; document/selection/Undo/Redo/dirty/Save не принадлежат DLL и не перезагружаются при переключении вида. Начальный Rebuild64-view может быть только явно обозначенным просмотром. Picking/ID/depth/brush overlays, custom 16 384-slot atlases, соседние карты и unsaved MOB/hierarchy — отдельные обязательные риски, не автоматически решённые наличием HWND. Native-child stacking и композиция Qt, двойные RAM/VRAM caches, cancellation/generation races и DLL crash/lifecycle также остаются открытыми.

## Локальные материалы и очистка

Код, исходные archive-копии, `.lib`, MSVC/MinGW binaries, shader cache, logs, JSON/CSV и изображения сохранены в **W** для продолжения. Это инженерные материалы, не staging/релиз и не установленная сборка. Перечень очистки — **`W/cleanup-plan.json`**, inventory SHA — `cleanup-cache-inventory.json`. Raw игровых MPR/MOB/RES в W не копировали.

Автоматическая проверка инструмента отклонила рекурсивное удаление собственного производного GL cache с причиной **`blocked by policy`**; весь отклонённый вызов не выполнялся. Кэш **оставлен**, статус плана `blocked_by_automatic_tool_review`: `W/gl-build/release/cache`, **269 файлов / 34 713 877 байт**. Обход отказа не выполнялся. Source/binaries/images/logs сохранены; постоянная установка и соседние work-задачи не затрагивались.

Итоговый readback `W/verification.json`: **943** файла исходников/tests/VERSION/ei_maper.pro GL archive совпадают с архивом HEAD; в upstream archive-копии изменены только `Prototypes/shader-lab/lab_world.h/.cpp`, в embedded archive-копии — только `OwnedRenderer/Renderer.props` плюс собственные добавленные диагностические headers. Хеши сохранённых бинарников подтверждены. Собственных probe-процессов после завершения нет; `subst` пуст.
