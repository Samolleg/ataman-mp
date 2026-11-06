# EI-REBUILD64-TOOLS-286 — инструменты CView в Rebuild64

Начато 2026-11-06; финальные проверки 2026-11-07. main, VERSION 286. **PARTIAL: реализованная часть проверена, строгие критерии ниже открыты. STOP BEFORE INSTALLATION, no push.**

## База, границы и изменения

Первоначальный HEAD `0454827835abf4a713b46e9ef4225fce7d566cce` совпал. Параллельный `1ad21c2bfec448675e63af1d1f346aa4c8da6457` меняет только три документа установки284/285, сохранён; программного влияния нет. AGENTS SHA256 `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7` неизменён. Следующий свободный номер286. Прочитаны план283§4, отчёт285 и действующие договоры. Навыки приватного движка отсутствуют: использованы Ataman и закреплённые public embedded adapters; приватный код не копировался.

Мышь/клавиатура/ShortcutOverride/wheel/context доставляются исходными Qt-событиями в CView с nativeVirtualKey и модификаторами. Отдельная cameraInput нового вида удалена. Документы, операции, привязки/ограничения, Undo, Save, MCP, автосохранение и recovery journal принадлежат прежнему приложению. DLL не пишет MPR/MOB/RES. Панели — прежние Qt-виджеты. OpenGL по умолчанию и uiScalePercent не изменены.

Перед новым выбором мышью при ещё неготовом подтверждённом кадре теперь выдаётся сообщение ожидания и клик не выбирает по устаревшей картинке. Undo/Save и завершение pending transform остаются у CView. Адресный финальный `layers-fence-final` exit0 проверил эту защиту реальными Qt-событиями, последующее снятие ожидания и unit/OpenGL предупреждение. Эти две защиты добавлены после полной матрицы; проверена именно затронутая ветка, неизменённые успешные сценарии не повторялись.

При выбранном OpenGL пользовательские изменения: **ничего**; удалённые возможности: **ничего**. В коде выделение/helpers и ID submission извлечены в общие функции, без другой GL policy. Новый service branch включается только при специальном захвате слоя. Ни одного vendor/render/shader патча не добавлено; renderer image не подгонялся.

## Работа инструментов и обновление

Привязки и ограничения используют исходную CView/operationmanager логику; исчерпывающий отдельный набор для каждой привязки и кликабельных 3D-ручек не выполнен. Проверены осевой G/X mouse preview и числовые операции, новая самостоятельная гизмо не создавалась.

Проверены реальные Qt-события клика/Shift/рамки, G/X с числом и мышью, R/Z, масштаб, Delete/Undo, Qt Undo/Redo actions, копирование/вставка/отмена и подтверждение вставки, сохранение копии и строгий reopen, побайтное совпадение MOB с такими же GL-правками, переключение вида с выделением, смена zone1→zone7, камера W/средняя кнопка/колесо. Группа дерева имеет штатное меню с2 действиями; отдельного меню у обычного объекта/самого CView исходно нет. Новое меню не придумано.

**Обновление мира:** во время pending transform/нажатой мыши используется служебный предпросмотр выделения, без полной загрузки на mouse move. После подтверждения сериализуется canonical MOB и публикуется полный снимок. На том же D3D устройстве пересоздаётся встроенный renderer; последний принятый кадр удерживается до готовности мира/соседей. Это не отдельный процесс и не быстрый object-delta. Возможна заметная пауза камеры/старый кадр с сообщением обновления. Скрытые узлы передаются отдельными immutable admission facts: Save-формат не меняется; смена видимых MOB не смешивает ordinal/ID разных поколений.

Попытка `visual_retire_detached/visual_seed` не меняла immutable main-source geometry и удалена. Старые замеры service serial не доказывали world update и отвергнуты. Повторная публикация без restart давала пурпурные материалы — также отвергнута. Финальная проверка читает **сырой native world без служебного слоя**, проверяет ограниченную область изменений, отсутствие missing-material magenta и возврат после Undo; PNG просмотрены. У Undo сначала отменяется отдельная запись снятия выделения, затем само перемещение, как в GL.

Требуют OpenGL: interactive terrain/tile/overlay/sculpt brushes и их секторное обновление; персонажи/appearance и stst8 для достоверного отображения. Кисть блокируется до stroke с русским сообщением, без смены вида/изменения terrain/MOB. При выборе unit/stst8 выдаётся предупреждение; document edits не получают другого пути сохранения. Круг неработающей кисти не показывается. Эти отдельные этапы не реализованы.

## Служебный слой и ограничения глубины

Способ: **общий скрытый GL depth/ID CView → premultiplied RGBA8 readback → текстура/alpha blend в D3D11 хосте** с восстановлением context state. Через CPU постоянно копируется только служебный RGBA, сам world frame не читается. Raw world/captured composite копируются GPU→GPU; CPU world readback выполняется только по запросу снимка. Два host overlay shader entry не пересобирают таблицу renderer shaders.

Общие `drawEditorObjectServices`, `drawEditorPickIds` и `drawTestOverlaysOpenGl` сохраняют выделение, рамку, сетку, штатные operation/helpers, logic/scenario/AI/CAM/entrance/diagonal и прочие CView-метки при прежних настройках. Новых настроек нет. Полный GL scene/lighting/shadow pass для слоя отвергнут как дорогой.

Глубина приблизительная: GL query geometry/alpha и terrain. Native cutouts, LOD, displacement, вода и background-object depth могут отличаться. В основном harness GL neighbours отложены, native neighbours настоящие: **positive hit по объекту соседней карты и полная occlusion её объектов не подтверждены**. Наличие каждого редкого типа метки на всех настройках отдельно не исчерпано; проверены общий путь и сохранённые пары слоя. Метки, которые GL сам рисует поверх мира, сохраняют этот режим. Точного native depth/ID нет, самодельного прохода внутри renderer нет.

Адресный `layers-control` exit0 проверил собственный readback **обычного GL world с сеткой**, новый composite и предупреждение unit/OpenGL,0 visible HWND. [Штатный GL](../../work/rebuild64-tools-286-20261106/case-layers-control/ordinary-gl-grid.png)/[новый вид](../../work/rebuild64-tools-286-20261106/case-layers-control/native-grid.png). Диагональный мусор сетки существует и в GL: `CSector::drawTileGrid` использует старый stride `sizeof(SVertexData)` для буфера `STerrainVertexData`. Слой наследует его; GL/sector.cpp не исправлялись по границе поручения. Это отложенный существующий дефект, не гарантия правильной формы сетки. Пары одних прозрачных service readbacks подтверждают доставку общего слоя, а не независимую корректность его геометрии.

25 разных принятых service serials на карту, а не25 queued callbacks. CPU wall, не GPU timestamps. Только RTX4070SUPER,960×640 logical; native960×640/1440×960. Значения коротких прогонов при существующей нагрузке владельца приведены ниже без обещания60fps/4K. Стоимость CPU readback допустима для проверенных размеров, отдельные p95 пики сохраняются в данных.

```text
PICK_SUMMARY zone1 samples=17 logical_mismatch=0 legacy_GL_mismatch=0
FOOTPRINT zone1 id=1601 model=samspr04 gl_physical_px=209 native_change_px=1074 intersection_px=171 noise_px=58 (includes native shadows/contact; not ID accuracy)
RESPONSE zone1 drag_ms=30.2247,27.4295,37.841,28.6286, release_ms=1522.47 confirmed_document_updates=1 serial_start=175
MEASURE zone1 n=25 service_median_ms=14.7222 service_p95_ms=15.7596 composite_median_ms=0.1986 composite_p95_ms=0.2348 frame_median_ms=16.3014 frame_p95_ms=16.5157
PICK_SUMMARY zone7 samples=17 logical_mismatch=0 legacy_GL_mismatch=0
FOOTPRINT zone7 id=4098 model=samt57z1 gl_physical_px=24 native_change_px=2185 intersection_px=9 noise_px=303 (includes native shadows/contact; not ID accuracy)
RESPONSE zone7 drag_ms=27.9644,22.4928,28.6545,25.7446, release_ms=1870.58 confirmed_document_updates=1 serial_start=537
MEASURE zone7 n=25 service_median_ms=36.648 service_p95_ms=80.5282 composite_median_ms=0.2119 composite_p95_ms=1.4139 frame_median_ms=14.5209 frame_p95_ms=16.4921
PICK_SUMMARY zone1 samples=17 logical_mismatch=0 legacy_GL_mismatch=14
FOOTPRINT zone1 id=1601 model=samspr04 gl_physical_px=482 native_change_px=2343 intersection_px=399 noise_px=119 (includes native shadows/contact; not ID accuracy)
RESPONSE zone1 drag_ms=33.0909,31.1585,31.9226,26.4159, release_ms=1852.4 confirmed_document_updates=1 serial_start=188
MEASURE zone1 n=25 service_median_ms=22.9328 service_p95_ms=27.3953 composite_median_ms=0.4045 composite_p95_ms=0.4698 frame_median_ms=15.7392 frame_p95_ms=16.4329
PICK_SUMMARY zone7 samples=17 logical_mismatch=0 legacy_GL_mismatch=12
FOOTPRINT zone7 id=4098 model=samt57z1 gl_physical_px=59 native_change_px=5213 intersection_px=43 noise_px=612 (includes native shadows/contact; not ID accuracy)
RESPONSE zone7 drag_ms=44.3293,42.1361,41.2766,41.6275, release_ms=2458.94 confirmed_document_updates=1 serial_start=688
MEASURE zone7 n=25 service_median_ms=19.1908 service_p95_ms=20.3803 composite_median_ms=0.3987 composite_p95_ms=4.0869 frame_median_ms=15.4673 frame_p95_ms=16.8353
VISUAL_TARGET id=56 model=stbuho7
VISUAL_PREVIEW unchanged_native_pixels_check changed=34 document_updates=0
VISUAL_COMMIT changed_native_pixels=8450 ms=1946
VISUAL_UNDO changed_native_pixels=574
PICK_SUMMARY zone1 samples=17 logical_mismatch=0 legacy_GL_mismatch=0
FOOTPRINT zone1 id=1601 model=samspr04 gl_physical_px=209 native_change_px=1089 intersection_px=171 noise_px=57 (includes native shadows/contact; not ID accuracy)
RESPONSE zone1 drag_ms=32.6781,24.3505,23.6414,30.5892, release_ms=1504.58 confirmed_document_updates=1 serial_start=197
MEASURE zone1 n=25 service_median_ms=14.5076 service_p95_ms=16.7593 composite_median_ms=0.2088 composite_p95_ms=0.2345 frame_median_ms=16.2616 frame_p95_ms=16.4698
PICK_SUMMARY zone1 samples=17 logical_mismatch=0 legacy_GL_mismatch=0
FOOTPRINT zone1 id=1601 model=samspr04 gl_physical_px=209 native_change_px=1208 intersection_px=171 noise_px=54 (includes native shadows/contact; not ID accuracy)
```

Отдельный сырой дом ID56/stbuho7, DPR1: pending22 изменившихся pixels/0 document updates; подтверждение3350мс,15729 изменённых pixels; Undo12218 pixels относительно baseline с временным шумом, bytes восстановлены точно. [До](../../work/rebuild64-tools-286-20261106/case-visual-world-r2/pose-baseline-b.png), [перемещён](../../work/rebuild64-tools-286-20261106/case-visual-world-r2/pose-moved-deselected.png), [Undo](../../work/rebuild64-tools-286-20261106/case-visual-world-r2/pose-undo.png). Тест окончательного подтверждения ждёт новый document generation, ready и settled neighbours; это не только overlay serial.

[Итоговые логи/замеры](../../work/rebuild64-tools-286-20261106/final-test-results.json), [разности прозрачных пар](../../work/rebuild64-tools-286-20261106/final-service-pairs.json), [SHA снимков](../../work/rebuild64-tools-286-20261106/final-captures.json). [zone1 GL layer](../../work/rebuild64-tools-286-20261106/case-fallback-native-final/zone1-selected-gl-service.png)/[native layer](../../work/rebuild64-tools-286-20261106/case-fallback-native-final/zone1-selected-native-service.png); [zone7 DPR1.5 composite](../../work/rebuild64-tools-286-20261106/case-fallback-dpr150-final/zone7-selected.png). Пары сравнивают наличие меток, не художественный вид мира.

## Попадания и DPR

Скрытый CView синхронизирован с logical rectangle нового вида, физический native target учитывает DPR. Отложенный Qt resize синхронизирован до ввода/frameCommitted; только DPR не сбрасывает projection/far plane. Возврат в GL восстанавливает исходные physical extents.

На zone1/zone7 по17 точек (12 объектов,4 угла и край в сторону native neighbours), оба DPR: **0/34 расхождений с тем же GL query при logical extent**. DPR1 совпадает и с исходным GL. DPR1.5 исходный GL запрашивает1440×960 при logical мыши960×640:14/17 расхождений zone1 и12/17 zone7 с корректным logical query. Пример(130,517): legacy1840, logical/native1849. Тест отдельно сохраняет legacy результат; это **не PASS строгой эквивалентности неизменённому GL**. Исправление исходного GL запрещено поручением, поэтому строгий критерий оставлен открытым.

Геометрические facts до правок: native1808/3084 static candidates zone1/zone7; max component position delta0.000083923м/0.000290871м, quaternion/semantic strength/grace/tall delta0. Неизменённые native позы не заменяются GL позами. Raw footprint до/после скрытия объекта измеряется после настоящего нового снимка; числа выше включают native contact/shadows и шум, это не ID mask. Точные пиксельные cutout/LOD/смещения и отнесение каждого промаха к их причине **не измерены достоверно** без native ID/depth. Ранние footprint из неработавшего seed-update не используются.

Вопросы Илье (не отправлялись): public обновление/замена main-source object poses/visibility без перезагрузки immutable terrain/neighbours; generation-coherent native ID+depth, соответствие ID исходному MOB и соглашения output resolution/jitter/LOD/cutout; границы lifetime при повторной публикации материалов. Renderer patches ради собственного picking не делались.

## Сохранность, AV и изоляция

Test-only forwarding DLL загружает настоящую DLL и по своему разрешающему файлу вызывает настоящий AV. Exception0xC0000005/3221225477, address внутри тестовой DLL. Unhandled filter собственного процесса только подавляет WER/UI и пишет квитанцию. Отдельная live-session recovery marker сохраняется до корректного teardown. Следующий запуск штатно защёлкивает GL/manual retry.

Сценарий: объект в новом виде+5 → production `performAutosave` → ещё одна завершённая правка+1 → AV → production discover/validate/open autosave. Восстановлены точно байты снимка; потеряна1 правка после него. В общем случае число потерянных правок не ограничено: теряется всё после последнего успешного autosave, pending operation может его откладывать. Containment не заявляется. [AV receipt](../../work/rebuild64-tools-286-20261106/case-fallback-crash-final/fault-result.json), [recovery log](../../work/rebuild64-tools-286-20261106/case-fallback-crash-final/recover.log). Fault DLL в комплект не включается.

Системный clipboard не использован: штатный объектный `TEMP/copy_paste_buffer.json` направлен в свой TEMP. AppData/known folders/settings/locks/discovery/backups/temp изолированы. HWND только скрытые, в финальных сценариях0 visible. Игровые источники read-only с SHA readback; Base-карты не открывались. Чужие bridges/headless/Codex процессы, установленный mapper и старые EXE не запускались/не трогались. Сами hidden checks не доказывают видимую композицию панелей, плавность и художественную/игровую приёмку: это проверяет **только владелец**.

## Проверки, сборка и передача

- fallback-native-final: exit 0, PASS assertions/log rows 164 (footer включён, если есть).
- fallback-dpr150-final: exit 0, PASS assertions/log rows 166 (footer включён, если есть).
- fallback-visual150-final: exit 0, PASS assertions/log rows 26 (footer включён, если есть).
- fallback-context-final: exit 0, PASS assertions/log rows 14 (footer включён, если есть).
- fallback-extras-final: exit 0, PASS assertions/log rows 91 (footer включён, если есть).
- fallback-crash-final: exit 3221225477, PASS assertions/log rows 46 (footer включён, если есть).
- recovery: exit 0, PASS assertions/log rows 11 (footer включён, если есть).
- raw-world-DPR1: exit 0, PASS assertions/log rows 25 (footer включён, если есть).

Первоначальные failed эксперименты сохранены вW и не считаются PASS: seed updates, materials без renderer restart, ошибочное ожидание меню обычного объекта, умножение системного DPR2 до3, deferred projection/resize, лишний undo selection. Для GL выполнен контроль со свежим286 binary и исходным GL body045: текущий GL отличается3/921600px, mean RGB0.0000271267/255, max15. Исторический285 PNG отличается80845px даже от исходного GL body при текущем harness; причина shadow/input/cache состояния не локализована, вид не менялся ради совпадения. [GL comparison](../../work/rebuild64-tools-286-20261106/gl-original-body-comparison.json). Старый EXE не запускался.

Release построен из `git archive` отдельного tree, исключающего все owner uncommitted source/QM/ROADMAP строки; необходимые наши изменения view.cpp применены отдельно поверх HEAD. Wrapper build_release.ps1 имел известный отказ x86 path normalization; сам по себе не PASS. Qt/MCP собраны; прямые штатные x86 bridge/runtime-host через8.3 завершились0/0. DLL собрана по прежнему recipe, runtime lib `e21003c22efd3999c598a9bb7e99743fc95ca1dbc19320179185f0af6ee4c2d9`, shader table `38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64` неизменны. Lock/vendor patches не менялись, NVIDIA SDK/imports не возвращены. [Release receipt](../../work/rebuild64-tools-286-20261106/release-receipt.json), [renderer audit](../../work/rebuild64-tools-286-20261106/renderer-audit.json).

Свежий staging: `build-staging-286-rebuild64-tools-20261106`. Finalizer exit0; против285:1364→1364, **0 потерь, добавленное: ничего**. Изменённые программные/metadata пути: BUILD_INFO.txt, CHANGELOG.md, EIMapperEndurance.exe, EIMapperMcp.exe, Readme.md, VERSION, ataman_rebuild64.dll, runtime/EIMapperEnduranceJmvBridge.plg, runtime/EIMapperEnduranceRuntimeHost.exe, translations/ei_mapper_ru.qm. Raw/staged `--version`:286,exit0, одинаковый EXE SHA256 `6c2298382812268b0cef52d9b5ff3abed68e9a3679339cce68e78fc911549e8e`. DLL SHA256 `eb93b56690428cfadddf545e3684827241675b250755dae320e82e92fa7765ac`. Release archive tree `c17e0fe81c18abc14f989162921e0107f225d202`; последующие изменения только в evidence/ROADMAP. [Package receipt](../../work/rebuild64-tools-286-20261106/package-receipt.json). Постоянная установка не изменялась; финализатор запускался только на новой staging-папке.

Owner строки view.cpp сохранены в working copy и исключены из archive/commit; другие protected hashes unchanged: landscape.cpp, render_stall_trace.h, view_neighbor_load.cpp, mob_load_pipeline_check.cpp, QM, .mcp.json, отчёт277. ROADMAP обновлён только своим пунктом, включая параллельную installation запись. [Сохранность](../../work/rebuild64-tools-286-20261106/preservation-result.json). subst пуст, junction/subst не создавались. Сохранён существующий дефект сетки (отдельный следующий вопрос; данные не меняет). Полного PASS основной задачи нет, поэтому приватные map payloads перечислены с точными SHA/размерами и дальнейшим удалением в [cleanup-plan](../../work/rebuild64-tools-286-20261106/cleanup-plan.json).

Открыто: задержка подтверждения в секунды; строгий DPR1.5 GL equality; native ID/depth/cutout/LOD и positive neighbour-object selection/полная occlusion; видимая приёмка владельца. Установка и push не выполнялись. Основная задача не закрыта полностью.
