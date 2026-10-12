# EI-PAINT-MODES-155 — все существующие режимы рисования

Дата: 2026-10-12. База main `b42eaf2984056cbc6e6ef472c6bbb78862004ec2`, программа и постоянная установка154. Исходная жалоба: Поверхности → Добавлять/удалять → Удалить поверхность, Покрытие/Water Still, сохранённые Auto/Smooth/Random/HD ON. Повторное поручение владельца расширяет исправление до всех существующих путей земли, покрытия, подводной земли и поверхностей. HD ALL256 и стыки остаются отложенными; их чужие кандидаты сохранены и исключены из этой сборки.

## Причины и исправления

- `modernMixedEdge()` учитывал только галочку и палитру, переносил Ground-only опцию в Surface/Delete и срабатывал защитный отказ. Getter и состояние чипов теперь учитывают слой, операцию, Modern/C и анимацию. Auto/Random/HD/rotation также effective только в поддерживаемом контексте. Сохранённые галочки остаются предпочтениями для возврата; отключённые опции объяснены tooltip. Raw HD preference отдельно управляет загрузкой банка, чтобы переключение слоя не теряло сам банк.
- Общая game-wide палитра очищала Underwater Ground без повторного заполнения. Это представление того же слоя земли под покрытием; оно теперь имеет полный доступный Ground набор, правильное выделение в видимой галерее и обновление применимости опций даже без смены mode ID.
- C переключал палитру без восстановления сохранённого Vertex target. Переключение обновляет mode visibility. C применяется к Ground/Overlay paint; Surface/Sculpt явно объясняют отсутствие ручной кисти. Смена режима при удержанном C возвращает Modern и не оставляет зависший Manual.
- Manual после Modern обновлял только baseline выбранных углов. Соседняя ячейка могла остаться изменённой семантически и требовать неизвестную смесь при Save. Baseline охватывает incident cells, сохраняя их фактические packed artwork и current identities; Undo остаётся пропорциональным footprint и восстанавливает current/baseline.
- Portable profile сохранял только исходные разрешённые слои материала и мог отклонять собственный результат после явного физического Manual размещения в другом слое. Профиль теперь учитывает реально активные размещения обоих слоёв и проверяется на usable analysis до публикации. Успешный reopen проверяется новым мазком, а не только чтением sectors.
- Автоматический выбор мог выбирать storage frames flipbook вместо его старта. Только старт остаётся selectable для Modern; исходные кадры защищены. Для Ground мазка по анимационному фону Mixed-edge guard сохраняется, caller применяет отдельный exact Modern fallback с явным статусом. Это не новая поддержка анимации самим Mixed-edge. Budget, source/type, generation и upload guards остаются действующими.
- Материалы без подтверждённого uniform source/runtime-класса или без найденного исходного тайла в настроенных ресурсах недоступны для выбора и имеют объяснение. Во время асинхронной hydration источники не объявляются отсутствующими до её завершения. Paint identity не заменяется придуманным материалом.

## Матрица UI и dispatch

Основные ID: Sculpt0, TilePaint1, OverlaySurface2, OverlayPaint3, OverlaySculpt4. UI «Поверхности» содержит три настоящих действия: рисовать существующее покрытие, добавить/удалить, рельеф поверхностей. В Surface CRUD нет Vertex и ручного C. Sculpt меняет вершины высоты; painting target к нему не применяется. World/neighbor, импорт высот, HD conversion и seam recovery не входят в это поручение.

| Ветка | Реальный сценарий матрицы | Проверки |
|---|---|---|
| Ground Modern | Yellow lichen, Vertex/Circle/drag; Grass Burnt, Tile/Square | Qt events, pixels, другой слой, один Undo/точная Redo |
| Ground на анимационном фоне | Физический Swamp через C, затем Yellow lichen с сохранённым Smooth ON | Exact Modern fallback, frames сохранены, pixels/Undo/Redo |
| Underwater Ground | Grass Burnt, Vertex/Circle/drag | Видимый underlay, Smooth не действует, Overlay сохраняется |
| Overlay Modern | Lava Orange, Swamp, Water Still; Tile/Square/drag, Lava Vertex | Ground сохраняется, pixels/Undo/Redo; animated placements используют record start |
| Surface Delete | Water Still/Square/size3, все stored options ON | Активная геометрия уменьшается, pixels, Ground сохранён, Undo/Redo |
| Surface Add | Существующая MPR material record, Water Still | Активная геометрия увеличивается, pixels, Ground сохранён, Undo/Redo |
| Ground/Overlay C | Press C → authored physical slot → drag → release C | Exact physical stroke, Modern/Vertex preference восстановлены |
| C через смену режима | C press в Ground → Surface → C release → Ground | Нет зависшего Manual, настройки сохранены |
| Ground Sculpt | Raise, Lower, Flatten, Smooth, Noise, BoundaryFill | Каждая ветвь events/pixels, Overlay сохранён, один Undo/Redo |
| Surface Sculpt | Те же шесть существующих инструментов | Каждая ветвь events/pixels, Ground сохранён, один Undo/Redo |
| Неполный source pool | Реальный отсутствующий source в настроенной копии | Disabled material button click не меняет brush; причина видна |
| Mixed-mode Save | Накопленная работа → обычный Save → unload → новое load | Exact committed sectors, Ground/Overlay identities, анимация, пригодная палитра и новый event stroke |

Все изменения матрицы проходят QApplication Qt press/move/release или key events через CView/COperationManager. Прямые `setTile`, `beginTileBrushGroup` и footprint override не используются для доказательства этой матрицы. UI переключения выполняются существующими сегментами, layer buttons и Surface combo. На входе только активируется editing context, как при выборе инструмента владельцем.

Native изображения: штатный `CView::draw`, отдельный desktop OpenGL FBO, angled orthographic/unlit terrain. Ground captures скрывают Overlay для проверки underlay; overlay captures рисуют покрытие. Shader linked и GL error0. Это реальный native GPU render, но не экран владельца, не Full Environment, игровая или художественная приёмка.

Анимационные кадры не изменяются во время рисования. Обычная существующая публикация приводит страницы к общей размерности и кодирует DXT1: исходные и опубликованные raster bytes могут различаться. Reopen проверяет frame count/order/content после этой штатной конверсии, runtime record start и возможность дальнейшей покраски. Upscale в этой операции не заявляется настоящим HD; задача HD ALL256 остаётся отдельной.

## Доказательства и статус

Адресные Windows widget tests: 9 PASS/0 FAIL/0 SKIP, `build-tests-155-widget-marker-final.log`; hidden windows до Polish, UI lock. Настоящий Windows/DPI font, чипы320px, сегменты и target transitions. Offscreen-font fallback с отсутствующим font directory не используется для font acceptance.

Связанный CPU регресс модели: 8 PASS/0 FAIL/0 SKIP, `build-tests-155-documents.log`: Manual baseline, legacy fieldless painting, Modern initialization, first Resolve, shared corner ordering и sparse Undo/Redo. Последующие изменения renderer/profile этих исходников модели не меняли.

Приватная матрица: `build-tests-155-matrix`, исходная zone1 MPR SHA-256 `1333A596DB880C9D73670F24F115BECB38BAB45D3D2B04BD9662CCD3DB23DA87`, исходный HDPack SHA-256 `79AE99A3BD47CDE0B36E73A774B20E2CFBABCDD9491AAD49DC1E1CF8D3376909`. Настройки, CWD, backup и clipboard backend изолированы, native clipboard attempts0. Source/reference и живая карта не записываются. Приватные MPR/RES/PNG не входят в Git или пакет.

Ранние diagnostic1 и offscreen widget font run не считаются PASS: harness не активировал editing, radius cast был неверен, font backend не имел Windows fonts. Diagnostic2 выявил реальные C/Save отказы. Diagnostic3 raw264/0 не закрывает задачу: поздний тест выявил непригодную fresh authoring palette. Diagnostic4 подтвердил исправленное fresh painting и все12 sculpt pixel paths; topology counter ошибочно считал inactive packed0, а требование byte-identical до/после обычной DXT1 конверсии было неверным. Эти ранние итоги не подменяют завершающую строгую матрицу.

Диагностическая завершающая матрица310 PASS/0 FAIL, `build-tests-155-matrix/palette-diagnostic5.log`, exit0, nativeAttempts0/unchanged1. Анимации8×256px и32×128px сохраняют frame order/content; вторая штатно публикуется256px через DXT1, без утверждения о настоящем HD. Чистая версия155 дополнительно проверит длинный120px drag и несколько brush centers. Окончательная Release/staging/установка фиксируются ниже. Ручная приёмка владельца остаётся отдельной; основная EI-RECOVERY-20261012 не закрывается этой версией.

Общий UI-check первоначального clean155 выявил связанный marker-selection регресс: выключенный родитель палитры ошибочно считался недоступным источником. Доступность теперь хранится у самого материала, явный выбор корректно выходит из marker; отдельная legacy underwater палитра сохраняет видимость при смене слоя. Дополненный widget9/0 и UI-check uild-tests-155-release/ui-marker-final.log PASS. nativeAttempts0; внешние clipboard sequence/formats изменились5516→5524 при нулевых native calls, повтор ради unchanged не выполнялся. Ранний snapshot a524 использовался как диагностический; последующая byte-equality сверка с окончательным git archive и пересборка описаны ниже.

Строгая clean21 long-drag matrix:318 PASS/4 FAIL — только multiple centers четырёх Overlay маршрутов. Addressed trace показал GPU hit/CPU footprint и отсутствие gesture interruption, но hover не доставлялся. Это дефект harness: CMainWindow включает mouseTracking на source/app/main_window.cpp:1836, отдельный CView теста его не включал. Harness приведён к production setup. Addressed Overlay/Suface Qt/GPU102 PASS/0 FAIL, uild-tests-155-matrix/palette-overlay-drag-tracking-final.log, один Undo/exact Redo и несколько центров Lava/Swamp/Water Still/Manual; nativeAttempts0/unchanged1. Изменение только в CLI harness, обычный UI программы не менялся. Прошедшие Ground/sculpt/Save/fresh authoring и CPU/widget не повторяются без новой причины.

## Принятая Release155

Source main `22fc6f6420da23a69360d99c211bba605275f162`. Полный tracked source1927/1927 byte-equal к этому `git archive`, receipt `build-tests-155-release/archive-source-receipt.txt`. При распаковке Windows tar исправлено только CP866-искажение имени русского DOCX в двух собственных Temp snapshots; ранний неполный receipt отозван и заменён строгой проверкой с ErrorActionPreference Stop.

Shadow `build-release-155-clean` создан заново для VERSION155. Обычные продуктовые исходники после main21 не менялись; после scoped CLI-harness commit нормализованы source bytes к окончательному архиву и пересобран этот модуль. Успешные неизменённые Ground/sculpt/Save и CPU проверки повторно не запускались. Build source находится `C:/Users/samol/AppData/Local/Temp/Ataman155-paint-modes-a52445fb`; имя описывает происхождение первого snapshot, окончательный SHA устанавливает receipt. Makefile использует `../source`, не dirty checkout.

Approved EXE SHA-256 `93A10CCC06EB2D9313B48AA360A6517D89DC312CC888056ACDE19390915B2203`. `--version`155 и обязательный UI-check на этих bytes PASS9635ms, nativeAttempts0/unchanged1, receipts `build-tests-155-release/version-approved.log`, `ui-approved.log`. Addressed corrected native102/0 выполнялся на byte-equivalent до нормализации harness source, EXE `EA573621D70C2D8E24570DB9CCA1C9A315FF2623B82C9EDDB722373875453EE1`; этот hash не выдаётся за окончательный EXE. Состав продукта и весь renderer/model код одинаковы; различается пересборка CLI validation module.

Передача выполнена ниже. Редактор за владельца не запускался; ручная приёмка остаётся отдельной.

## Установка и завершение программного этапа

Staging `build-staging-155-paint-modes-final`: raw/staged EXE совпали, deployed --version155 PASS. Fresh finalizer/internal validator PASS:135 prior/136 current/0 removed, removed stale files0, один текущий changelist, без приватных MPR/RES/PNG. Receipt `build-tests-155-release/finalizer.log`. Manifest SHA-256 `25144F21F84BB41FD3E5B1AEF1D4329CAEF0024B95E2344425150E512D3C8B5A`. Финализированный staging после этого не запускался и не менялся.

Host-level CIM guard подтвердил отсутствие mapper/runtime из постоянного target, guard повторён перед записью. Установка154→155 завершена в `artifacts/EI-Mapper-Ataman-2-first-test`:136 managed SHA проверены,6 replacements/1 addition/0 removals. Все23233 текущих неуправляемых файлов сохранены; before/after digest `CDF2FA24FFE3C18BA0193BBAD4C0F306953A85FD3E2C69F50E8BE1E5BFE55DCD`. Это текущий refreshed owner baseline, не старый счётчик154. Installed EXE SHA совпадает с approved/staged `93A10CCC06EB2D9313B48AA360A6517D89DC312CC888056ACDE19390915B2203`; installed manifest совпадает с finalized manifest. Exact receipt `build-tests-155-release/install-receipt.txt`.

Retained rollback154: `artifacts/rollback154-EI-PAINT-MODES-155-20261012`. Собственный старый rollback153 удалён только после resolved-path/no-link/exact-file-set/hash guards; проверены8 старых файлов. Все чужие rollback143–151 сохранены. Receipt `build-tests-155-release/rollback-cleanup-receipt.txt`. Finalizer на постоянной установке не запускался.

Программное исправление/достаточная матрица/передача155 завершены. Для дальнейшей ручной приёмки: открыть свою карту, рисовать Ground/Overlay/Underwater с сохранёнными опциями, добавить/удалить Surface, удержать/отпустить C, сделать длинный drag и Save/reopen. Матрица не заменяет экранную, игровую или художественную приёмку владельца. Основной recovery HD ALL256/стыков остаётся открытым и отложенным; их dirty candidates не интегрированы.
