# Ataman289 — EI-REBUILD64-SERVICE-289, 2026-11-07

## Результат и слияние

По прямому поручению владельца принят служебный этап из рабочего service-291.patch (база6d02bd4f6a1041d99b089290c2ada0f090e0618f) и вручную сведён в main поверх **c3b150d7c68c4f4c9fd93337852a6ea45a04a1af**, где уже есть288 и renderer-diagnostics. Рабочее имя291 не является номером релиза; версия **289**. Кадры zone1/zone7, слой/гизмо/панели/графы приняты владельцем до слияния.

- Общий **ABI10** сохраняет api.output/EirOutputRequest/EirOutput и live_values из288, добавляет api.query/depth и EirService.image_update/world_to_clip. Сохранены accepted-frame/raw-output, независимый preview renderer, lifetime/cancel/close из288 и причины выбора вида/постоянный индикатор. В worker раздельные очереди OutputJob и Query, общий generation. Capture ждёт serial служебного слоя.
- Новый vendor060 применяется после001–008/020/021. Source.lock abi10 и SHA патчей согласованы. Для patches/*.patch в .gitattributes закреплён LF: Git archive/Windows checkout раньше меняли SHA020/021 из-за CRLF. 060 не заменяет020/021. Хостовые изменения отдельными блоками editor_query.inc, AtamanReadiness, Session.querying, service lease и API initializer.
- Шейдеры/таблица и вид мира Rebuild64 не менялись: **37** source/table/prebuilt shader-ресурсов побайтно сверены с pinned recipe. Приватный код движка и навигации не копировался/не правился.
- Сохранены и исключены из commit/clean source snapshot чужие ROADMAP-строки, landscape.cpp, render_stall_trace.h, view.cpp (только чужая дельта), view_neighbor_load.cpp, mob_load_pipeline_check.cpp, QM, .mcp.json, отчёт277 и прочие untracked. Snapshot оригиналов и проверка дельт: work/rebuild64-service-291-20261107/integrate289/owner-*. Файлы/процессы кистей и персонажей не менялись, мосты не останавливались.

## Что работает через Rebuild64

Точный выбор объекта/точки земли/воды использует native ID/depth обычных world-fragment проходов: пять семибитных stencil-плоскостей без изменения shader, с прежним cutout/LOD/соседним клиппингом и сохранением shadow-bit. ID мира/MOB/соседа разделены, оригинальный nid соседа сохраняется, сосед readonly. Normal draw batching прежний; разделение по ID только при query. Запрос для исходной камеры Qt-события выполняется worker, stale generation/extent/camera отвергаются, порядок press/release/key сохраняется.

Служебный слой построен из immutable value snapshot в worker с проверкой native depth: метки/рамки/XYZ-гизмо/сетка/локаторы, cached пути и точки логики/ловушек, сценарий/CAM/переходы, магнит/ghost, размещение карт/швы, старый AI и navmesh fill/edges/narrow/islands/losses. Отложенная terrain-модель строится из уже загруженных MPR-байтов. gui.service.draw и GL-query context на native-пути исключены. QObject/документы/GL в worker не разыменовываются. Qt снимает COW-данные/позы; документы/Undo/форматы прежние, MOB/.grp и отдельный .nav64 сохранены. В CLogic/CActivationZone только const preview-getter.

Все прямые дочерние панели, включая поздние, над native HWND, без click-through. Легенда — прокручиваемая Qt-панель в свободной области без пересечения с другими панелями. Дипломатия/NPC focus остаются native; защита обычного выбора персонажей/stst8 сохраняется для отдельного этапа. Снимки MCP/независимые модели288 сохранены.

## Прежняя приёмка до слияния

Изолированный этап: zone1/zone7 по146 проверок, native Present291/311 без ошибок, gui.service.draw=0, Qt service snapshot max3,469/3,423мс; Qt gap открытия440,957/423,826мс. Это прежние результаты, не метрики нового staging289; параллельная нагрузка не гейт. Evidence в case-accept-zone1/7 и results.json собственной рабочей папки. Владелец просмотрел кадры и принял слой/панели/графы.

**Ограничение:** в разрешённых frozen-копиях нет готового .nav64/baker. Проверен immutable SNav64Document render-fixture на настоящей местности, не загрузка/импорт/запекание настоящего .nav64. Старый MOB/.grp-граф настоящий. Редкие варианты CAM/сценария/магнитных соединений отдельно не перебирались; это не полная матрица функций.

## Итоговая сборка и один прогон

**PASS, один процесс, 46.789с, exit0, realClose:true.** Обе карты последовательно: настоящие Qt-клики выбирают точный nid объекта/землю/readonly соседа; G/X/1/Escape сохраняет положение и Undo; панели/легенда без click-through/пересечений; MCP snapshot и api.output288; native model preview и закрытие; штатный MainWindow close. 570 PASS-событий включают повторные проверки MCP operation_status, это не570 разных сценариев. Native Present951, validation FAIL0. Denied writes/reads0, visible windows0, unexpected dialogs0; frozen SHA после процесса совпали.

Renderer journal содержит только startup requested:Rebuild64 latched:0 interrupted:0 и ожидаемый from:OpenGL to:Rebuild64 persist:0 action:startup. Неожиданных переходов нет. Кадры обеих карт (gizmo, AI/scenario/CAM, navmesh fixture, панели, native output и модель) просмотрены агентом. Замечание без исправления/повтора по указанию владельца: на zone7 перекрываются две подписи перехода/появления, как в принятом исходном кадре. На граничном кадре за правой границей местности виден однотонный фон; причина не исследовалась, шейдеры/вид мира не менялись.

gui.service.draw=0; max Qt snapshot5,204мс/total8,289мс, worker draw27,167мс. Общий max Qt gap2186,859мс включает прочие этапы/смену карты/preview; не объявляется паузой service draw или исправленным общим opening budget. Все цифры под параллельной нагрузкой справочные.

Staging: **build-staging-289-rebuild64-service-20261107**, финализатор/внутренний validator PASS. Состав против build-staging-288-rebuild64-offscreen-r2-20261107: **1365→1365, потерь0, добавлено: ничего**. --version: EI Mapper Endurance Edition289. Fresh QM:3410 finished/0 unfinished, новый query diagnostic переведён. После финализации запускалась только идентичная runtime-copy, все managed SHA staging после прогона прежние.

EXE SHA256: `b86c93c88d2f96f3a225fae12ab18d35786e020fd1e83c173811bbf3c33d0910`. DLL SHA256: `394eda25371ec790785b5349ecaed74dfdd4ca0fa7cb4261a1e668696c7e7c35`. QM SHA256: `b7e65aad9692940704b3e16e127dc960d9dede4f41d9df7f311b2fee71cab853`. Чистый сборочный tree: `5237b1a69d626f9ea9706993b1ef0c7ad2a0ba26`; после сборки менялись только итоговые ROADMAP/report, C++/TS/ресурсы прежние. Финальный commit с нормализованными Git-окончаниями строк семантически совпадает с этим snapshot; он не включает чужие правки.

Обёртка scripts/build_release.ps1 собрала mapper/MCP, но завершилась exit1 на известной проблеме кавычек длинного пути x86 .cmd. Те же штатные bridge/host .cmd отдельно запущены по реальному GetShortPathNameW (без subst/junction), оба exit0; их новые бинарники включены в staging. Это не скрытый PASS исходной обёртки: release-receipt.json/logs фиксируют оба этапа. При подготовке тестовой копии до запуска EXE отсутствовал каталог resource-profiles; создан в своей копии, новых запусков EXE из-за этого не было.


Evidence: work/rebuild64-service-291-20261107/integrate289/{staging-receipt,shader-preservation,acceptance-receipt}.json, logs/; case-integrated289/{process,exit}.json, events.jsonl, opening-spans.jsonl, presented.jsonl, logs/workflow.log и PNG. panels-own-composite — собственный native readback плюс QWidget::render реальных панелей, не снимок рабочего стола. Кадры проверяет агент, художественная приёмка владельца указана отдельно выше.

Копия финализированного staging запускается только внутри своей work-папки; исходный staging после финализации не запускался/не менялся. Настоящий Qt MainWindow/EXE, sendEvent/QAction, frozen-only ресурсы. Ровно один согласованный интеграционный прогон, обе карты последовательно в одном процессе; без матриц и новых кругов. Установка/старые EXE/живой мод не запускались/не открывались, computer-use/видимые окна/clipboard/subst/junction не использовались. Копии по новому указанию владельца оставлены, прежний cleanup-plan.json сохранён; удаления не предпринимались.

## Оставшиеся GL-пути

Точный перечень актуальных useLegacyViewport с файлами/строками: integrate289/remaining-legacy-calls.txt.

- Кисти/плитки/покраска, terrain-water/materials/atlas/info/transitions/profile/compact/cleanup, Base→HD, canvas resize, heightmap import, world-sculpt map selection: отдельный этап кистей/редактируемой местности.
- Обычный выбор units/appearance/stst8, texture-preview персонажа и particle/freeze-политики: отдельный этап персонажей. Дипломатия — узкое native-исключение.
- Явные параметры изображения/модель освещения OpenGL, диагностика теней и guard scene-light/shadow-политик: прежние GL-политики, не замена Config64/картинки Rebuild64.
- GL-preview нового объекта остаётся отдельным ещё не перенесённым маршрутом; обычный independent model preview288 и snapshots/minimaps не возвращены на GL этим слиянием.
- Ручной переключатель OpenGL остаётся запасным; был проверен в принятом изолированном этапе. В итоговом прогоне неожиданный переход на GL не разрешён.

Для query/service/panels shader-блокеров не найдено. Эти оставшиеся пути не объявляются невозможными без shader; общая миграция остаётся открыта, отдельные патчи кистей/персонажей не включались.
