# Ataman285 — EI-REBUILD64-NEIGHBOURS-285 (2026-11-06)

Штатные соседи подключены и видны в собственных readback. Статус: IMPLEMENTED / HIDDEN CHECKS PASS / STAGED; только RTX4070SUPER. Совпадение с игровым кадром не заявляется: игру агент не запускал, кадр сверяет владелец. **STOP BEFORE INSTALLATION, no push.**

База проверена точно: main, `b9d8834b72728e9a9a1eee9fcd2e0c9cca37c1b4`. AGENTS.md SHA256 `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7`, без локальных изменений. VERSION284/CHANGELOG сверены; новая функция285. Незакоммиченные строки/файлы владельца сохранены. Канонический пункт — EI-REBUILD64-NEIGHBOURS-285 в ROADMAP.md. Навыков evil-islands-modding/private-engine-reference в сеансе нет; исследование ограничено прямо разрешёнными public adapters и обычным кодом маппера, приватный движок не читался и не копировался.

## Исправленный вывод R3 и фактический путь

В [R3 отчёта283](ATAMAN_REBUILD64_STAGE3_283_20261106.md) добавлено уточнение: в установленной283 соседний вход **вообще отсутствовал**, а не был лишь неполно подтверждён. Объединённый snapshot не содержал terrain_resource; layout указывал на несуществующий scratch/layouts.json; terrain placement для соседа не публиковался. neighbour_maps=true не исправлял эти потери.

Использованы public adapters SHA `81dbcdd29b41baeb346f167c6dfb511dff05f295`: Game/OwnedRender/E1Scene, E1Terrain, NeighbourTerrain; external/owned-renderer/Source/owned_resource_runtime, embedded_host, neighbour_client/maps/piece/layout, renderer_neighbour_maps, renderer_static_set, renderer_device/diagnostics. Проверялся публичный интерфейс, не игровой производитель в private Game code.

Хост285 передаёт путь world-map-layouts.json из **выбранного профиля**, настоящее имя активного MPR в OwnedMapSnapshot, неизменённый порядок ресурсных монтирований и identity placement активной карты. Активные MPR и видимые MOB по-прежнему берутся из immutable снимка памяти. Native static-terrain flush вызывает renderer.render_neighbour_maps; тот включает neighbour_client → neighbour_maps → worker-owned neighbour_piece и штатное усвоение. Устройство опубликовано штатным adopt_host_device/ensure_host; epoch — generation опубликованного map snapshot. Источник для weld — опубликованный активный CPU terrain, включая несохранённый рельеф.

Публичный native selector не вычисляет соседей из map.txt во время кадра: он читает catalog paths/dimensions/origins из world-map-layouts.json и сопоставляет имя текущего MPR. map.txt нужен каталогу/метаданным активной карты маппера. В standalone host не придумана вторая топология по travel links. Соседи читаются с диска, как задаёт native layout; их декорации выбираются самим renderer (семейства53/54/55/60; без юнитов, факелов, ловушек). Подготовка height/normal/shading weld, liquids, ground cover, contact maps, fog/casters остаётся в renderer.

Все настройки приходят обычным Config64 reader. Семь относящихся зарегистрированных значений сверены с исходником: neighbour_maps=true, load_reveal_hold=true, neighbour_fog_range=1, neighbour_fog_skip=true, terrain_blend_neighbours=true, shadow_neighbour_casters=2, water_surf_no_sea_maps=zone7. Все остальные настройки сохраняют прежний общий reader и прежние только backend/no-SDK исключения. [Квитанция](../../work/rebuild64-neighbours-285-20261106/neighbour-options.json). Самодельного показа соседей, подгонки света/цвета/тумана/FOV, нового качества или настроек нет.

load_reveal_hold не подменён: renderer включает hold только если получил игровую loading picture подходящего размера. У редактора такого входа нет, поэтому основной кадр видим, пока готовятся native pieces; они принимаются в штатном порядке, по одному за обычный кадр. Никакого ожидания полной соседней сцены на UI нет.

## Жизненный цикл, ошибки и защита

Смена документа сначала останавливает stream, затем retire_embedded_world, terrain placement и публикации предыдущего мира. Закрытие карты и возврат к GL уничтожают renderer session на том же worker; UI только меняет input/paused и продолжает работать. Проверены A(zone1)→B(zone7)→A, GL→Rebuild64→GL→Rebuild64, close/reopen и остановка при вновь начатой загрузке.

Единственный новый renderer patch008 — reviewable stop/join под EID3D11_EMBEDDED: перед выгрузкой DLL worker не оставляется detached после5с. Retail-ветка сохранена; алгоритмы/вид/шейдеры не менялись. Stop/join выполняется на worker хоста. Это не обещание мгновенного окончания файлового I/O; интерфейс не ожидает его при смене вида/карты. Завершение приложения может ждать выхода своего renderer-worker.

Дополнительно исправлен реальный lifetime хоста: remote-core adopt_mounts не имел парного release выбранных RES. Теперь Session владеет обычными public owned_resource_runtime_acquire/release; RAII release выполняется после renderer/world/snapshots. Первый lifetime-контроль был FAIL; он не принят как готовность. После исправления адресный контроль прошёл: до native —2 sfx.res handles GL/звука, после unload/GL — **0 MPR/MOB/RES handles всего скрытого процесса**, RAM2814054400/VRAM51982336 байт. Чужие процессы не открывались для этого контроля; использован собственный process-handle snapshot. [Журнал](../../work/rebuild64-neighbours-285-20261106/case-lifetime-r3/process.log).

Startup marker теперь снимается только после принятого основного кадра **и** neighbour_client_settled(epoch). На human off→on он ставится снова; при новом документе ставится до document load. Тест наблюдал метку при уже видимом основном кадре и pending neighbours и её отсутствие после settled. Decode/layout C++ ошибки ловит штатный поток; host GameJournal::Count теперь также выводит штатные piece summaries и причины failed_step вместо прежнего пустого shim; NEIGHBOUR_FAILURE записывается один раз при изменении причины, основной memory terrain остаётся видимым. На zone12dun2: `Active MPR absent from saved layout`, accepted0, кадр принят — это проверенный отказ входа без падения, не полноценная сцена с соседями.

**Граница:** метка не является обработчиком AV и не изолирует память. Как в283, native access violation внутри in-process DLL может завершить маппер; абсолютная гарантия «любой сбой потока не роняет процесс» **не выполнена**. Проверены штатные C++/layout отказы и безопасное завершение. Для строгой защиты от AV нужен отдельный renderer process — отдельный следующий этап283, не замена шейдера/вида. Если это обязательный критерий принятия285, результат остаётся частичным до этого этапа.

Существующий m_neighborLandscapesVisible передаётся через ABI6; он gates native neighbour_maps исходным config boolean. Других настроек не добавлено. Изменённый активный рельеф подтверждён новым generation и отличающимся собственным readback, с последующим откатом только тестовой памяти. В другом открытом документе несохранённые правки соседней карты **не видны** в этом фоне: native stream читает её последнюю дисковую MPR/MOB, без доступа к чужой document memory. Это соответствует выбранному game-style file stream; активный документ всегда memory-backed.

## Размещение относительно маппера

Сверены native layout selection и реальный CNeighborMapLayout::build на той же сохранённой раскладке выбранного мода, без открытия desktop/рабочего workspace владельца. Общие origins/sizes совпали; смещений проверенных соседей не найдено. Для zone1 native выбрал и принял5: gipat1(−256,0), gipat3(0,288), zone2(192,288), zone3obr(352,64), zone7(352,−256). Для zone7 —4: gipat2(512,128), zone1(−352,256), zone3obr(0,320), zone6(352,320).

**Расхождение состава:** m_neighborPlacements/world workspace содержит все10 других размещённых карт аллода (в том числе дальние); renderer выбирает только прямоугольники того же component, соприкасающиеся/перекрывающие активную карту. Поэтому из списка zone1 renderer не берёт zone6/gipat2/zone4/zone9/zone8; из списка zone7 не берёт zone8/zone9/zone4/zone2/gipat3/gipat1. Общие touching subsets совпали. Дополнительные различия алгоритмов: mapper допускает зазор≤0.5м и dedup по физическому пути, renderer использует строгие прямоугольники и MPR/component identity; mapper сортирует distance/zone, renderer profile/allod/zone. На этих данных дополнительных расхождений координат/дублирования/компонентов не выявлено. OpenGL не менялся. [Native metadata](../../work/rebuild64-neighbours-285-20261106/layout-audit.json), [mapper helper output](../../work/rebuild64-neighbours-285-20261106/case-layout/process.log).

## Кадры и замеры

Только RTX4070SUPER, одна машина; собственные скрытые процессы с0 видимых HWND, redirect всех writable paths/Qt known folders/locks/settings в W=`work/rebuild64-neighbours-285-20261106`. GL background loading в harness отложен, чтобы измерялся нужный native stream; source GL не менялся. Процессы/нагрузка владельца не останавливались. Источник PNG — собственный DLL target readback; screenshot desktop отсутствует. Камера/frozen world segment0 заданы тестом, настройки renderer не снижались.

[Дальний zone1 с соседями](../../work/rebuild64-neighbours-285-20261106/pairs/zone1-far-on.png) / [без](../../work/rebuild64-neighbours-285-20261106/pairs/zone1-far-off.png); [граница с](../../work/rebuild64-neighbours-285-20261106/pairs/zone1-boundary-on.png) / [без](../../work/rebuild64-neighbours-285-20261106/pairs/zone1-boundary-off.png); [zone7](../../work/rebuild64-neighbours-285-20261106/pairs/zone7-far-on.png); [пещера](../../work/rebuild64-neighbours-285-20261106/pairs/cave-on.png); [несохранённый рельеф до](../../work/rebuild64-neighbours-285-20261106/pairs/unsaved-before.png) / [после](../../work/rebuild64-neighbours-285-20261106/pairs/unsaved-after.png). Всего12 PNG с SHA в [captures.json](../../work/rebuild64-neighbours-285-20261106/captures.json). Повторные кадры не означают game-frame equivalence.

Основной итоговый прогон: main_ms — от выбора вида до первого принятого кадра; neighbours_ms — до settled всех соседей/отказа. CPU wall frame call включает native submission/composite/Present/memory query, **не GPU timestamp**, без PNG capture. Samples от main frame до settled/ещё кадров; p95 включает adoption и подготовку материалов.

|Случай|Main мс|Соседи мс|Принято|Frame median мс|p95 мс|Private GiB|Video GiB|
|---|---:|---:|---:|---:|---:|---:|---:|
|zone1-far-off|1873|1873|0|16.65|17.2401|4.69|1.07|
|zone1-far-on|1579|4200|5|16.1993|165.239|6.29|2.36|
|zone1-boundary-on|798|2888|5|16.6543|121.758|6.27|2.24|
|zone7-far-on|2225|4233|4|42.0262|175.171|7.80|2.67|
|zone1-return-on|1206|3122|5|16.447|136.148|7.44|2.40|
|zone1-reselect-on|1579|3424|5|16.6261|143.503|7.61|2.41|
|cave-on|979|979|0|16.6472|16.8295|3.76|0.42|
|zone1-reopen-on|1367|3249|5|16.2654|159.121|7.48|2.41|

Отдельный адресный замер после settled, ещё>60 frame notifications на той же дальней камере zone1 (без повторения всей матрицы):

|Случай|n|Resident median мс|p95 мс|Private GiB|Video GiB|
|---|---:|---:|---:|---:|---:|
|zone1-far-off|57|16.5634|16.8073|4.73|1.10|
|zone1-far-on|60|16.6216|16.8726|6.43|2.36|

В этом адресном замере main1649/1611мс (off/on), все5 соседей —3813мс от выбора вида; main принят с0 соседей. RAM — private bytes всего процесса, VRAM — DXGI local CurrentUsage этого процесса, включая Qt/GL и D3D. Короткий замер, не абсолютный benchmark; рост памяти при принятых соседях зафиксирован, не использован для изменения качества. UI timer продолжал срабатывать во всех основных случаях. [timings.json](../../work/rebuild64-neighbours-285-20261106/timings.json).

Финальные проверки: native-final exit0/61 checks; lifetime-r3 exit0/16; gl-control exit0/10; layout exit0; steady exit0. Предварительный native-r1 с прежним lifetime не используется вместо финального. Неудачные сборки harness и преждевременный собственный case-lifetime прекращены/исправлены; их логи не PASS. libpng IDAT CRC warnings остались классом старых resource warnings283, ресурсы не исправлялись.

## OpenGL, сохранность и сборка

При выбранном OpenGL новых изменений поведения/инструментов/горячих клавиш/сохранения/форматов/MCP/uiScalePercent/выбора вида по умолчанию нет. DLL не загружается на GL-only startup. Один контрольный прогон на сохранённом кадре R3: 7/921600 изменённых пикселей, mean absolute RGB 0.000173249/255, max channel 83. Это не0-pixel proof: ранее в R3 уже была микронестабильность7 пикселей/того же max83; повтор для подгонки не делался. GL — только техническая регрессия, не эталон вида Rebuild64. [Квитанция](../../work/rebuild64-neighbours-285-20261106/gl-regression.json).

MPR/MOB/RES-путь нового stream только читает; Save и форматы не затронуты. Source hashes после тестов сохранены. C:/GOG Games/PZ не записывался; Base-карты не открывались/копировались; AppData/ProgramData/настройки/буфер владельца не использовались. Мосты, headless, установленные EXE/DLL и Codex-процессы владельца не трогались. subst пуст, junction нет. Защищённые CPP/H/QM/.mcp.json/отчёт277 и хвост ROADMAP сохранены по [protected.json](../../work/rebuild64-neighbours-285-20261106/protected.json); коммит не включает их.

Release из git archive tree `7dc4ed1548f598176ba9f3c9415a44536cb84509`; проверяемые production sources соответствуют ему, Qt-продуктовый код этого архива сохранён. Итоговый DLL-host (добавлено только forwarding журнала) собран из отдельного git archive tree `98143853cf092bbf1f69a84912bb5b13a9208a04` по тому же рецепту; адресный case-journal exit0 подтверждает native summaries accepted=1/failed_step=none, декорации и кадры. Эта правка shim не меняет renderer/его вид/lifetime. Wrapper вернул1 на известной x86.cmd передаче нормализованного пути с пробелами — **не PASS wrapper**; Qt/MCP собраны, оба x86 runtime из того же архива собраны напрямую через8.3, exit0/0. Mapper SHA256 `ac369aee6cccf3fd01945652dfbde287304c67dbc808565cd4161564bbcc1cd5`; DLL `a3347efdd55380257a7223cb547471c8b36b404d6addad10b90f63be9a720c20`. [Квитанция](../../work/rebuild64-neighbours-285-20261106/release-receipt.json).

Рецепт переиспользовал материал renderer283. Весь runtime пересобран с001–008, lib SHA256 `e21003c22efd3999c598a9bb7e99743fc95ca1dbc19320179185f0af6ee4c2d9`; одиночный object splice не применялся. Shader table638 не пересобиралась:190 shader headers/HLSL совпали с283, новая008 касается только stop/join; bin SHA256 `38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64`. Все3 shader resources совпали с283. Импорты без NVIDIA SDK, единственный export ataman_renderer_get_api. [Shader audit](../../work/rebuild64-neighbours-285-20261106/shader-audit.json), [DLL audit](../../work/rebuild64-neighbours-285-20261106/dll-audit.json).

[Свежий staging285](../../build-staging-285-rebuild64-neighbours-20261106/): finalizer/validator exit0, состав против build-staging-284-navbake-close-20261106 — 1364→1364, **0 потерь**, добавлено 0: список пуст. Raw/staged --version285 exit0, SHA совпали. Первая финализация была до дополнения host journal; непереданная собственная staging-папка проверена по всем прежним SHA, обновлена только своей DLL/BUILD_INFO и финализирована повторно (новая причина/новые входы). После окончательной финализации не запускалась и не менялась. [Package receipt](../../work/rebuild64-neighbours-285-20261106/package-receipt.json). Установка запрещена этим поручением и не выполнялась; push нет.

## Вопросы Илье и оставшаяся приёмка

Для самого запуска штатного потока блокирующего вопроса нет: public embedded ingress достаточен. Полезный следующий вопрос: предполагается ли публичный selector для профилей кроме hardcoded deja-vu и как нормативно обрабатывать cave/отсутствующую layout entry без диагностической ошибки? Текущий selector не менялся.

Строгая гарантия сохранности процесса при native AV требует выделения renderer process; задать Илье вопрос о рекомендованном public embedded/scene-core lifetime/seam для изолированного host, без изменения вида. До этого AV recovery marker не выдаётся за containment. Прежние runtime-input gaps283 (TORCH/weather/terrain TL state и др.) этим изменением не закрыты. Равенство с игрой, фактическую художественную картинку и видимые панели принимает владелец.

Из программы удалено: **ничего**. OpenGL изменено: **ничего**. Удаление собственных6 приватных map fixture payloads после PASS — только инженерная очистка, по [cleanup-plan.json](../../work/rebuild64-neighbours-285-20261106/cleanup-plan.json), не удаление функциональности или пользовательских данных. Полный SHA scoped-коммита — в итоговом ответе и W/handoff.json. **STOP BEFORE INSTALLATION, no push.**
