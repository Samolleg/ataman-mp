# EI-REBUILD64-TOOLS-286 — инструменты CView в Rebuild64

> **R1 НЕ ПРИНЯТА. Поправка R2:** case-fallback-* запускались с neighbours OFF и GL defer. Фразы ниже о настоящих native neighbours и крае в их сторону неверны для этих прогонов. Цифры1,5–2,5с получены без соседей. Прежний GL control — промежуточный. R1 DPR1.5 получен принудительным тестовым режимом; обычный продуктовый DPR1, явный QT_SCALE_FACTOR проверен в R2 отдельно. Старый текст сохранён для аудита; действующий результат — в разделе R2.

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

## R2 — повторная приёмка с соседями (2026-11-07)

**286 НЕ ПРИНЯТА, общая задача PARTIAL.** В перечисленных успешных прогонах A–D работают с настоящими соседями; быстрый object update не достигнут. Кроме того, обнаружен отдельный intermittent AV при принудительной подготовке canonical terrain в скрытом CView **до загрузки DLL**. Один GL-контроль на итоговых production objects завершился PASS, но чистого закрытия GL-регрессии при наличии этого AV не заявляю. Установки и push нет; картинку и видимое ощущение работы проверяет только владелец.

### Поправки к R1 и воспроизводимость

- `case-fallback-*` R1: native neighbours OFF во всех эпохах, GL neighbours deferred. Их «neighbours settled» не проверяло соседей; фразы «native neighbours настоящие» и «край в сторону native neighbours» отозваны. Числа1,5–2,5с относятся к отсутствующим соседям. Пустой фон за границей на `zone1-raw-a`/`zone1-selected` нельзя было выдавать за проверку соседнего мира.
- R1 `case-visual-world-r2`:5 соседей только на промежуточных EXE59219250 / DLL9dd9f992 и после первой правки;3350мс — единственное прежнее измерение с ними. Этот прогон не является финальной приёмкой286.
- R1 GL-контроли bb6d20d6/2af3adba/998226ae — промежуточные. R2 не использует их вместо нового контроля. Старый текст и числа выше оставлены историческими, без переписывания.
- Поимённый [инвентарь всех56 case-прогонов](../../work/rebuild64-tools-286-r2-20261107/run-neighbours.json) содержит наблюдённые native/GL counts и бинарные receipts. Пустое поле означает «не записано», а не PASS; исходные промежуточные проверки не повышены до финальных. Ниже перечислены действующие R2 прогоны.

База R2 — `ada760ebbd0988e3b5186cc980af91c1e4b69caf`, main. Отдельный CR-only коммит `2c2e813e77fb42147d646a8c85e461c368a97349`; VERSION остаётся286. AGENTS SHA256 `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7` актуален и не менялся.

Никакого `deferNeighborLoading(true)` и ручного присваивания world context в финальных проверках нет. Настройка `ui/viewportVisuals/neighborLandscapes=true` сохраняется **до** MainWindow. Использован штатный переключатель. Private external profile `deja-vu` загружается тем же `loadResourceProfiles()`, что приложение: исходные RES/Config read only, исходная раскладка с неизменными координатами, только пути zone1/zone7 направлены на собственные копии. Пустой game.exe — лишь неисполняемый signature marker, не копия игры. Base-карты не копировались. Исходные MPR/MOB после каждого прогона сверены по SHA256.

CView загрузил11 штатных GL background landscapes; его стадии terrain/object upload завершены. Native stream принимает5 на zone1 и4 на zone7. Перед **каждым native snapshot** `settled/capture` пишет `accepted/expected`, generation, GL_loaded и проверяет точное равенство; отсутствие соседей завершает тест FAIL.

|Прогон|Соседи|Результат|SHA256 скрытого harness EXE|
|---|---|---|---|
|stable-races-stream-final|5 zone1 / 4 zone7; OFF=0 только по явному запросу; GL11|D + весь поток, PASS|`13c5dd696e9858c6634af788786052c37c1b5895b6176496be025ded0b67711a`|
|accepted-neighbours-series-r2|5 / 4, ON; GL11|A + C, exit0,215 строк PASS|`c1070b10163fdb3c4ba568571f05834f3fa25bb48251aeb8257f69674f8e07aa`|
|mouse-release-clock-r2|5 / 4, ON; GL11|B, точный release, exit0,105 строк PASS|`12ad6c6a7591e19ff35900122e493d3bbb9252459682ba1a70647df838dd8e2a`|
|accepted-camera-r2|5 / 4, ON; GL11|камера при обновлении, exit0,51 строка PASS|`c1070b10163fdb3c4ba568571f05834f3fa25bb48251aeb8257f69674f8e07aa`|
|gl-traced-control-r2|native не запускался; GL ON, glReady; число GL отдельно не записано|GL control, exit0,14 строк PASS|`4b68c03736e9bb5de10d080243286337e947bc6773277d7a41a0b3fec97ae3b5`|
|accepted-gl-control-r2|native не запускался; GL ON, glReady|FAIL AV до DLL|`c1070b10163fdb3c4ba568571f05834f3fa25bb48251aeb8257f69674f8e07aa`|
|mouse-release-exact-r2|native не запускался; GL ON, glReady|FAIL AV до замера/DLL|`4b68c03736e9bb5de10d080243286337e947bc6773277d7a41a0b3fec97ae3b5`|

Все успешные native прогоны используют DLL SHA256 `33b2fd5b91da859aa1354e7191876191a63052eed938be62b4cfdfe3b4251179`. Это разные тестовые main с **одними итоговыми Release production objects**, а не старые mapper EXE. Основной Release EXE SHA256 `6fd4d5c3a81da57b947b5a630b9a434960eff72b232f4971542431c1023fbc5e`. Проверена идентичность исходников production core между архивами и неизменность object hashes; `main.o` заменён изоляцией, только lock/save translation units имеют test hooks для собственных путей. Qt archive `994f855c25b5c29a3bf70ecbf69791b6e66b444d`, host archive `1eacfab62a2c82ebcbcee31feccdd7c1468ca860`; версии harness archives и проверки — [release receipt](../../work/rebuild64-tools-286-r2-20261107/release-receipt.json), [production objects](../../work/rebuild64-tools-286-r2-20261107/production-objects.json).

### A — инструменты и выбор

На zone1 и zone7:8 реальных точек активных объектов сравниваются с тем же `CView::pickObject`, Shift, рамка; G→X→четыре mouse move→подтверждающий клик, Undo/Redo с точным восстановлением canonical MOB bytes, Delete/Undo, копирование/вставка/Undo вставки. Во время четырёх preview нет document reload; растёт принятый service_serial. Копирование использует только файл `case/tmp/copy_paste_buffer.json`, системный буфер обмена не используется. Save сравнивается побайтно с `buildValidatedCandidate`, затем строгий reopen и normalized projection; неизвестные lossless MOB records не подменяются простой сериализацией. Проверены оба формата ожиданий, включая zone7.

На каждой карте отдельно проверены точки реального GL neighbour object и соседнего рельефа за границей активной карты. **Результат — пустой выбор (0) в обоих видах.** Соседние объекты readonly и не становятся редактируемыми объектами активного MOB. Примеры zone1: samastrborder01(404,324), samt57z1(681,311), nafltr59(236,321), рельеф(-16,192)→(36,467). Все точки обеих карт — `neighbour_hits` в [измерениях](../../work/rebuild64-tools-286-r2-20261107/final-measurements.json). Это совпадение с GL, не positive native ID hit.

Служебный слой остаётся прежним CView GL depth + premultiplied RGBA readback; поверх GPU кадра его накладывает D3D host. Постоянного CPU readback мирового кадра нет. При960×640/DPR1 служебный image2,34МиБ; в финальной матрице CPU GL/service9,14–17,26мс, host composition submission0,0003–2,764мс, native frame call15,48–15,91мс (CPU замеры, не GPU timer). Shared texture не реализована; это измеренная цена выбранного CPU пути служебного слоя. В режиме записи всех PNG присутствует дополнительный readback **только диагностики**, эти прогоны не используются как цифры обычного отклика.

[zone1 GL service](../../work/rebuild64-tools-286-r2-20261107/pairs/zone1-after-release-GL-service.png) / [native input service](../../work/rebuild64-tools-286-r2-20261107/pairs/zone1-after-release-native-service.png), [zone7 GL service](../../work/rebuild64-tools-286-r2-20261107/pairs/zone7-after-release-GL-service.png) / [native input service](../../work/rebuild64-tools-286-r2-20261107/pairs/zone7-after-release-native-service.png). Это сверка наличия меток при одинаковых настройках, не подгонка вида renderer. Перекрытие использует GL geometry/depth; точные D3D cutout/LOD/ID/depth не предоставлены. GL содержит11 background maps против native5/4: метки дальних GL-only фонов могут оказаться над пустым native фоном. Нативную глубину и полное neighbour occlusion по этим снимкам не заявляю.

### B — от отпускания до реально показанного нового кадра

Начало — QPC в event filter входящего **Qt MouseButtonRelease** настоящего native widget, до CView handler. Конец — первый фактический `display->Present` целевого generation. Поэтому позднее UI-observed settled/readback не прибавлено к latency. Qt gap рассчитан по10мс heartbeat только в этом же QPC интервале. Private bytes — GetProcessMemoryInfo, VRAM — DXGI LOCAL CurrentUsage текущего процесса на RTX4070SUPER;10мс background sampling для пика продолжает работать при занятом Qt. Пики выборочные, не математический максимум между отсчётами. before/after берутся непосредственно вокруг действия; before/peak могут включать несколько микросекунд завершающего press перед release.

|Карта|Native neighbours|Release→Present,мс|Самый длинный Qt gap,мс|Private MiB до / пик / после|VRAM MiB до / пик / после|
|---|---:|---:|---:|---|---|
|zone1|5|2992.8|163.1|13004.7 / 13230.3 / 13046.8|8955.7 / 9259.9 / 9031.5|
|zone7|4|2982.7|341.9|14787.7 / 15097.5 / 14890.6|8724.9 / 9125.6 / 8933.0|

В полной A/C матрице34 подтверждения:2680,4–4961,4мс, максимальный Qt gap409,3мс (zone7 Insert). Это **не плавный интерфейс без пауз**. Во время native reload сохранён последний готовый кадр; камера в картинке ждёт нового мира. Отдельный реальный movable CCamera сохраняет исходную матрицу с ошибкой<0,001; W доставлен во время reload, изменение logical camera зафиксировано через326/517мс. Present после клавиатурного подтверждения3340,0/3496,1мс; Qt gap до395,7мс. Клавиши сохраняются, но визуальная заморозка на секунды остаётся.

**Что показывается:** opt-in trace снимает копию именно композиции, отданной фактическому Present, после допуска generation/visibility. В D сохранены и проверены **все1755 PNG**: всегда5/4 соседей при ON,0 только по двум явно запрошенным OFF кадрам; pending=0 и settled=true. Для всех1755 проверен известный магентовый fallback (`R>120,B>120,G<40`), максимум0 пикселей. Это проверка известной заглушки и ресурсной готовности на данном наборе карт, а не классификатор любой возможной ошибки текстур.

- [последний preview gen1/frame69](../../work/rebuild64-tools-286-r2-20261107/pairs/stream/stable-races-stream-final/present-69-gen-1.png) → [первый принятый gen2/frame70](../../work/rebuild64-tools-286-r2-20261107/pairs/stream/stable-races-stream-final/present-70-gen-2.png); между ними удержан предыдущий экранный кадр.
- [OFF frame264](../../work/rebuild64-tools-286-r2-20261107/pairs/stream/stable-races-stream-final/present-264-gen-15.png) — единственный допустимый случай отсутствия соседей, запрошенный переключателем.
- [zone7 после последней правки, native4](../../work/rebuild64-tools-286-r2-20261107/pairs/stream/stable-races-stream-final/present-1687-gen-22.png).

[Полный покадровый pixel/hash audit](../../work/rebuild64-tools-286-r2-20261107/presented-pixels-stable-races-stream-final.json), [30 выбранных кадров потока](../../work/rebuild64-tools-286-r2-20261107/selected-stream-stable-races-stream-final.json), [generation/visibility audit](../../work/rebuild64-tools-286-r2-20261107/case-stable-races-stream-final/stream-audit-r2.json). Выбранные PNG — из потока показа, не поздние финальные snapshots. Диагностическая запись PNG влияет на скорость, поэтому B/C измерены отдельно без неё на том же production code.

### C — двадцать подтверждённых правок подряд, zone1/native5

Смесь move/Undo/Redo,20 отдельных целевых generation9…28, каждый дождался нового Present с5 соседями. Ни падения, ни fallback в GL. Ресурсы каждые5 правок:

|Правок|Private MiB|VRAM MiB|Handles|Потоки|GDI|USER|
|---:|---:|---:|---:|---:|---:|---:|
|0|12988.5|8950.9|971|40|86|161|
|5|13171.7|9029.5|962|38|86|159|
|10|13519.5|9189.9|962|38|86|159|
|15|13380.5|9029.5|963|35|86|159|
|20|13399.3|9029.5|961|35|86|159|

Прирост0→20: **+410,8MiB private, +78,6MiB VRAM, handles−10, потоки−5, GDI0, USER−2**. Рост private не монотонный (пик checkpoint на10); причина остаточного прироста не установлена, отсутствие утечки по20 операциям не утверждается. После закрытия обеих карт: private5635,5MiB/VRAM1479,1MiB; все MPR/MOB handles текущего тестового процесса, включая исходные соседние карты, исчезли. Соседи при каждом подтверждении всё ещё перезагружаются целиком.

### D — найденные гонки и исправления хоста

Исходный диагностический `races-stream-before`:144 реально показанных кадра с0/частичными соседями,1 устаревший gen8 после запроса9. Исправлено: host ждёт полного neighbour stream уже на первом кадре, актуального service layer; ABI8 can_present берёт краткую блокировку worker через проверку generation/dirty/preview/visibility непосредственно на Present. Любой mob/loaded-map signal немедленно помечает input dirty. После долгого decode берутся свежие camera/service/size, устаревший документ не показывается. Принятые raw/composite targets копируются только после допуска.

Дополнительно полный pixel scan нашёл один магентовый кадр при OFF. Epoch fence без завершённого ready cycle, а также один readopt без этого ожидания не исправили его: промежуточные `final-races-stream-r3`, `epoch-races-stream-final`, `reload-races-stream-final`, `ordered/completed-toggle-stream-probe` имеют функциональный exit0, но **FAIL по показанному кадру**, финальными PASS не считаются. Исправление: текущий neighbour flag применяется перед публикацией replacement world; смена visibility заново принимает immutable документ на том же host device/display. Первый pending==0 draw ещё может использовать прежнюю material table, поэтому Present допускается лишь после **полного ready render/present/housekeeping цикла той же эпохи**. Ни маскировки пикселей, ни изменения renderer/shader/art нет.

Итоговый `stable-races-stream-final`: новая клавиатурная правка во время decode (позиция+2 подтверждена), два Undo подряд (точная ожидаемая позиция), Save копии во время update и strict reopen, переход GL→Rebuild во время update, OFF→ON, смена zone1→zone7 с сохранённым выбором Rebuild, Save/закрытие во время update. Перед сменой/закрытием правки сохранены в собственную копию; отказ от несохранённых правок не имитировался. Итоговые документы соответствуют последним правкам. В1755 Present: **0 incomplete-neighbour,0 stale-generation,0 stale-visibility,0 Present после GL/close**. После close все map handles закрыты;0 видимых HWND. Это не закрывает отдельно найденную нестабильность terrain setup ниже.

### E, DPI, GL-контроль и границы PASS

Два CR убраны **только из index blob** отдельным коммитом2c2e813e. Working view.cpp не записывался и сохраняет точный SHA256 `f6630ce5192d4db647481a90e521fad63fe25b298c78bd86fda8c2e702677de4`; `git diff --stat` показывает13 owner additions, `git ls-files --eol`: `i/lf w/crlf attr/text=auto`. Исходники CView/GL в R2 функционально не менялись, удалённое: ничего. Остальные protected source/QM/.mcp/report277 hashes сохранены; owner ROADMAP suffix сохранён отдельно от наших286 строк. В archive/commit owner строки не входят.

`source/app/main.cpp:4205` безусловно AA_DisableHighDpiScaling, до QApplication. В R2 с тем же флагом фактический DPR1. Небольшой изолированный Qt constructor probe: обычный=1, legacy QT_DEVICE_PIXEL_RATIO=1.5 всё ещё1, **явный QT_SCALE_FACTOR=1.5 даёт1.5 даже при AA_DisableHighDpiScaling**. Main не очищает этот env: неверно утверждать, что1.5 достижим только изменённым тестовым main. Но R1 AA_EnableHighDpiScaling14/17 и12/17 относятся к принудительному условию; это **не открытый штатный продуктовый критерий**. uiScalePercent, DPI flag и default OpenGL не изменены. Forced env остаётся отдельным неподдержанным режимом. [DPI proof](../../work/rebuild64-tools-286-r2-20261107/dpi-result.json).

GL контроль `gl-traced-control-r2` прошёл один раз на итоговых Release core objects: native DLL lazy, собственный GL readback, обычный DPR1, GL neighbours ON/glReady,0 visible HWND и закрытые map handles. [GL readback](../../work/rebuild64-tools-286-r2-20261107/pairs/final-gl-neighbours.png). До него `accepted-gl-control-r2` упал AV, а новый точный B сначала упал так же в `mouse-release-exact-r2`, до native selection. Поэтому это **не чистый общий GL PASS**: наблюдаемая нестабильность инициализации остаётся открытой.

Снятый stack: `CLandscape::projectPositions+7 → CMob::reprojectForCurrentTerrain → CView::ensureTerrainAutoCatalogReady → test open()`. Native DLL ещё не загружена. Исторический bit insideTestDll в fault-result этого R2 случая неверен: null GetModuleHandle трактовался диагностикой как main module; адреса и base0x400000 доказывают EXE. В новом handler этот ошибочный bit не используется. Символьная перелинковка тех же object files без strip сохранила `.text` побайтно; [факты сбоя](../../work/rebuild64-tools-286-r2-20261107/catalog-fault.json), [нормализованный stack](../../work/rebuild64-tools-286-r2-20261107/logs/history/CView-catalog-native-stack.txt). Все три функции побайтно совпадают с базой04548278: [source identity](../../work/rebuild64-tools-286-r2-20261107/catalog-source-identity.json). GDB diagnostic остановлен только для собственного проверенного child, результата PASS не имеет; две неудачи запуска GDB также не PASS. Причина неверного состояния CView не установлена, связь с обычным видимым запуском не доказана. Безопасного исправления в слое Rebuild64 host/embedding не найдено; общая GL/terrain ветвь не менялась догадкой и не отключалась ради зелёного контроля.

### G/H — сборка, QM и состав

Release собран из git archive без owner working changes. build_release.ps1 wrapper exit1 из-за прежнего x86 path normalization не считается PASS; штатные bridge/runtime host через8.3 дали0/0, Qt/MCP Release собраны. Runtime lib `e21003c22efd3999c598a9bb7e99743fc95ca1dbc19320179185f0af6ee4c2d9` и shader table `38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64` переиспользованы и побайтно неизменны. Внешние renderer checkouts, vendor patches, shaders и NVIDIA SDK не менялись.

**Почему QM отличается от285:** `scripts/build_release.ps1:151–153` запускает lrelease и получает актуальный QM. Затем285 `build.py snapshot():16–25` повторно извлёк git archive в source и заменил generated QM старым tracked QM. `packaging/stage_release.ps1:18–30` предпочёл этот текущий source-файл предыдущему комплекту; `refresh_package.py:9` обновлял затем только DLL. Поэтому285 содержит3292 сообщения, TS/286 —3341:49 переводов возвращаются, это не изменение текста переводов286. В R2 QM отдельно сгенерирован из archived TS прямо перед упаковкой и явно скопирован в staging; owner working QM не изменён/не коммитится. [QM counts/SHA](../../work/rebuild64-tools-286-r2-20261107/qm-result.json).

Свежий staging: `build-staging-286-rebuild64-tools-r2-20261107`. Finalizer exit0, против285: **1364→1364,0 потерь; добавленное — ничего**. Изменены BUILD_INFO.txt, CHANGELOG.md, EIMapperEndurance.exe, EIMapperMcp.exe, Readme.md, VERSION, ataman_rebuild64.dll, runtime/EIMapperEnduranceJmvBridge.plg, runtime/EIMapperEnduranceRuntimeHost.exe, translations/ei_mapper_ru.qm. Raw/staged --version286/exit0 и EXE SHA совпали; DLL SHA `33b2fd5b91da859aa1354e7191876191a63052eed938be62b4cfdfe3b4251179`, QM SHA `ff8e3c30a16dca8910b88340abbdc4228504735e0e776801093ae4b94f31e418`. [Package receipt](../../work/rebuild64-tools-286-r2-20261107/package-receipt.json). Прежний staging286 не заменяется и не запускается. Полный scoped SHA — в [postcommit receipt](../../work/rebuild64-tools-286-r2-20261107/postcommit-receipt.json) и итоговом сообщении. STOP BEFORE INSTALLATION, no push.

### Открыто и следующий шаг

- **Быстрый объектный update не выполнен:** весь world и5/4 соседей перезагружается после подтверждения. Drag использует только service preview,0 document reload на движения; после подтверждения удерживается старый кадр примерно3–5с, визуальная камера ждёт. Нужен вопрос Илье о штатном incremental object update с согласованной generation/material lifetime; самодельного renderer object ingress нет.
- **Стабильность скрытого CView terrain setup/GL control не закрыта:** два AV до DLL, один контроль PASS; известен стек, причина не установлена. Это отдельная необходимая подзадача с этим evidence, не SKIP/PASS. Обычная видимая приёмка не проводилась.
- Native ID/depth и точные cutout/LOD/occlusion, включая фоновые GL11 против native5/4, остаются вопросом Илье. Фоновая карта readonly: попадание пустое, как GL; редактировать соседний MOB этим кликом нельзя.
- Кисти рельефа требуют OpenGL с прежним понятным сообщением; персонажи/stst8 отдельно. Быстрое обновление секторов не делалось. R1 crash/autosave recovery — историческое доказательство без соседей, в R2 заново не запускалось. Форматы/Save/MCP/autosave code в R2 не менялись.
- Полного PASS/принятия286 нет. Установка, push, видимые окна, desktop capture отсутствуют. Проверка картинки/плавности/сравнения с игрой остаётся **только владельцу**. Очистка собственных промежуточных case payloads и оставшиеся приватные копии — [cleanup receipt](../../work/rebuild64-tools-286-r2-20261107/cleanup-receipt.json) / [cleanup plan](../../work/rebuild64-tools-286-r2-20261107/cleanup-plan.json).

I выполнен частично: после покадрового pixel PASS удалены13977 избыточных readback/duplicate PNG, освобождено11139767934 байта (10,38GiB);30 выбранных кадров, пары и журналы сохранены. Общего PASS из-за CView setup AV нет, поэтому полная условная очистка case-папок и копий карт пока не выполнена:58 приватных payloads/122380623 байта перечислены с точными SHA в cleanup-plan. Заблокированных файлов нет. Финализатор на установке не запускался; ни один путь владельца не участвовал в очистке.
