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

Диагностическая завершающая матрица310 PASS/0 FAIL, `build-tests-155-matrix/palette-diagnostic5.log`, exit0, nativeAttempts0/unchanged1. Анимации8×256px и32×128px сохраняют frame order/content; вторая штатно публикуется256px через DXT1, без утверждения о настоящем HD. Чистая версия155 дополнительно проверит длинный120px drag и несколько brush centers. Source commit, clean Release/staging и guarded установка пока ожидаются. Ручная приёмка владельца остаётся отдельной; основная EI-RECOVERY-20261012 не закрывается этой версией.

Общий UI-check первоначального clean155 выявил связанный marker-selection регресс: выключенный родитель палитры ошибочно считался недоступным источником. Доступность теперь хранится у самого материала, явный выбор корректно выходит из marker; отдельная legacy underwater палитра сохраняет видимость при смене слоя. Дополненный widget9/0 и UI-check uild-tests-155-release/ui-marker-final.log PASS. nativeAttempts0; внешние clipboard sequence/formats изменились5516→5524 при нулевых native calls, повтор ради unchanged не выполнялся. Snapshot a524 после адресной правки является диагностическим и не выдаётся как финальный clean artifact. Следующий шаг — новый scoped source SHA и fresh archive Release с длинным drag.
