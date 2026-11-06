# EI-REBUILD64-RENDERER-PROBE — этап 1, 2026-11-05

> Обновление 2026-11-06: настоящий owned::Renderer запущен; актуальный результат, цифры и ограничения — в разделе «Этап 1Б». Ранние выводы ниже относятся к этапу 1 от 2026-11-05.

## Результат и решение

**Частичный технический успех: общий upstream-декодер и шейдеры настоящего рельефа работают вне игры на аппаратном D3D11 через MSVC DLL, вызываемую MinGW 7.3. Полный production `owned::Renderer` вне игры не запущен.** Это два разных результата, их нельзя объединять в PASS полного Rebuild64.

- У embedded-поставки удалось собрать `OwnedRenderer.lib`, преодолев диагностические заголовки этапа 0. Минимальная DLL с настоящим `Renderer` не линкуется: после `/Gy`, `/OPT:REF` и подключения независимых адаптеров остаются **68 внешних символов**, включая поставщиков из игрового слоя. На этом пути применён стоп-критерий владельца; игровые классы, их заголовки и функциональные заглушки не переносились.
- Отдельный upstream-target **shader-lab** действительно предоставляет независимый путь настоящего рельефа. Для него сделан ограниченный собственный DLL-хост. Это лабораторный потребитель общих декодера/шейдеров, **не тот же полный Renderer, что в EIRebuild64**. Он обходит его scene/presentation/cache orchestration.
- На DejaVu `zone1` получены текстурированный рельеф, два парных ракурса с GL и изменение рельефа в памяти. **MOB-объекты и персонаж не реализованы; речная/морская вода визуально не воспроизводится корректно; игровое освещение/небо/туман на 12:40 не подтверждены.** Частичные кадры подписаны соответственно.
- В лабораторном пути полная смена поколения массивов высот/материалов до готового кадра: **median 545.745 мс, p95 646.848 мс, 50 замен**. Immediate Private Bytes выросли на **289.098 МиБ**; после ещё 30 кадров остаточный рост около **110.34 МиБ**. Такой путь пока непригоден для обновления на каждом движении кисти.

**Продолжать техническое выделение полноценного game-free renderer target — да. Начинать встраивание этой лаборатории как готового вида Rebuild64 — нет.** Перед UI нужен успешно слинкованный и запущенный полный backend с реальными независимыми поставщиками мира и контролируемым освобождением поколений. Этап 1 завершён с частичным результатом и открытым препятствием, а не с приёмкой полного renderer.

## Полномочия, идентичность и сохранность

Применено поручение владельца от 2026-11-05: переключатель OpenGL / Rebuild64 в будущем; текущий OpenGL и инструменты сохраняются. **Актуализация этапа 1Б, 2026-11-06:** разрешение лично подтвердил автор Илья Руднев (Ilufus), автор renderer и основы Endurance; скриншот подтверждения находится у директора. Вопрос разрешения для этой работы закрыт. Публикации/поставки этой пробы нет.

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

Upstream `LICENSE.md` — **PolyForm Noncommercial 1.0.0**, `NOTICE.md` требует сохранить licence/required notices и credit **EID3D11 renderer by Ilufus**, обозначать модифицированные копии. **Разрешение автора Ильи Руднева (Ilufus) лично подтверждено; скриншот у директора, вопрос разрешения закрыт решением этапа 1Б от 2026-11-06.** Прежнее требование повторно получать подтверждение к этой работе не применяется. Публичный текст лицензии и notices сохраняются; он не переименовывается в GPL/MIT.

Игровые классы и приватные заголовки по границам задания не переносятся. DLSS/NGX/NVIDIA SDK и runtime имеют **отдельные условия**: разрешение на renderer их не заменяет. У полного upstream host также отдельные FidelityFX/NVAPI/DXC dependencies. Даже если они выключены в UI, это не доказательство исключения из сборки/поставки. Для технической пробы это не повод повторно открывать уже закрытый вопрос разрешения автора.

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

## Этап 1Б — настоящий owned::Renderer, 2026-11-06

**Вне игры работает настоящий `owned::Renderer`: MSVC x64 DLL успешно вызывается через C ABI из MinGW 7.3.** Получены zone1 DejaVu с настоящими материалами, водой/водопадами, небом, атмосферным туманом, светом на 12:40, тенями и статическими MOB-объектами. Штатный Terrain 2.0 тоже работает после подготовки его профиля. Это уже полный renderer orchestration, а не shader-lab. Кадры остаются **частичными по составу мира**: персонаж не реализован; один объект `stst8`, NID 742, отклонён валидатором.

**Продолжать — да: можно начинать отдельный, первоначально неизменяемый вид QWidget/native HWND и переключатель OpenGL / Rebuild64.** Пригодность к непрерывной кисти пока не подтверждена: полное поколение до проверенного кадра — **median 206.562 мс / p95 327.643 мс, 50 замен**. Нужен частичный путь и явный контракт готовности поколения.

### Границы, база и восстановление после прерывания

Сверены AGENTS SHA-256 `2A2F58EF34F63D6C5D67D165D0B809B046D0A519181119D22EA8FDD8781FE3B7`, отчёт этапа 0 и существующий W/stage1b. При возобновлении HEAD маппера — `744e720e73a0a51b2638bc906a3c94d04e31d4b4` (280). Код маппера, версия, ROADMAP и чужие изменения не редактировались; состояние этой пробы хранится в отчёте и W/stage1b. Git получит только этот файл.

Успех старого пустого кадра был проверен по exit/log/PNG. Прерванный shader producer не оставил завершённой таблицы `.bin/.res`; она **не объявляется собранной**. Окончательный DLL повторно проверен: `readiness-build.exit=0`, `material-client-build.exit=0`, `final-material-generation.exit=0`; initialize → пустой кадр → PNG → load_map(MPR,MOB) → обе камеры → замены → destroy. После каждого кадра мира идёт штатный `present()` в скрытый HWND, включая обслуживание ресурсов.

База полного renderer — archive-копия **EIRebuild64 `origin/main` `81dbcdd29b41baeb346f167c6dfb511dff05f295`**, включая её `external/owned-renderer` и публичные E1-адаптеры. В ней уже есть типизированные E1World/E1Terrain/E1Scene, completed surfaces и retained scene. Upstream main `d17a6149e5b5d4c2d23873d18e037bc270269050` остаётся эталоном сравнения/дальнейшей синхронизации; prototype `00461a736e1a5768d8800ba6555c5aff3b0eb9` не выбран, поскольку его маленькая синтетическая сцена не заменяет мир zone1. Checkout владельца и read-only upstream не изменялись. Старый compile-label `eirebuild64-X64RUPD02-b1ffc736` в props не является SHA использованных исходников.

Личное подтверждение разрешения автором Ильёй Рудневым (Ilufus) принято, скриншот находится у директора. **Вопрос разрешения закрыт.** LICENSE.md/PolyForm Noncommercial и авторство сохраняются. Разрешение автора renderer не заменяет условия NVIDIA/NGX/DLSS. Публикации/поставки этой пробы нет.

### 68 символов и независимые поставщики хоста

Ниже все **68 уникальных неразрешённых символов** из исходного link3. Полные demangled signatures и исходные linker-строки сохранены в `W/stage1b/symbols68.json` и `W/logs/host-link3-gy.log`. В таблице для читаемости приведены имена функций. Все они разрешены в итоговой DLL.

| Символ | Поставщик в EIRebuild64 | Назначение для кадра | Решение в хосте |
|---|---|---|---|
| `E1CompleteVisualFrame` | `E1CompletedVisuals.cpp:123` | Завершённые immutable appearance/pose/visibility факты мира перед depth/colour. | наш host provider; независимые опубликованные render facts |
| `bridge_critter_obstacles` | `E1AmbientEffects.cpp` | Препятствия для renderer-owned critters из retained entities. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_critter_perches` | `E1AmbientEffects.cpp` | Опоры/деревья для critters из retained scene и текстур дерева. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_ground_cover_footprints` | `E1AmbientEffects.cpp` | Опубликованные следы в snow/ground-cover; без симуляции они отсутствуют. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_ground_cover_objects` | `E1AmbientEffects.cpp` | Contact/cover stamps статических объектов. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_ground_cover_units` | `E1AmbientEffects.cpp` | Footprint/trail/detail факты units для ground cover. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_liquid_ripples` | `E1AmbientEffects.cpp` | Возмущения воды от опубликованных движения/габаритов; без движения wake отсутствует. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_live_wants_capture` | `Game/LiveObserver.cpp:388–389` | Запрос retail LiveObserver capture; наш capture принадлежит C ABI. | retail capture выключен; собственный readback C ABI |
| `bridge_local_single_player_mission` | `E2LoadReveal.cpp:15` | Граница load-reveal: наличие игровой local mission. | независимый E2LoadReveal; local mission не объявлена |
| `bridge_outer_frame_timing_matches` | `E2Diagnostics.cpp:96` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_outer_frame_timing_supported` | `E2Diagnostics.cpp:93` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_take_outer_frame_timing` | `E2Diagnostics.cpp:95` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `camp_inventory_clear` | `E4Camp.cpp:218` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_input` | `E4Camp.cpp:206` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_preview_specular_rgb` | `E4Camp.cpp:191` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_take_decoration` | `E4Camp.cpp:158` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_wheel` | `E4Camp.cpp:197` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `game_focus_swallow_activation` | `E3Input.cpp:98` | Игровой focus/input/camp dispatch; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `game_hud_editing` | `E3Input.cpp:90` | Игровой HUD/UI ownership и frame sink; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `game_hud_editor_available` | `E3Input.cpp:89` | Игровой HUD/UI ownership и frame sink; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `harness_enabled` | `E2Diagnostics.cpp:104` | Retail harness request/timing/completion; не управляет нашим кадром. | диагностика: независимые E2Diagnostics/E1Notices |
| `harness_on_travel_map` | `E2Diagnostics.cpp:110` | Retail harness request/timing/completion; не управляет нашим кадром. | диагностика: независимые E2Diagnostics/E1Notices |
| `harness_timed_out` | `E2Diagnostics.cpp:68` | Retail harness request/timing/completion; не управляет нашим кадром. | диагностика: независимые E2Diagnostics/E1Notices |
| `widescreen_interface_owned` | `E3Input.cpp:88` | Игровой HUD/UI ownership и frame sink; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_notice` | `E4Camp.cpp:182` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `figure_fallback_reason_name` | `E2Diagnostics.cpp:60` | Читаемое имя причины отказа geometry. | диагностика: независимые E2Diagnostics/E1Notices |
| `harness_directory` | `E2Diagnostics.cpp:105` | Retail harness request/timing/completion; не управляет нашим кадром. | диагностика: независимые E2Diagnostics/E1Notices |
| `camp_inventory_blit` | `E4Camp.cpp:215` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `bridge_latest_profile` | `E2Diagnostics.cpp:87` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `camp_inventory_take_scroll_cost` | `E4Camp.cpp:156` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `harness_present_step` | `E2Diagnostics.cpp:107` | Retail harness request/timing/completion; не управляет нашим кадром. | диагностика: независимые E2Diagnostics/E1Notices |
| `res_cache_release_snapshot` | `E2Diagnostics.cpp:113` | Диагностика addon cache hook; hook не установлен. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_scene_renderer_timing_begin` | `E2Diagnostics.cpp:97` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `current_semantic_context` | `E2Host.cpp:57` | Классификация текущего host pass для учёта; game caller RVA отсутствует. | наш provider: frame context, без game RVA |
| `bridge_shadow_reuse_diagnostics` | `E2Diagnostics.cpp:101` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `embedded_frame_scope_begin` | `E2Diagnostics.cpp:49` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_append_probe_event` | `E2Diagnostics.cpp:78` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_ground_cover_corpses` | `E1AmbientEffects.cpp` | Contact stamps тел из опубликованных render facts. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_live_capture` | `Game/LiveObserver.cpp:388–389` | Retail capture swapchain; не используется для снимков нашего хоста. | retail capture выключен; собственный readback C ABI |
| `bridge_neighbour_diagnostic` | `E1Notices.cpp:18` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_post_engine_surface_material_group_scan` | `E2Diagnostics.cpp:90` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_release_ground_contacts` | `E1AmbientEffects.cpp` | Освобождение contact/perch/footprint caches при retirement. | типизированный E1AmbientEffects; retained scene/MPR |
| `bridge_report_event` | `E2Diagnostics.cpp:71` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_request_hovered_shadow_capture` | `E2Diagnostics.cpp:89` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_scene_renderer_timing_end` | `E2Diagnostics.cpp:100` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_set_camp_input_overlay` | `E2Input.cpp:16` | Игровой focus/input/camp dispatch; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `bridge_set_load_reveal` | `E2LoadReveal.cpp:20` | Запрос загрузочного reveal; игровая пауза в самостоятельном хосте не вызывается. | независимый E2LoadReveal; local mission не объявлена |
| `bridge_set_native_terrain_animation_owned` | `E2TerrainAnimation.cpp:8` | Признак renderer-owned terrain animation. | независимый E2TerrainAnimation |
| `bridge_set_outer_frame_timing_active` | `E2Diagnostics.cpp:94` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `bridge_set_profile_enabled` | `E2Diagnostics.cpp:88` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика / retail command выключена |
| `bridge_trace_input_focus` | `E2Diagnostics.cpp:82` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `camp_inventory_art_failed` | `E4Camp.cpp:162` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_cancel_drag` | `E4Camp.cpp:195` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_output` | `E4Camp.cpp:173` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_popup_failed` | `E4Camp.cpp:163` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_popup_published` | `E4Camp.cpp:164` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_retire_textures` | `E4Camp.cpp:157` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `camp_inventory_set_wide` | `E4Camp.cpp:181` | Camp UI/preview/layout/scroll state; в standalone редакторе отсутствует. | выключено в host policy; E4Camp/E3Input исключены |
| `embedded_frame_scope_end` | `E2Diagnostics.cpp:52` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `embedded_perf06_report` | `E2Diagnostics.cpp:56` | Диагностическое время/счётчики; не поставщик геометрии. | диагностика: независимые E2Diagnostics/E1Notices |
| `embedded_report_render_failure` | `E2Diagnostics.cpp:21` | Диагностика/trace/capture-request renderer; не игровой источник состояния. | диагностика: независимые E2Diagnostics/E1Notices |
| `harness_complete` | `E2Diagnostics.cpp:108` | Retail harness request/timing/completion; не управляет нашим кадром. | диагностика: независимые E2Diagnostics/E1Notices |
| `request_debug_lightning` | `E2Diagnostics.cpp:111` | Retail debug-команда молнии; не является обязательным world input. | диагностика / retail command выключена |
| `set_background_mode` | `E2Host.cpp:63` | Политика игровой паузы в фоне; хост сам задаёт кадры и время. | игровая пауза выключена; clock принадлежит host |
| `set_game_effect_distance` | `E2EffectDistance.cpp:10` | Ограничение дальности визуальных эффектов. | независимый E2EffectDistance |
| `set_game_hud_editing` | `E3Input.cpp:91` | Игровой HUD/UI ownership и frame sink; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `widescreen_clear_hud_frame_sink` | `E3Input.cpp:107` | Игровой HUD/UI ownership и frame sink; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |
| `widescreen_set_hud_frame_sink` | `E3Input.cpp:104` | Игровой HUD/UI ownership и frame sink; отсутствует в редакторе. | выключено в host policy; E4Camp/E3Input исключены |


E1CompleteVisualFrame получает готовые immutable assemblies/poses из нашего retained-поставщика; игровых указателей в нём нет. Отключены camp inventory/preview, game HUD/portraits, game focus/input, retail capture и игровая фоновая пауза. E4Camp/E3Input и приватные классы не подключались; необходимые entry points возвращают явное отсутствие функции/невыполненный blit либо ничего не публикуют. Это политика редакторского хоста, а не эмуляция игры. Нет UI-кадра, который выдавался бы за игровой.

Дополнительно реализован существующий типизированный `scene_core::Config::run`: хост передаёт свои renderer options в памяти. `ensure_host` сам не читает ini — полагаться только на файл настроек было ошибкой адаптера. Собственный ini находится в W/stage1b/profiles; настройки владельца не использовались.

Диагностические заменители только в нашей копии: `embedded/Game/CorePerf.h` (пустые scopes/counters и диагностический frameNumber) и `embedded/Game/GameJournal.h` (Event в stderr собственного клиента, Count no-op). Это не игровые классы. GameJournal/CorePerf не являются источником камеры, времени, геометрии или world epoch.

Минимальные reviewable изменения поставки:

- `W/patches/001-typed-terrain-snapshot.patch` + `001-rationale.md`: E1Terrain получает готовый проверенный TerrainCpuAsset, новое поколение и существующий prepared-resource путь. Алгоритмы renderer не заменены. Объявление ingress находится в собственном host adapter.
- `W/patches/002-function-level-linking.patch` + rationale: build-only `/Gy` для `/OPT:REF`, чтобы не подтягивать неиспользуемые game hook installers. Изменение возникло на этапе 1.

Остальная реализация — собственные `W/stage1b/host.cpp`, `terrain.inc`, `snapshots.inc`, `host_providers.cpp`, `api.h`, `client.cpp`, `W/probe1b.vcxproj`. В Source renderer алгоритмических правок нет. Ничего из этого не добавляется в Git маппера.

### Сборка, C ABI и скрытый GPU-хост

MSVC v143/VS2022 BuildTools, x64, `/MD`, C++17; MinGW GCC **7.3.0** из того же Qt kit, что маппер. Команды сохранены в `W/stage1b/build-probe1b.ps1`; сборка использует `C:/GOGGAM~1/maped/EI-MAP~2`, без subst/junction. DLSS SDK использован только read-only для обязательных headers/static library существующего target; штатной no-NGX сборочной опции в этой базе нет. Большой переделки backend ради удаления SDK не делалось.

Запуск: `run.py final-material-generation map village bench`, CREATE_NO_WINDOW, TEMP/TMP и EI_RENDERER_DEBUG_DIR в W. D3D11 HARDWARE, **NVIDIA GeForce RTX 4070 SUPER**, FL 11.0. WARP не нужен. Используется `adopt_host_device` → `ensure_host`; `NgxBackend::initialize` не вызывается, NGX/SL/DLSS runtime DLL не требуются этому запуску. Обычный `ensure()` с NGX и AppData-путём не использовался.

ABI v2: `probe1b_version/create/load_map/set_camera/set_time_of_day/frame/capture_png/get_terrain_snapshot/replace_terrain_snapshot/get_statistics/destroy`; дополнительные probe-only `frame_status`, `set_features`, `load_objects`, `retire_gpu_world`, `debug_cpu`. `load_map` загружает оба MPR и MOB; features управляют последовательностью показа. `sizeof(create_info)=80` подтверждён обеими сторонами. Проверялся один create/destroy на процесс с одной активной сессией; многократная смена документов/recreate внутри одной DLL ещё не принята. Буферы принадлежат вызывающему коду, handle освобождает DLL; Qt/STL/исключения и C++ allocations через границу не передаются. Ошибки — HRESULT и фиксированный error record, для snapshot также 20–23; `E_PENDING` отделяет незавершённую подготовку.

Окно WS_POPUP (-32000,-32000), WS_EX_NOACTIVATE/TOOLWINDOW, без Show/активации; в последнем логе `window_visible=0`. `present()` нужен и для offscreen-потребителя: он запускает tile-family preparation, trim и bookkeeping. Строка renderer `shader_preparation ... window=shown` означает progress draw/Present в уже предоставленный **скрытый** swapchain; соответствующий код не вызывает ShowWindow. Отдельных видимых окон, computer-use, desktop screenshots, игры, EIRebuild и установленного маппера не было.

В итоговой DLL нет shader-lab object files/functions. Только плоский snapshot wire format первоначально переиспользовал ту же схему; теперь определения находятся в stage1b/api.h с собственными именами.

### Настоящие данные zone1 и полнота кадра

PZ читался напрямую, без записи/ремонта. Явные loose roots Maps/Res DejaVu и Base; упорядоченные figure, redress и texture chains из профиля, включая HDPack_Terrain.res, с archive_kinds. Те же mounts переданы resource runtime через `owned_resource_runtime_adopt_mounts`, без обнаружения настроек игры. Необходимые redress archives устранили 9 первоначальных missing-texture ошибок.

MPR: 11×9 секторов, 352×288 м, 19 основных atlas pages, два отсутствующих необязательных long-atlas слота сохраняются отсутствующими. Никакой подменной текстуры не создано. Реальная выбранная цепочка даёт смесь страниц 1024/2048; размер из заголовка MPR не подменяет размер загруженного MMP.

MOB decoder дал **1809** visual/static/lever записей: families 53 — 1106, 54 — 625, 55 — 46, 60 — 32. Загружены **106 моделей, 74 текстуры, asset failures 0**. Собственная render-only политика пропускает типизированные исходные позы через общий `prepare_retained_static_candidate`, не ограничиваясь игровым static-admission gate 53/55. Семейства/записи не переименовываются, скрипты не запускаются. Опубликованы **1808** настоящих rigid assemblies; NID **742 / stst8 / family 54 / node 4** отклонён с `figure_structure`. Проверка не обходилась и заглушка не рисовалась.

| Часть | Проверенный результат |
|---|---|
| Рельеф/материалы | MPR geometry, реальные MMP и renderer materials; Terrain 2.0 после штатной подготовки профиля |
| Вода/водопады | Успешный transparent pass в обеих камерах; видны море/реки и водопады, включая procedural-fall геометрию из MPR |
| Небо/туман/свет | WorldEnvironment 12:40, Gipat, ясная погода; RGB из LightsGipat.ini; направление солнца в том же публичном часовом соглашении, что GL |
| Тени | Включены sun/far/point-shadow settings; работает полный E1RunOwnedVisualFrame и его depth/colour путь; дневные тени видны на итоговых кадрах |
| Дома/ограда/деревья | Настоящие FIG/BON/MMP assemblies и исходные MOB poses; оба ракурса |
| Персонаж | Не реализован: текущий loader выделяет static/lever источники, отдельный unit appearance/animation producer не написан |

Поза объектов, terrain layer shifts и tile animation phases зафиксированы. Часы окружения задаются хостом; игра/симуляция/NPC AI не запускаются. Нет игровой HUD, соседних карт, точек входа как editor overlays и полной реконструкции игрового состояния. Material classes JSON и Terrain 2.0 mask resources не упакованы: renderer честно сообщает штатные defaults/bilinear fallback. Это не приёмка полного визуального совпадения с установленным EIRebuild64.

Семейства Terrain 2.0 подготовлены из реального `ei_atlas_profile_zone1_9f571c20fa04_c444762f8d84493c.json`: в логе ready 25344/25344 transition tiles, 21 plain materials, 2 liquid-bed families; 4816 profile corners, 48 unassigned. Промежуточный пурпурный marker был следствием отсутствующего в раннем адаптере Present/подготовки данных, а не доказательством неисправности теней. Итоговые опубликованные пары не содержат этого маркера.

### Парные изображения и честная готовность

Во время продолжения обнаружено изменение zone1.mob в **2026-11-06 01:02:02**, после старых GL captures. Поэтому только GL-снимки пересняты тем же изолированным capture target из archive `761f52765835aaf1922d8a9ce24e7f1270655dc3` (279); код/сборка владельца не запускались. 280 относится к save integrity и не использован вместо графического эталона. Оба новых GL процесса exit 0, 2012 узлов, effective_hour 12.6667. **Полная готовность всех unit appearances в GL снова TIMEOUT 45 s (`prepared=0`)**, PNG сохранены; это не PASS полного GL мира.

Одинаковые 1280×720, VFOV 45°, near/far 1/2000, Z-up:

- Деревня: eye (240,110,175), target (240,171,10).
- Вдоль берега: eye (90,22,52), target (90,140,6).

Пары (слева GL, справа настоящий renderer Terrain 2.0), все пути относительно W:

- `stage1b/images/zone1-pair-village-1b.png`
- `stage1b/images/zone1-pair-coast-1b.png`
- Исходные renderer PNG: `stage1b/images/zone1-owned-stage4-village.png`, `...-coast.png`.
- Этапы показа: `zone1-owned-stage0..2-{village,coast}.png` — terrain → water → environment; `stage4` — итог с MOB и Terrain 2.0. Вариант stage3/terrain_blend=0 для берега не принят (см. ниже); старый PNG с marker перенесён в checkpoints как stale/rejected.
- `stage1b/images/empty-owned-renderer-final.png` — самый ранний успешно снятый пустой кадр текущего ABI.

Рендер может завершить GPU readback до готовности производных material tables. В этой пробе MinGW дополнительно вызывает `frame_status` и повторяет кадр при **реальном shader diagnostic marker** (без изменения пикселей, лимит 30 s). За итоговый прогон было 27 успешных ожиданий / 52 дополнительных кадров. Это проверка конкретной zone1, **не универсальный production readiness API**; нужен явный готовый generation token. Методика предотвращает выдачу переходного пурпурного кадра за полный. QA, размеры, SHA и pixel counts — `image-verification.json`.

В предыдущем исследовательском прогоне `final-ready-generation` было отдельное **FAIL/TIMEOUT**, не скрытое exit 0 общего исследовательского клиента: stage3/берег с `terrain_blend=0` не стал пригоден к показу за 30 s (413349 marker pixels, 1872 retries, E_PENDING). Клиент продолжил независимый штатный вариант Terrain 2.0 (`terrain_blend=1`): обе его камеры приняты, marker pixels **0**, и все 50 измеренных замен дошли до принятого кадра. Готовый вид и замеры относятся именно к нему. Отключение blending в этой комбинации E1/host inputs пока не подтверждено; это не объявляется исправленным. Старый stage3-coast файл не переиспользован как новый успешный результат. В окончательном `final-material-generation` этот уже известный нерабочий вспомогательный вариант пропущен; весь итоговый кадр и 50 замен используют Terrain 2.0.

### Полная замена поколения и память

`get_terrain_snapshot` вернул **1 266 452 B** в caller-owned память. На секторе **(7,5)** изменены **961** внутренних вершины, плавный подъём с пиком **+6 м**; один tile frame/material reference **810 → 788** в той же atlas page: **Grass / Common → Grass / Dark**. Разные семьи подтверждены CPU-декодером и тем же atlas profile (`material-audit.log`): IDs 2142727678 → 1974896831. После первой замены обратное чтение всего снимка через C ABI совпало побайтно (`roundtrip_snapshot=exact`). Ранний вариант 810→805 оказался той же семьёй Grass/Common и не использован как итог проверки материала. Меняется настоящее поле tile_index; переназначение page/topology этой C-схемой пока не разрешено. Каждый новый снимок имеет большее поколение, целиком копируется/валидируется и передаётся E1 ingress. MPR/MOB/RES на диске не сохранялись.

Замер: один warm replacement, затем 50 чередований baseline/edited; QPC от вызова replace до **проверенного нового кадра**, включая D3D11 readback, readiness retries и Present/bookkeeping. Включены полный renderer, Terrain 2.0, вода, окружение, тени и MOB. Это уже не лабораторные 546 мс.

| Метрика | Итог |
|---|---:|
| Полная замена: median / p95 (nearest-rank), N=50 | **206.562 / 327.643 мс** |
| Private Bytes перед / после 50 | 1501.215 / 1511.863 МиБ |
| Рост Private Bytes после 50 | **+10.648 МиБ** |
| После ≥1880 дополнительных кадров и ≥30 s | 1440.453 МиБ, к исходному **-60.762 МиБ** |
| После явного E1 world retirement и renderer frame begin | 773.625 МиБ |
| DXGI local VRAM перед / после 50 / после ожидания | 1023.344 / 1029.969 / 985.574 МиБ |
| 30 обычных кадров: median / p95 | 13.191 / 53.382 мс |
| Синхронный capture/readiness/Present FPS по median | **75.81** |

Output BGRA8/RGBA8 1280×720 — **3.516 МиБ**; CPU pixel buffer и временный staging readback имеют такой же объём каждый. В таблице DXGI приведена память всей сцены/кэшей, а не только output. Поле `targetsBytes=0` в сырой статистике не реализовано и **не означает нулевую память кадра**. FPS включает диагностический readback и ожидания; это не обещание FPS видимого QWidget. Сырые данные: `snapshot-latencies.csv`, `benchmark.json`, `retirement.json`, каноническая сводка `measurements-final.json`, `final-audit.json`.

До/после: `stage1b/images/terrain-edit-pair-1b.png` и `terrain-edit-crop-1b.png`; оригиналы `zone1-owned-before.png`, `zone1-owned-edit-after.png`. Static MOB objects сохраняют опубликованные исходные позы; автоматическая перестановка объектов на изменённую землю не проверялась.

**Разбор старого роста памяти.** Ранний полный хост без Present удерживал сотни МиБ, даже после 1880 кадров; его цифры сохранены как `before-present-*` и не являются итогом. Пропущен был штатный trim/preparation path. После исправления финальные числа выше показывают освобождение поколений при обычном обслуживании; необратимая утечка из прежних +289/+110 МиБ не следует.

Отдельно повторена именно старая shader-lab DLL на **100** заменах: после 50 Private Bytes **860.496 МиБ**, после 100 **965.648 МиБ**; ещё 150 кадров/30 s — **824.273 МиБ**; после destroy **270.563 МиБ**, после FreeLibrary/2 s **15.145 МиБ** против **1.094 МиБ** до LoadLibrary. Освобождение основной памяти согласуется с владением ресурсами/кэшами DLL/драйвера и отложенным освобождением; безвозвратная утечка процесса не доказана. Остаток не нулевой; неограниченный soak/heap audit этим не заменён. Лог `lab-memory100.log`, 100 строк `lab-memory-latencies.csv`; эта лаборатория использовалась **только для диагностики старых чисел**, не для финальных картинок/таймингов полного renderer.

### Частичное обновление и вопросы Илье

В текущем E1 seam произвольное изменение base height/UV/material требует новой immutable TerrainCpuAsset. Уже есть отдельные `E1PublishTerrainSectors` (phases/layer_z), layer shifts в OwnedTerrainGeometryValues и GPU prepared parts с ключами sector/material/subpart/LOD. Это полезные механизмы, но **готового editor API для произвольного base-sector/atlas-page update не подтверждено**. Сдвиг слоя воды не является заменой кисти базовой земли.

Для 30–60 мс нужен per-sector revision/COW и сохранение неизменённых prepared handles: пересчитать dirty sector + halo нормалей/стыков, обновить его GPU ranges и tile/class/height pages, адресно инвалидировать Terrain 2.0/тени/воду. Не перечитывать архивы, не пересоздавать все atlases, объединять частые mouse moves; держать прежний согласованный кадр до готовности нового. Скорость такого пути ещё не измерена. Полную публикацию на каждое движение кисти начинать рано.

Точечные вопросы автору (ответа не ожидали, доступная работа доведена до результата):

1. **E1Terrain.cpp / E1PublishTerrainSource и patch 001:** можно ли принять типизированный completed CPU snapshot ingress и какой контракт sector/material revision нужен для частичной замены вместо целого TerrainCpuAsset? Какие normal/LOD/liquid dependency halos обязательны?
2. **owned_terrain_front_end.h::OwnedTerrainPreparedResources, renderer_native_queue.cpp::trim_native_mesh_cache/retire_native_source_values:** какой поддерживаемый per-generation/sector retirement путь сохраняет статические объекты и shaders? Нужен ли отдельный offscreen end-frame/housekeeping вместо обязательного Present скрытого swapchain? После правильного Present финальный рост памяти не остаётся прежним сотням МиБ.
3. **renderer_frame.cpp::present_frame, prestart_terrain_tile_families, renderer_terrain.cpp/renderer_terrain2_cache.cpp:** как получить явный токен «все производные данные данного поколения пригодны к показу»? RGB marker polling — только приём пробы; QWidget не должен показывать промежуточные marker frames.
4. **retained_static_submission.cpp::prepare_retained_static_candidate:** почему `stst8`, NID 742/family 54/node 4, нарушает figure_structure? Как штатно описать этот узел (включая возможный недrawable/service node), не ослабляя валидатор и не теряя целый объект?
5. **owned_asset_loader.cpp::parse_mob_document, E1Scene/E1FigureSources:** какой открытый typed producer рекомендуется для одного unit из MOB: appearance/body parts/material composition + idle ANM/ADB/completed pose? Игровые классы и симуляция в редактор не переносятся.
6. **OwnedRenderer.vcxproj/DlssSdk.props, scene_core::Config, Tools/shader_embed.cpp:** нужен поддерживаемый standalone MSVC target с optional NGX/SL и конечной shader-table `.res`, documented Config/mounts/end-frame contract. Сейчас game-free запуск уже доказан, но build SDK и длительный runtime FXC остаются неудобными.
7. **renderer_terrain.cpp::apply_terrain_tile_blend/update_native_terrain_constants, scene_core::Config:** почему `terrain_blend=0` в береговом E1-кадре оставляет marker, тогда как `terrain_blend=1` и подготовленный профиль работают? Каков поддерживаемый контракт выключения и инвалидирования уже начатых Terrain 2.0 jobs/tables? Гипотеза — несогласованное включение rows/table при смене option; причина ещё не установлена.

Для доказанного кадра автору **не требуется возвращать game.exe, хуки или приватные классы**. Его изменения/согласование API желательны для production упаковки, readiness и быстрого редактирования; разрешение на код не является открытым вопросом.

### Поставка, воспроизведение и сохранность

Проверенный DLL **14,715,392 B** (SHA-256 `23665c987767da753587983b30cc1d69d423b867daeaa061604854953e30b1c4`), MinGW client **605,225 B** (`736bd57dfbe6f1285387b33ec15b4b1712acb4b268242197ce7601a15f47a578`). Вместе **14.611 МиБ**, плюс MinGW libwinpthread-1.dll 52 224 B, MSVC 14 runtime и штатные Windows D3D11/D3DCompiler_47/WIC/COM библиотеки. NGX/DLSS runtime imports в этом пути отсутствуют; NVIDIA SDK статически использован при линковке по read-only пути и требует отдельного учёта условий при будущей поставке.

Отдельных HLSL-файлов shader-lab этот target не читает: shader sources уже в DLL, при отсутствии встроенной таблицы компилируются штатно. В последнем запуске **272/272 shader programs, failed 0, 108241 мс** подготовки; это заметная startup-задержка. Прерванную `.bin/.res` нельзя включать в пакет. Нужна завершённая штатная таблица для следующего этапа. Ресурсы PZ/MPR/MOB/RES остаются внешними, не копируются в пакет. Дополнительные renderer material/mask assets и их exact deployment layout надо закрепить при упаковке; текущие fallbacks названы выше.

`W/cleanup-plan.json` обновлён. Сырых рабочих копий MPR/MOB/RES не создавалось. Источники и бинарники полного хоста, patches, конфигурация, таблица символов, логи и PNG оставлены в W. Старый производный GL cache остаётся: предыдущая автоматическая проверка удаления ответила «blocked by policy»; обхода или повторной попытки удаления другим способом не было. Ни PZ, ни installation `artifacts/EI-Mapper-Ataman-2-first-test`, ни AppData/ProgramData/registry владельца не записывались. Аккаунты/Git config не менялись; push отсутствует; subst в конце пуст.

### План этапа 2: окно и переключатель

Начать можно с **отдельного read-only Rebuild64 вида**: один собственный QWidget с native HWND, DLL load/unload/resize/device-loss, копия камеры и согласованный snapshot карты. OpenGL продолжает обслуживать существующие инструменты; editable mode требует отдельного рубежа частичного обновления, готовности и проверки Undo/Redo. Эта проба сама не изменяет код маппера и не занимает номер.

Предполагаемые файлы следующего этапа (в этом задании не менялись):

- Новые `source/render/rebuild64_viewport_widget.{h,cpp}`, `rebuild64_backend_client.{h,cpp}`, flat ABI header/renderer-backend contract.
- `source/app/main_window.{h,cpp,ui}`, `main_window_world.cpp`: действие OpenGL / Rebuild64, lifetime активного документа и native child.
- `source/render/view.{h,cpp}`, `camera.{h,cpp}`, `landscape.{h,cpp}`: камера и immutable terrain generations; GL overlays/picking сохраняют текущего владельца.
- `source/resources/resourcemanager.*`, `source/mob/mob.*`: точный snapshot mounts, статических transforms/appearances, source identities; Save-путь 280 не используется как канал обновления renderer.
- `source/app/settings*`, `options.*`, `ei_maper.pro`: renderer preference и сборочная проводка; meaningful ABI/lifetime/generation checks при реализации.

Существенные риски: Qt/MinGW ↔ MSVC lifetime и threading; HWND/DPI/resize, device loss; суммарная VRAM GL+D3D; ожидание CPU/GPU derivative jobs без marker frames; чистое retirement/reload документа и DLL; согласованность материалов/поколений, grounded objects; неподдержанный stst8, unit appearance/animation и недостающие renderer-data assets. Задача редакторской кисти 30–60 мс остаётся отдельным проверяемым пунктом, а успешный внешний кадр — основание продолжать, не её приёмка.
