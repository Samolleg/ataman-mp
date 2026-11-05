# EI-REBUILD64-RENDERER-STUDY-20261105 — этап 0

Дата исследования: 2026-11-05. Результат: исследование и план, без реализации.

## Решение для владельца

**Технически отдельный игровой просмотр осуществим как проект интеграции; заменить текущую графику подключением готовой библиотеки сейчас нельзя.** Рекомендация — сначала второй режим «Вид: OpenGL / Rebuild64», с общей моделью документа и камерой. Первый показываемый результат — неизменяемый снимок одной карты с её ресурсами, освещением и соседями рядом с GL. Полное редактирование и отказ от GL — последующие самостоятельные рубежи.

Четыре существенных препятствия:

1. **Право:** исходная предпосылка «лицензии нет» устарела. В закреплённой поставке есть PolyForm Noncommercial 1.0.0. Её ограничение коммерческого применения конфликтует с распространением объединённой работы под GPLv3. До поставки пользователям нужно совместимое разрешение правообладателей, включая изменения embedded-редакции и зависимости.
2. **Игра:** embedded-ветвь заменяет чтение адресов типизированными входами, но весь `Game/OwnedRender` не является независимым SDK. Нужен новый адаптер маппера; игровые классы и приватные/производные исходники движка в него не переносятся.
3. **Редактирование:** поддержана публикация нового поколения геометрии и GPU-поверхностей; готовый редакторский протокол кистей, полного рабочего атласа, Undo/Redo и согласованного обновления всех производных эффектов не подтверждён. Неизменяемость снимка не означает невозможности обновления, но требует явной замены поколения.
4. **ABI/окно:** MinGW/Qt и MSVC/C++ нельзя соединить прямой передачей C++-объектов. Предпочтителен MSVC DLL-хост с узким C ABI и отдельной нативной поверхностью QWidget; GL остаётся рабочим резервом. Отдельный процесс дороже и не устраняет лицензионный вопрос.

Это инженерная оценка условий интеграции, **не юридическое заключение**, не подтверждение визуального соответствия игре и не разрешение на реализацию следующего этапа.

## 0. Идентичность, границы и способ исследования

Обозначения для всех ссылок `путь:строка` ниже:

| Метка | Корень и неизменяемая ревизия |
|---|---|
| **M** | `C:/GOG Games/maped/ei-mapper-ataman`, ветка `main`, исследованный commit `401bd0b8d5f66e3bcd1f3a8252f12e8a1972e994` (279). Исходники читались также через `git show`/`git grep` именно этого SHA, чтобы параллельные правки 280 не меняли доказательства. |
| **R** | `C:/Users/samol/OneDrive/Документы/Проклятые Земли/work/eirebuild64`, закреплённый `origin/main = 81dbcdd29b41baeb346f167c6dfb511dff05f295`. Все выводы о поставке относятся к этому объекту Git, не к рабочим файлам checkout. |
| **P** | Разрешённые справочные `RENDERER_ARCHITECTURE.md` и `FEATURE_MATRIX.md` из `work/eirebuild-renderer-source01-preserved`. У этих двух файлов использован SHA-256, а не приписанный им Git SHA; это историческая справка, не доказательство текущей x64-приёмки. |

Рабочая ветка EIRebuild64 при входе — `task/X64ICE01`, HEAD `d9bdfc1042a939ed48cbd42b13c5ba15735b7775`; переключений, fetch и записей туда не было. R-вендор основан на upstream Ilufus `b1ffc736d4792eb36f3528a8d8ee2c358da80d54`, предок `ef7e12467db17ef673f8ce3b105063b4b3b16417`; это **изменённая** embedded-поставка, не исходный upstream (`R:external/owned-renderer/PROVENANCE.md:1–22`, `OwnedRenderer/Renderer.props:14`).

Актуальный `AGENTS.md` прочитан с диска, tracked diff отсутствовал; SHA-256 `2A2F58EF34F63D6C5D67D165D0B809B046D0A519181119D22EA8FDD8781FE3B7`. Прочитаны CONTRIBUTING и относящиеся договоры/описание toolchain. По явному ограничению владельца ROADMAP не изменяется; этапы и следующий шаг сохранены в этом отчёте. Открытый EI-SAVE-INTEGRITY-280 и чужие изменения не включаются в задачу.

Навыков `evil-islands-modding` / `evil-islands-private-engine-reference` в доступном каталоге нет. Исследованы только явно разрешённые renderer/adapter, проектные файлы, notices и справочные документы. Приватные исходники из других мест не читались и не копировались; обнаруженная зависимость от заголовка движка не устранялась переносом такого заголовка.

Через `git archive R OwnedRenderer external/owned-renderer Game/OwnedRender` получена отдельная копия исключительно в `work/rebuild64-renderer-study-20261105/snapshot`. В ней 100 файлов `.cpp/.h/.inl` адаптера, 10 352 строки; в `external/owned-renderer/Source` — 287 таких файлов, 190 402 строки (без ThirdParty/Tools/assets). Это размер области исследования, не оценка стоимости переноса.

Маппер, игра, EIRebuild и E1/E2 исполняемые проверки не запускались; компьютерный интерфейс и снимки экрана не использовались. Выполнена только разрешённая попытка сборки статической библиотеки в собственной копии, подробности ниже. Не менялись установки, настройки, игровые ресурсы, процессы, буфер, subst/junction и Git-конфигурация владельца.

## 1. Право и происхождение

### Что фактически обнаружено

`R:THIRD_PARTY.md:14` прямо называет автора Ilufus, upstream и **PolyForm Noncommercial 1.0.0**. Полный текст находится в `R:external/owned-renderer/LICENSE.md:1–73`, notices — `NOTICE.md:3–24`. Research/reference в `NOTICE.md:26–30` заявляет самостоятельное написание рендера и отсутствие включённого кода изученных сторонних проектов; это заявление поставщика, не независимый аудит всего происхождения. Required Notice закрепляет авторство, некоммерческие условия и указание автора в README/credits.

Маппер имеет GNU GPL v3 (`M:LICENSE.txt:1–4`), и `M:NOTICE.md:15–18` объявляет GPL-3.0 для производного редактора. Существенны GPL §5(c), весь объединённый результат (`LICENSE.txt:222–228`), §5 об aggregate (`:236–243`) и запрет дополнительных ограничений §10 (`:463–471`). PolyForm разрешает распространение/изменения только в пределах permitted purpose (`R:external/owned-renderer/LICENSE.md:9–25,31–49`). Бесплатный ZIP для других людей всё равно является распространением; «некоммерческий проект» сам по себе не снимает конфликт условий объединённой работы.

**Вывод по поставке:** техническая возможность линковки не даёт права поставлять результат. Статическая линковка GPL-редактора с этим кодом не имеет установленного здесь совместимого основания. DLL не является автоматическим исключением. Отдельный процесс тоже не даёт гарантированного исключения: самостоятельные независимые программы и тесно связанная объединённая работа требуют отдельной оценки; способ IPC/упаковки сам по себе вопрос не решает. Публичная доступность исходников не заменяет лицензию.

Частное некоммерческое исследование/эксперимент следует отличать от поставки: PolyForm прямо предусматривает personal research/testing (`LICENSE.md:35–37`), а GPL допускает частные изменения без передачи (`M:LICENSE.txt`, §2). Это не основание выдавать объединённый ZIP другим людям и не отменяет отдельные права на игровые исходники/данные и SDK.

Минимум для движения к распространению: получить от Ilufus и иных правообладателей соответствующих изменений **письменное предоставление GPLv3-совместимых прав на точный код/ревизию** (например, дополнительную GPLv3/MIT-лицензию), разрешающих копирование, изменения, исходную и бинарную поставку, дальнейшее распространение получателями без noncommercial-ограничения. Разрешение «используйте в Ataman бесплатно» недостаточно определённо. Должно быть ясно, кто вправе лицензировать локальные embedded-изменения EIRebuild64 и переносимые assets. Если автор выбирает исключение, оно должно действительно разрешать требуемую GPLv3-поставку, а не только загрузку DLL. Согласование и юридическая проверка остаются владельцу; запросов автору в рамках этапа 0 не отправлялось.

### Онлайн-проверка 2026-11-05

Прочитана только указанная публичная GitHub-страница: [Ilufus/evil-islands-owned-renderer](https://github.com/Ilufus/evil-islands-owned-renderer). Web-инструмент получил **HTTP 404 Not Found**. Попытка открыть [LICENSE.md в main](https://github.com/Ilufus/evil-islands-owned-renderer/blob/main/LICENSE.md) дала `Cache miss`, а не содержимое файла. Поэтому **публичная доступность репозитория и наличие лицензии на текущей публичной странице не подтверждены**. По 404 нельзя различить приватность, удаление, переименование или ограничение доступа. Локально в R наличие лицензии подтверждено независимо; утверждение «общая лицензия не обнаружена» для R неверно. Никаких входов в аккаунты, clone/fetch или изменений GitHub не было.

### Зависимости и что понадобится поставлять

| Компонент | Условия / доказательство в R | Значение для маппера |
|---|---|---|
| Собственно EID3D11 + изменения | PolyForm NC; `external/owned-renderer/LICENSE.md:9–49`, `NOTICE.md:14–24`, `PROVENANCE.md:1–22` | Основной правовой блокер. Закрепить полную ревизию, сохранённые notices и статус modified. |
| Dear ImGui 1.92.9b | MIT, pin `f1cc2ae15e53a861a874c3034aae6798fde194ab`; `ThirdParty/imgui/PROVENANCE.md:3–9`, `LICENSE.txt` | Сохранить copyright/лицензию. Сейчас компилируется проектом библиотеки. |
| meshoptimizer 1.1 | MIT; `ThirdParty/meshoptimizer/PROVENANCE.md:1–8`, `LICENSE.md` | Использованы simplifier/allocator для LOD, сохранить notices. |
| SMAA | MIT-подобный permissive текст; `ThirdParty/smaa/LICENSE.txt:1–25`; `THIRD_PARTY.md:17` | Проверять собственный текст; upstream commit в provenance не установлен. Не выдумывать pin. |
| AMD FSR2 | MIT; `external/owned-renderer/NOTICE.md:39`, `ThirdParty/fsr2/LICENSE.txt`, `PROVENANCE.md` | Сохранить лицензию/происхождение точных включаемых частей. Наличие каталога не означает обязательную отдельную DLL. |
| NVIDIA DLSS/NGX SDK 310.9.16 | Отдельная NVIDIA RTX SDK License/RTX Supplement, не MIT/GPL; `THIRD_PARTY.md:68–94`, `OwnedRenderer/DlssSdk.props:3–15` | Текущий проект требует headers, x64 `/MD` import lib и release DLL даже для подготовки библиотеки. Поставка DLL/атрибуция/EULA требуют отдельной проверки совместимости; разрешение Ilufus не лицензирует NVIDIA-код. Самостоятельно отключать/переписывать backend не предлагается как уже разрешённое действие. |
| D3D11/DXGI/D3DCompiler и Windows import libs | Windows SDK/platform dependencies; `OwnedRenderer/RendererShaders.vcxproj:34`, `Renderer.props:10–17` | Платформенные компоненты, не общая лицензия renderer. Проверить redistributable/CRT и применимость GPL system-library exception к конкретной комплектации; не объявлять все SDK свободными. |
| Camp art / Terrain 2.0 masks | `Assets/camp/PROVENANCE.txt:1–7` — генерация, игровые изображения лишь references; `Assets/terrain2/PROVENANCE.txt:1–7` — копии масок маппера владельца | Asset provenance не даёт прав на игровые карты/текстуры. Для preview camp UI не нужен; включённые `.res` всё равно инвентаризировать. Права на маски/их происхождение подтвердить отдельно при упаковке. |

Пути `ThirdParty/*` и `Assets/*` в таблице относительны `R:external/owned-renderer/`. Дополнительные зависимости **всего EIRebuild64**, которые не следует автоматически тащить в renderer SDK: miniaudio 0.11.25 — public domain/MIT-0; nlohmann/json 3.10.5 — MIT; LuaJIT GC64 — MIT; DirectXTex/DirectXTK — MIT; FFmpeg 8.1.3 — LGPL-2.1-or-later в заявленной shared-конфигурации без GPL/nonfree; XAudio2.Redist — отдельные условия Microsoft; Recast/Detour 1.6.0 — zlib (`R:THIRD_PARTY.md:10–24,28–42,96–102`). Это манифест/рецепты проекта, не подтверждение наличия или необходимости всех этих бинарников в отдельном viewer. Базовые игровые и модовые ресурсы остаются внешними пользовательскими данными.

## 2. Граница игры и минимальный embedded-контракт

**Retail/proxy-ветвь привязана к конкретной игре.** `R:external/owned-renderer/Source/game_sites.h:8,119–204` проверяет образ и группы адресов; `engine_bridge.cpp:107–132` содержит размеры образа и RVA, `:444–454` описывает вызовы `CVisualizator`. Hook/installers и native layout readers относятся к этому маршруту. P-архитектура прямо ограничивает адреса fingerprint конкретного game.exe (`P:RENDERER_ARCHITECTURE.md:12–15`). Нельзя использовать их как API маппера или загружать игровые DLL/аддоны для получения данных.

**Embedded не требует именно игрового процесса**, если хост полностью поставляет необходимые данные. `EID3D11_EMBEDDED` задан в `R:OwnedRenderer/OwnedRenderer.vcxproj:114`; исключения retail-reader ветвей видны в `engine_bridge.cpp:51–65,2660–2694`, неиспользуемые hook-only helpers отделены в `embedded_bridge_stubs.cpp:1–18`. Сам факт наличия адресов/файла game_sites в дереве ещё не означает выполнение hook-маршрута в embedded. Обратно, отсутствие hook само по себе не означает независимость всего C++-графа от движка.

Адаптер EIRebuild64 зависит от игры по смыслу и по headers: `R:Game/OwnedRender/Startup.cpp:4–24` получает SettingsProfile/ModRuntime; `E1CompletedVisuals.cpp:4–11,45` использует WorldClient, Unit и CObject3DClientSpecific; `E3Game.cpp:4–10` — игровые экраны; `E1VisualFrame.cpp:1,6` — CorePerf/GameJournal. Это **игровой/потенциально производный от игры слой**, происхождение и права которого нельзя выводить из лицензии Ilufus. Он не переносится в GPL-редактор. По правилам Ataman приватные исходники не входят в Git/комплект (`M:AGENTS.md`, разделы «Данные и исследование» и «Единственная ветка main»).

Минимальный собственный хост должен обеспечить:

| Группа | Конкретный контракт и свидетельство R |
|---|---|
| Пути/ресурсы | `Source/embedded_host.h:9–38`: абсолютные пути INI/layout, ordered mod/base mounts, configure один раз до workers/renderer; `owned_resource_store.h:59–79,111–127`: identity/incarnation, immutable resource bytes, loose roots прежде ordered archives, отдельные kinds. ResourceStore читает/разрешает RES/MMP/FIG и другие assets, но не выбирает за хост правильный профиль. |
| Окно/device/lifecycle | `Game/OwnedRender/E1RendererFacade.h:17–100`: initialize HWND, resize между кадрами, begin/end/present/discard. Реальный Renderer создаёт D3D11 device (`Source/renderer_device.cpp:599–627`) и swapchain HWND (`:262–269`). Хост управляет вызовами/lifecycle; это не готовый Qt-widget и не правило «передай любой GL FBO». |
| Мир/камера/свет | `Source/engine_bridge.h:2037–2077`: текущий typed visual frame, transform, camera anchor, lighting, flags, terrain, fog callback. `Game/OwnedRender/ei_renderer_api.h:74–96` описывает world generation, регион, игровое время, погоду, ветер, цвета/параметры света и тумана. Одной view-projection матрицы недостаточно для вида как в игре. |
| Terrain/материалы | `Game/OwnedRender/E1Terrain.h:9–58`: source reader, source/world revision, атласы, значения секторов, материалы и покадровая геометрия. `E1Terrain.cpp:66–125`: копия map members, decode/validation, новая generation; texture reader нужен также классификации воды/болота. |
| Модели/анимация | `Source/retained_static_scene.h:106–182` — appearance/resource/assembly lifetimes; `owned_visual_evaluation.h:89–90,128–150` — animation assets/pose/shadow facts. Хост поставляет разрешённые сборки FIG/LNK/BON/ANM, части, трансформы, фазы, видимость/свет и смену поколений; игровую логику не симулирует renderer. |
| Borrowed inputs/output | `Game/OwnedRender/ei_renderer_api.h:4–9,33–44,108–132`: синхронные входы, размеры/version, без STL/allocator/engine pointer через DLL. `Source/embedded_device.h:6–22` — callback-lifetime targets/output, не удерживать и не present из visitor; сериализовать resize/release. |

Здесь `Source/*` означает `external/owned-renderer/Source/*`. Старый `ei_renderer_api.h` полезен как **образец C-контракта**, но не доказательство готовой совместимой DLL: `R:OwnedRenderer/README.md:22–25` прямо говорит, что это сохранённые типы старого source-host ABI c40, а DLL c40 не подключена. Текущая библиотека имеет C++/STL-интерфейсы; новый экспорт нужно отдельно спроектировать, версионировать и проверить.

### Что показывают E1/E2-проверки без игры

- `R:Game/OwnedRender/E1SceneCheck.cpp:65–110,126–174`: lifecycle/membership, повторное использование ID, retirement, immutable frame, fixture animation и публикация pose. Это источник сценариев для отдельного хоста.
- `E1TerrainCheck.cpp:95–137` создаёт D3D11 WARP без окна, проверяет поверхность/пиксели/foreign-device/generation; `:449–460` проверяет dispatcher с fault-injector sink и прямо не объявляет это GPU-кадром; `:588–609` проверяет replacement/edited terrain и retirement без игры/окна.
- `E2TerrainAnimationCheck.cpp:18–75` проверяет фазы тайлов/RNG/revision на fixture-данных без игры/GPU. `HostPathsCheck` и `ResourceMappingCheck` — примеры mounts/identity/lifetime; их границы перечислены в `Game/OwnedRender/README.md:15–60`.
- `E1RendererFacadeCheck.cpp:163–216` **создаёт HWND**, читает shader resource и задаёт ресурсные пути. Это пример полной GPU/lifecycle-проверки, не разрешённый здесь запуск. Без игры не означает автоматически без окна и внешних ресурсов.

Эти проверки **прочитаны, но не выполнялись**. Их код показывает декомпозицию и ожидаемые контракты, а не новый PASS для R или готовый viewer всех игровых карт.

## 3. Возможности мира и обязательства редактора

### Что реализовано в renderer-поставке

| Мир | Свидетельство R | Что ещё должен дать маппер |
|---|---|---|
| Terrain: тайлы, материалы, LOD/швы, атласы | `Source/owned_terrain_front_end.h:405–457`, `owned_terrain_decode.h:125–140`, `renderer_terrain.cpp:1,104–146` | Точные map members, слои, актуальные текстуры, phase/material values. Tile-family профиль и placement принадлежат конкретному атласу. |
| HDPack / соседние карты | `Source/owned_resource_store.cpp:851–855` знает JSON/семантику атласа рядом с MMP в HDPack_Terrain; `embedded_neighbour_terrain.cpp:1–4`, `neighbour_maps.h:83–118` | Реальный порядок mounts, layout/transform соседей, их геометрия/ресурсы. Упоминание HDPack не гарантирует любой пользовательский пак или согласованность ещё не сохранённых соседей. |
| Статика/фигуры/анимация | `Source/retained_static_scene.h:106–214`, `owned_visual_evaluation.h:89–150`; `PROVENANCE.md:31–53` | Полная иерархия, shape/parts, материалы/прозрачность, pose/animation clock; готовые live Unit-состояния заменить собственными render snapshots. |
| Вода и болото | `Source/renderer_liquid.cpp:1–4`, `Game/OwnedRender/E1Terrain.cpp:104–120`, `E1TerrainCheck.cpp:380–400` | Классификация по CPU atlas bytes, transparent material range, актуальная геометрия/свет/ветер. Одной прозрачной плоскости недостаточно. |
| Тени, включая соседей/point lights | `Source/renderer_shadows.cpp:1–11`, `renderer_point_shadows.cpp`; `engine_bridge.h:2071–2077` | Shadow eligibility, world/pose/depth revisions, корректное retirement/invalidation при редактировании. |
| Туман/небо/время суток/погода | `Source/renderer_fog.cpp:1–17`, `renderer_sky.cpp:1–6`, `engine_bridge.h:74–82`; `Game/OwnedRender/ei_renderer_api.h:74–96` | Час, region/cave, weather/time progression, fog/lighting/wind и выбранные настройки. |
| Частицы/ambient/осадки | `OwnedRenderer/OwnedRenderer.vcxproj:52,58–62`, `Source/embedded_game_effects.h`, `renderer_precipitation.cpp` | Источники и clocks/visibility, типы и состояния эффектов. Renderer-owned ambient эффекты не заменяют все произвольные scripted game effects. |

Таблица устанавливает **наличие кода/входов**, не художественную приёмку или стопроцентное покрытие любой карты. По P:FEATURE_MATRIX разные эффекты имели разные статусы implemented/passed/deferred; переносить те статусы на R нельзя. SHA-256 P-документов: architecture `F3760B3135264CAFD5B3F1F4DB09396F780EA9D60DD7BE81EBA8A8C143FEECFF`, feature matrix `171103E647A451BA338EED1BCC3539F03B986ACF30C1C09377645DA7F60DD822`.

### Что требуется редактору и не является готовой функцией этого renderer API

| Функция Ataman | Доказательство M | Последствие интеграции |
|---|---|---|
| ID-проход выделения, alpha-aware picking/рамка, глубина | `assets/shaders/select.fsh:6–18`, `source/render/view.cpp:5055,11483–11496,25032` | Игровой hover/selection существует в renderer-семантике, но это не готовый эквивалент редакторского ID/rectangle picking со скрытыми объектами/helpers. Нужен отдельный ID/depth pass либо согласованный CPU picker. |
| Манипуляции и helper-линии/ghost | `source/render/view.cpp:6692,6798,12059–12081`; `source/app/main_window.cpp:9689` | Перенести/сохранить действующие interaction/preview overlays. **Поправка предпосылки:** строка про gizmo в main_window — TODO; нельзя объявлять наличие законченного универсального gizmo только по слову в запросе. В любом случае готового редакторского gizmo API у renderer не установлено. |
| Навигационные оверлеи, граф/legend | `source/render/view.cpp:6714,6970`, `view_navmesh.cpp:56–82` | Это данные/визуализация маппера. Сохранить поверх viewport с корректными depth, DPI и picking; не путать с игровым navmesh. |
| Кисти/preview и живой рельеф/покраска | `source/render/view.cpp:8896,12059–12081`; `view_world_terrain.cpp:31–59` | Сохранять транзакцию stroke, dirty/Undo и показ несохранённых изменений. GPU-preview не должен записывать документ или требовать Save. |
| Полный рабочий атлас 16 384 слота | `source/documents/terrain_virtual_atlas.h:25`, `terrain_virtual_atlas.cpp:2925,3355`; `source/render/view.cpp:15690` | Проверить logical slot→page→UV/type/family для полного размера и пользовательского перераспределения. Это число слотов, **не размер текстуры в пикселях**. Renderer материал/texture-array limit — другой предел; прямое равенство не установлено. |
| Top-down экспорт/мини-карта | `source/render/view_topdown.cpp:15–78` — GL context/state/private FBO; `source/app/topdown_export.cpp:277` | Нужны orthographic pass, слои/filters, offscreen readback и восстановление камеры/состояния. Наличие output visitor у D3D backend не реализует этот контракт автоматически. |
| Предпросмотр моделей | `source/render/preview.h:18–24`, `preview.cpp:13–14,52–90,141–170` | Это отдельный GL widget/shared resources. Generic renderer preview primitives есть, но Qt catalogue/pose/lifecycle/cache нужно интегрировать; camp/game UI переносить не требуется. |
| MCP window_snapshot | `source/app/ai_agent_protocol.cpp:114`, `ai_agent_server.cpp:357–444` | Сохранить current-camera, revision/profile/map, async/cancel/timeout/лимиты/PNG; переподключить capture к выбранному backend. Возврат старого GL кадра под видом нового D3D недопустим. |
| Невидимый headless | `source/app/main.cpp:546,4185`, `source/render/view_topdown.cpp:18–30` | Уже сейчас «невидимый» означает работающий graphics context. Для D3D отдельно решить HWND/offscreen, отсутствие activation/fullscreen/dialogs и правильный readback. WARP test не доказывает весь MCP headless-сценарий. |

Поиск `ei::gl`/GL entry points по `M:source/render, source/objects, source/app` вернул **19 файлов**: `app/scenario_layer_check.cpp`, `app/topdown_export.cpp`, `app/world_map_snap_check.cpp`, `objects/magictrap.cpp`, `objects/unit.cpp`, `render/gl_dispatch.h`, `render/landscape.cpp`, `render/landscape.h`, `render/ogl_utils.cpp`, `render/part.cpp`, `render/preview.cpp`, `render/render_queue.cpp`, `render/sector.cpp`, `render/texture_page_upload.h`, `render/tile.cpp`, `render/view.cpp`, `render/view_camera_track.cpp`, `render/view_navmesh.cpp`, `render/view_topdown.cpp` (все относительно `source/`). Это воспроизводимый перечень по заданному поиску, не обещание, что переносу подлежат только эти файлы: shader/context/resource ownership и document/view связи шире.

### Критичный вывод об изменяемом рельефе

Называть renderer «только для навсегда неизменяемых карт» **неверно**. `R:Game/OwnedRender/E1Terrain.cpp:73–125` повторно использует тот же source revision, а для новой ревизии очищает публикацию, копирует members, декодирует и выпускает новое поколение. `E1TerrainCheck.cpp:85–87,588–605` содержит изменённые MP bytes и проверяет новый maximum_z. Поверхности имеют BeginWrite/Complete и generation (`E1TerrainCheck.cpp:421–431`), материалы публикуются отдельно (`E1Terrain.h:51–58`). Это реальные точки для live updates, а не необходимость писать MPR на диск.

Но **редакторское редактирование с гарантированной интерактивной задержкой не подтверждено**. Базовый путь заменяет/декодирует снимок, не предоставляет готовый `update_height_rect`/`paint_slots` с охватом всех caches. Ревизия phase/wind не эквивалентна изменению topology/высот. Нужна согласованная invalidation геометрии, нормалей/bounds/LOD/стыков, shadow/depth/occlusion, terrain2/material caches, воды/болота/contact/cover, соседей и picking. Нельзя менять память под `shared_ptr<const TerrainCpuAsset>` или переиспользовать identity для новых bytes.

Предлагаемый начальный безопасный контракт: immutable документный снимок + монотонный revision, полная замена поколения между кадрами, сохранение старого snapshot до завершения использующих его работ; затем только по измерениям — sector/page deltas. При отказе replacement текущий E1 путь очищает источник (`E1Terrain.cpp:74`); viewer должен явно показать ошибку/устаревание и дать GL, а не тихо выдавать старое изображение за новое. Критерии live-preview требуют и appearance after Undo/Redo, и стабильности GPU/CPU памяти при многократной замене.

## 4. ABI, Qt и фактическая сборка x64

### Выбор границы

M использует Qt 5.12.12 `mingw73_64`, GCC 7.3 x64 (`M:docs/BUILDING_ENDURANCE.md:5–10,21–26`). В текущем source target уже C++14 (`M:ei_maper.pro:28–29`), а backend MSVC v143 — C++17 `/MD` (`R:OwnedRenderer/Renderer.props:10–17`). **Фактический CView и CPreview наследуют QGLWidget**, не QOpenGLWidget (`M:source/render/view.h:193`, `preview.h:18`). Упоминания QOpenGLContext не меняют класс виджета. План для QOpenGLWidget рассматривается как возможная будущая композиция, не описание уже существующего frontend.

| Вариант | Плюсы | Минусы / условия |
|---|---|---|
| **A. MSVC DLL, C ABI; Qt/MinGW остаётся** | Минимальный охват toolchain, opaque handle и snapshots, отдельное владение CRT; renderer и shader producer собираются родным компилятором. Рекомендуемый первый путь. | Нет готовой актуальной DLL. Нужны create/resize/frame/snapshot/capture/destroy, version/size/capabilities, ошибки, allocation/free одной стороной, thread/lifetime/cancel. Не передавать Qt, STL, C++ exceptions/RTTI/shared_ptr через ABI. Старый C header не экспортирует автоматически текущий embedded backend. Сбой DLL может завершить маппер. |
| **B. Отдельный MSVC процесс + shared D3D11 texture** | Сбой/зависание renderer изолируется; процесс может владеть своим runtime и devices. | IPC snapshot/revision/acks, resource lifetimes, adapter LUID и разрешения shared handles, keyed mutex/fences, resize/restart/retirement, backpressure и readback. Нельзя передать ID3D11Texture2D pointer через IPC. Для показа в GL всё равно нужен interop/copy либо отдельное native окно. Двойные caches/VRAM и задержка возможны, но не измерены. Право не решает. |
| **C. Весь маппер на MSVC + matching Qt kit** | Единая C++ ABI/toolchain, прямой C++ adapter технически проще. | Самый широкий риск: GNU switch-range и прочие расширения, весь native/vendor/link/runtime graph, упаковка Qt plugins и regression. BUILDING прямо предупреждает, что MSVC kit не является текущим fallback (`M:docs/BUILDING_ENDURANCE.md:21–26`). Даже после порта C++17 и ресурсный контракт остаются. Не первый этап. |

Прямое добавление MSVC `.lib` с C++-объектами в MinGW qmake не является корректным решением ABI. COFF/x64 machine format недостаточен: name mangling, CRT/allocator, exceptions/STL и правила сборки должны совпасть. Узкая C ABI позволяет runtime `LoadLibrary/GetProcAddress` либо проверенную import library, но не устраняет требования права/данных.

### Куда выводить D3D11

| Поверхность | Применимость / риск |
|---|---|
| **Нативный дочерний QWidget с собственным HWND** | Наиболее короткий первый показ: renderer создаёт swapchain для отдельного HWND; существующий QGLWidget остаётся соседним/переключаемым viewport. Не назначать одновременно GL и DXGI presentation одному HWND. Хост должен сохранить оконный режим, focus/input/DPI/resize/minimize, dock/reparent lifecycle; игровое управление display/settings не должно переключать fullscreen или перехватывать мышь редактора. Native child имеет ограничения stacking/clipping и композиции Qt; QPainter поверх D3D не появляется автоматически. |
| **Shared texture → GL через WGL_NV_DX_interop2** | Может вернуть финальное изображение в GL-композицию и облегчить GL overlays. Это расширение WGL/desktop OpenGL и driver capability, не переносимый Qt API и не гарантировано на каждой машине. Нужны проверки extension/adapter/device, shared texture formats, register/lock/unlock, synchronization, gamma/alpha/Y-flip/MSAA и resize. Синхронизация только картинки не даёт GL-overlay правильный D3D depth/ID. |
| **QOpenGLWidget / нынешний QGLWidget** | QOpenGLWidget обычно композитит через свой FBO; DXGI нельзя просто назначить ему backbuffer. QGLWidget тоже владеет GL context/presentation. Нужен отдельный native child либо интероп-копирование в принадлежащую GL texture/FBO. Нельзя считать стандартный framebuffer всегда 0. |
| **Offscreen/headless/capture** | У renderer есть borrowed output visitor (`R:Source/embedded_device.h:18–41`) и discard-frame фасада (`Game/OwnedRender/E1RendererFacade.h:83–97`); нужны отдельные staging/readback/PNG и поддержка без видимого UI. Сам renderer.ensure принимает HWND. Полностью без HWND в изученном API не доказано. |

`M:source/render/gl_dispatch.h:5–17` уже различает desktop GL и GLES/ANGLE. WGL-интероп не покрывает ANGLE/GLES автоматически; нужны capability negotiation и GL-резерв либо другой способ показа. Все предлагаемые D3D11 backend-варианты — **Windows-only**. Не следует обещать поддержку других ОС через одну замену QWidget.

### Состояние поставки и выполненная проверка

Старые README вводят в заблуждение, если читать их как текущую квитанцию: `R:README.md:29–63` описывает незавершённый P06; `OwnedRenderer/README.md:9–12` ещё пишет Win32, а `Game/OwnedRender/README.md:40–45` — исторический probe с 106 unresolved symbols. Между тем **фактические** `OwnedRenderer.vcxproj:3–9`, `Renderer.props:20–23` допускают Release/x64, solution связывает проект, `EI_Rebuild.vcxproj:64,606–617` подключает shader resources и библиотеку. Последующая история R содержит интеграции X64RUPD02/tree shadows/ICE, но сообщение merge не является build/link receipt.

В исследованных tracked документах не найден свежий отчёт полного standalone build+link для точного R. Исторические `build-local.py`/handoff scripts не являются рецептом x64 — это явно сказано в `R:README.md:32–36`. Нельзя на основании README повторять «x64 ещё не существует», но нельзя и объявить нынешний renderer независимой успешно слинкованной библиотекой.

В своей копии выполнено:

1. MSBuild 17.14.51, установленный VS2022 BuildTools v143; `/t:Build /p:Configuration=Release /p:Platform=x64`, только `snapshot/OwnedRenderer/OwnedRenderer.vcxproj`. Первая попытка остановилась на `DlssSdk.props:11`: в archive нет внешнего SDK. Это ожидаемая неполнота archive, не дефект renderer.
2. С явным **read-only** `DlssSdkRoot` на имеющийся SDK 310.9.16 в разрешённом EIRebuild64-каталоге началась C++-компиляция. Полный путь передан с завершающим `/`; все шесть требуемых SDK-входов проверены на наличие. Код/бинарники SDK не переносились в маппер и не изменялись.
3. Сборка завершилась **exit 1 / FAIL**: `Game/OwnedRender/E1VisualFrame.cpp:1` требует `../CorePerf.h`, `external/owned-renderer/Source/renderer_internal.h:14` — `../../../Game/CorePerf.h`. Эти приватные host-заголовки не входят в разрешённый renderer+adapter archive. На этой границе остановлено; заглушки/перенос приватного кода/правки проектов не делались. Есть также compiler warnings, в том числе C4245/C4702; до успешного library output/link probe дело не дошло.

Журналы: `work/rebuild64-renderer-study-20261105/ownedrenderer-build.log`, `ownedrenderer-sdk-build.log`, `ownedrenderer-sdk-build-console.log`. SHA-256 основного SDK-build log: `CFD0CDCBDF0D1F54C709D5914E2D1AF77630DC7CA6BAA7F5B85F2953753B0D7E`. TEMP/TMP процесса сборки указывали на собственный `work/.../tmp`. Сборка shader producer, генерация `.res`, полный EIRebuild64 и проверки с окном не выполнялись.

**Граница вывода:** независимая сборка данного разрешённого subset не состоялась. Это доказывает неполноту самостоятельного SDK/зависимость от host diagnostics, **не** несобираемость полного дерева EIRebuild64. Даже получение `OwnedRenderer.lib` означало бы только compile/archive; нужная следующая проверка — link минимального собственного host со всеми реальными producers/resources, без Game/* и retail hooks. После неё отдельно нужны lifecycle/render/capture и визуальная приёмка. Унаследованные `E1*Check.vcxproj` также не следует запускать вслепую: например, `E1DeviceCheck.vcxproj:3` всё ещё Win32, тогда как общий Renderer.props запрещает всё кроме x64.

## 5. Режимы перехода и предлагаемые этапы

| Режим | Что даёт | Что теряется / остаётся обязательным |
|---|---|---|
| **Полная замена GL** | Один итоговый backend; потенциально единая игровая картинка после полной интеграции. | До переноса всех строк редакторской таблицы теряются или меняют поведение выделение, depth/picking, overlays, кисти, атлас, export/minimap, previews и MCP capture/headless. Не разрешать запись через renderer. Самая широкая regression и отсутствие резерва при driver/device failure. Сейчас не рекомендуется. |
| **Второй режим / рядом с GL** | Ранний полезный просмотр, прямое сравнение одной камеры/состояния, сохранение действующего editing path и fallback. | Сначала D3D viewport только для просмотра; правки видны после явного snapshot refresh. Возможны двойные CPU/GPU caches, надо явно показывать revision/устаревание. Если export/snapshot пока GL, обозначать backend результата. Не называть GL export изображением Rebuild64. |

Документ, selection, Undo/Redo, dirty state и Save остаются **едиными и принадлежат мапперу**. Backend — потребитель render state. Переключение вида не должно переоткрывать карту, терять несохранённые изменения или переносить игровые settings/config. Текущий GL уже имеет собственное освещение/время/туман (`M:source/render/render_environment.cpp:145–200`); даже одинаковое имя часа не доказывает одинаковые shaders/presets.

### Порядок и критерии готовности

| Этап | Ожидаемый полный результат | Проверка, способная закрыть этап / остановить подход |
|---|---|---|
| **0 — этот отчёт, завершён** | Право/границы/матрица/варианты/план и локальный docs-коммит одного файла. | Ссылки на M/R, доступность публичного repo, ограниченная build-попытка, проверка staged diff. Никакой реализации. |
| **1 — право и независимый SDK, после отдельного решения** | Разрешённая GPLv3-поставка; автором/сопровождающим выделен x64 embedded SDK без private Game/*, с точным pin, notices, shaders/assets, build recipe и контрактом диагностики. Узкий C API, протокол snapshot/revision и lifecycle. | **Самый ранний стоп-критерий:** требуемые права не предоставлены либо минимальный MSVC host нельзя compile+link без игровых классов/хуков/нераскрытых producers. Это проверяется раньше UI и большого переноса. Никаких выдуманных bridge stubs. |
| **2 — первая демонстрация владельцу** | Отдельный Windows viewer/native viewport рядом с GL, одна выбранная карта и соседние участки; реальные материалы/пак, статика, персонаж с полным hierarchy/pose, вода, свет/туман/время. Read-only snapshot; одна камера. | Link + настоящая инициализация/кадр/capture, resize/DPI/minimize/close/device error. Парные ракурсы GL и Rebuild64 с фиксированными mounts/settings/hour/revision. Художественная приёмка владельца обязательна; code/build PASS её не заменяет. |
| **3 — несохранённые правки и живое обновление** | Один source of truth, ревизии terrain/material/object/pose, замена поколений, dirty/Undo/Redo, соседние карты, полный 16 384-slot atlas. | Поднять/опустить сектор, покрасить базовый/краевой/анимированный/водный тайл, заменить/переставить узел модели, Undo/Redo, сравнить D3D colour/depth/shadow/cover до Save. Проверить stale jobs/cancel/reload и память многократных изменений. **Первый технический тест editor-пригодности:** полный snapshot replacement на большой выбранной карте в ходе stroke; если задержка неприемлема, согласовать author-supported deltas до дальнейшего UI. |
| **4 — функции редактора в D3D и service paths** | ID/depth picking/selection/манипуляции/helpers/brush overlays/nav, модельный preview, topdown/minimap, MCP snapshot и invisible headless с прежним внешним контрактом. | Тонкая/alpha-tested геометрия, helper/hidden filters/rectangle, DPI, depth alignment, current-camera revision/timeout/cancel, export без смены состояния. Не скрывать неполную поддержку за переключателем. |
| **5 — решение о главном backend** | Матрица всех функций, принятые профильные карты/пакеты/driver modes, license-complete package. Решение владельца, оставлять ли GL. | Визуальная приёмка, workload frame-time и edit-latency/RAM/VRAM, повторные open/edit/Undo/capture/close. Только после этого обсуждать полную замену. |

Предлагается оценивать объём по этим результатам, не обещать срок на основании числа GL-файлов. Наиболее рискованные места: права на составной код/NGX, самостоятельный link, ресурсный приоритет и модельная иерархия, coherent invalidation при покраске/деформации, точность ID/depth overlays при AA/render-scale, lifecycle родного HWND и headless/capture, VRAM двух backend. Вынесение в процесс добавляет protocol/handle/device recovery и не сокращает объём semantic adapter.

**Память/производительность:** в этом этапе не измерялись; актуальных сравнительных цифр для Ataman+D3D нет. Исторические P-замеры относятся к другим кандидатам/сценариям, здесь не переносятся. Критерий `candidate/reference ≤1.2` в `R:Game/OwnedRender/README.md:79–83` — плановый gate старого сценария, не полученный результат. Для новых этапов заранее определить карту/число соседей/пак/разрешение/GPU, cold/warm, frame-time median/p95, p95 edit→image latency, peak private bytes/process-local VRAM, время replacement и выходов. Числа порогов интерактивности задаёт владелец после первого воспроизводимого baseline.

## 6. Вопросы, без которых нельзя выбрать реализацию

1. Нужен вид **как в конкретном EIRebuild64** с его современными материалами/настройками или как в установленной игре/моде владельца? Какой EXE/renderer pin и какие graphics settings будут визуальным эталоном?
2. Можно ли запросить через владельца у Ilufus совместимую лицензию и самостоятельную x64 embedded-поставку? Кто подтверждает права на изменения EIRebuild64 и решает вопрос DLSS/NGX для GPL-поставки?
3. Принимается ли первый read-only режим рядом с GL и сохранение GL как основного редактора? Нужен ли единый переключатель или одновременные два viewport? Для этапа 0 решение не предполагалось принятым.
4. Какая первая контрольная карта, мод/texture pack, какие соседи, персонажи/эффекты и условия освещения обязательны для демонстрации? Данные читаются из явно выбранного профиля, не копируются в дистрибутив.
5. Какие возможности обязательны уже в первом пользовательском D3D-режиме: live sculpt/paint, picking/манипуляции, export/minimap, model preview, MCP snapshot/headless? Допустим ли ручной refresh только для первоначального показа?
6. Какие машины/видеокарты поддерживать, обязателен ли ANGLE/WARP, какой предел edit→image latency/RAM/VRAM? Разрешён ли native child HWND как первый display path? Нужно ли сохранять платформы кроме Windows для основного GL-редактора?

Следующий шаг — решение владельца по целевому визуальному эталону, лицензии/SDK и границе первой демонстрации. Код и версия на этом этапе не меняются; полная цель «перевести графику на renderer Rebuild64» **не объявляется выполненной** завершением исследования.

## 7. Сохранность и локальные материалы

В итоговый docs-коммит входит только этот файл. `source/`, `tests/`, `VERSION`, `CHANGELOG.md`, `ROADMAP.md`, `Readme.md`, `ei_maper.pro`, чужие документы/переводы и незакоммиченные файлы не редактировались этой задачей. Никакого push.

Рабочие копии/компиляторные outputs оставлены только в `work/rebuild64-renderer-study-20261105/`. Удаление не выполнялось; там подготовлен `cleanup-plan.json` с точным ограничением на собственные archive/snapshot/tmp, сохранением отчёта/журналов и без касания соседних задач. Эта папка не является установленной сборкой или комплектом для передачи. Журналы служат локальным доказательством FAIL; исходники renderer, SDK и generated binaries в docs-коммит не включаются.
