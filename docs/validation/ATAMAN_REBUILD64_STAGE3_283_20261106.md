# Ataman283 — EI-REBUILD64-RENDERER-STAGE3-283

2026-11-06. **Реализация и изолированные проверки выполнены; строгий критерий
контраста картинки выполнен не полностью. STOP BEFORE INSTALLATION, no push.**
OpenGL остаётся default и владельцем редактирования. VERSION283.

## База, границы, источник сборки

Директор принял main `0c08f35babb80668e6b41fd435bfa36a073d4922`, включая
packaging795aae2b. Предшествующая остановка по первоначальному guard снята его
явным решением; исходный объём частей1–4 сохранён. AGENTS SHA256
`2A2F58EF34F63D6C5D67D165D0B809B046D0A519181119D22EA8FDD8781FE3B7` не изменился.
Прочитаны действующие договоры и отчёт282. Номер283 был свободен.

W=`work/rebuild64-renderer-stage3-283-20261106/`.
Только RTX4070SUPER этой машины. Снимки — собственные скрытые процессы и
readback принятой текстуры DLL; desktop/computer-use/clipboard не использовались.
Ни установленная282, ни старые mapper EXE, ни игра не запускались этой работой.
Настройки/AppData, ProgramData locks и процессы владельца не изменялись.
Игровые данные, включая Base, не изменялись. Для изображений читались только
DejaVu и зависимости его профиля; нужные MPR/MOB копировались в W.
Навыков evil-islands-modding/private-engine-reference нет в доступном каталоге;
приватный код движка не использовался. В Git идут собственный host/C ABI и
reviewable-патч; вендорные исходники в репозиторий не копировались.

## 1. Причины картинки и поправки

### Gamma, экспозиция, выход

Гипотеза общей ошибочной gamma→linear передачи проверена первой и не
подтвердилась как объяснение всего кадра. `native_shader.h::calculateWorldLighting`
потребляет нормированные авторские цвета; `renderer_sky.cpp` сам вызывает
`sky_linear` для солнца и game sky colour. Конвертировать весь cameraSnapshot
повторно было бы неверно. Контроль ambient+fog с pow2,2 оставлял фон
(153,255,255), исходный фон был (175,255,255).

У однотонного фона установлена конкретная цепочка: captured FOGCOLOR →
`fog_lit_white=max(ambient,sun)` → `fog_lit_haze_color` с brightness1 днём и
ограничением каналов → `SkyFrameGpu::horizon` → `skyFinal`, возвращающий один
horizon ниже физического горизонта. GL на том же экране рисует опущенный
текстурированный цилиндр. Подъём взгляда выявлял настоящие объёмные облака:
sky работал, но находился выше видимой области. Это не доказательство
отсутствующего sky resource и не ошибка RGB-порядка readback.

Хост теперь читает `nask0sky.fig`/`sky00.mmp` из существующих mounts и рисует
авторский sky через уже поставляемый fixed-function shader. ABI3 передаёт
матрицу без camera translation, поворот15°/час, tint/регион/sky/fog visibility;
геометрия опущена на тот же authored ring, что GL. Открытый низ цилиндра
заполняется цветом окружения вместо прежнего тёмного clear. Большая бледная
заливка на coast/village убрана; дневные/вечерние raw PNG ниже. Переход нижних
колец approximated subdivision4 для существующего fixed-function fog
interpolator; это проход геометрии sky, не фильтр готового изображения.

В родном procedural sky был собственный skyDisplay: экспозиция,
экспоненциальная кривая и display gamma. В текущем stock sky этого grade нет.
Глобального линейного HDR/tonemap поверх всего кадра в данном пути нет.
Оба swapchain/принятая texture — BGRA8 UNORM; `CopyResource` не делает sRGB
conversion. WIC получает BGRA корректно. Host copy/readback выполняются до
gamma-ramp в hidden present; ramp в этом хосте identity, игровые настройки
brightness/contrast/gamma не импортируются. В сохранённом Config64/settings.ini
они0. Готовый кадр не перекрашивается и не повышает резкость постфильтром.

### Дальность, AA, фильтрация, cave, mist

- В282 оставался `draw_distance=300`: cull/fade нового рендера растворял края
  top view при высокой камере. Теперь1000 — поддерживаемый максимум конфигурации;
  попытка2000 штатно ограничивается1000. Это дальше GL fogEnd620. Near/far камеры
  парных кадров1/2000. При отключённом fog и дальних ракурсах за1000 универсальное
  совпадение не обещается; автору нужен per-frame camera draw-distance contract.
- GL capture FBO имеет0 samples, а host282 включал SMAA. Раздельный контроль
  снежной карты: GL lap-var2869, native SMAA1593, native без SMAA3109;
  material3/anisotropy16 без SMAA3023. Поэтому `smaa=false`, native resolution,
  render_scale100/upscaler0. Это одинаковое отсутствие AA с GL-эталоном;
  отдельного sharpen pass нет.
- `anisotropy=4` означает16 samples, `material_quality=3`. В прочитанной
  Config64/rddraw.ini сохранены те же значения. Сам хост282 имел8 samples/material2.
  World sampler zero-initializes описание: mip bias0; mip chain/trilinear/aniso
  сохраняются. У GL mipmapping включён, явная anisotropy не задаётся.
- Сохранённая игровая конфигурация также содержит D3D12, DLSS Balanced,
  sky_clouds2, fog_preset2, draw_distance500. Это чтение конфигурации, не запуск
  и не утверждение, что проверена фактически применённая настройка текущей игры.
  Хост сознательно оставляет D3D11/native resolution/без SDK и stock sky/faithful
  fog для редактора. NVIDIA NGX/DLSS/Streamline не возвращались.
- `fog_preset=0` — штатный faithful режим без modern mist/wisps. Он также
  выключает добавленные haze/height/volume-эффекты; сохраняет stock distance fog.
  Это названная политика хоста, а не правка fog shader или debug_component-mask.
- В cave pinned renderer намеренно обнулял sun и менял ambient через
  `cave_lighting_profile`, хотя GL использует фиксированную cave light table.
  Проверен существующий negative sentinel этой функции. Reviewable
  [патч008](../../tools/rebuild64_renderer/patches/008-embedded-cave-authored-light.patch)
  разрешает -1 только в embedded config; host ставит cave_brightness=-1 и
  сохраняет переданный authored cave-свет. Game range/default вне
  EID3D11_EMBEDDED не меняются; shader sources/table не менялись.

Контроли: [исходный haze/gamma/sky-up](../../work/rebuild64-renderer-stage3-283-20261106/evidence/),
[AA/distance/material числа](../../work/rebuild64-renderer-stage3-283-20261106/quality-controls.json),
[вечерние AO/GI controls и GL-noise](../../work/rebuild64-renderer-stage3-283-20261106/final-analysis.json).
Для патча переиспользована библиотека282: заменён один renderer_device.obj,
остальные модули и prebuilt shader resources сохранены. Исходный материал282
не изменён; [квитанция](../../work/rebuild64-renderer-stage3-283-20261106/cave-patch-receipt.json).

### Парные кадры и числа

Каждая сторона1280×720; original pixels, подписи добавлены вне изображения.
Камеры zone1: village eye(240,110,175)→(240,171,10), coast(90,22,52)→(90,140,6),
top(240,160,260)→(240,171,10), VFOV45°, Z-up. Часы12:40/19:30.
Другие карты: zone12/gz12/region Ingos outdoor и zone12dun2/gz12d2/Ingos cave;
камера из центра MPR и высоты canonical terrain. Их наличие/cave-флаг проверены
по настоящему DejaVu map.txt, а не назначены вручную. Units скрыты, particles
выключены, animation frozen. В каждом GL capture реально прочитан uniform
u_overlayTimeSeconds=0; недействительный таймер один раз до цикла был недостаточным
доказательством, поэтому он сбрасывается непосредственно перед каждым pass.

- [Деревня12:40](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone1-village-1240.png),
  [берег12:40](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone1-coast-1240.png),
  [сверху12:40](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone1-top-1240.png).
- [Деревня19:30](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone1-village-1930.png),
  [берег19:30](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone1-coast-1930.png),
  [сверху19:30](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone1-top-1930.png).
- [Ingos, снег](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone12-1240.png),
  [подземелье](../../work/rebuild64-renderer-stage3-283-20261106/pairs/zone12dun2-1240.png).

В таблице GL / Rebuild64. Яркость — display-luma Rec709 в0..255,
контраст — стандартное отклонение этой luma, чёткость — дисперсия дискретного
лапласиана без нормализации/предварительного фильтра. ROI заданы одинаковыми
координатами для обеих сторон и сохранены в JSON; они не подбирались по результату.

| Кадр | Яркость суши | Контраст суши | Lap-var суши |
|---|---:|---:|---:|
| zone1-village-1240 | 54.17 / 55.04 | 14.92 / 16.48 | 544.84 / 807.18 |
| zone1-village-1930 | 29.98 / 31.14 | 9.39 / 8.12 | 208.63 / 246.63 |
| zone1-coast-1240 | 49.94 / 56.04 | 16.32 / 18.89 | 468.29 / 874.11 |
| zone1-coast-1930 | 26.49 / 35.22 | 8.00 / 9.82 | 178.71 / 278.20 |
| zone1-top-1240 | 69.94 / 62.81 | 12.36 / 15.14 | 377.36 / 734.75 |
| zone1-top-1930 | 45.50 / 38.61 | 7.76 / 7.51 | 179.76 / 239.24 |
| zone12-1240 | 156.31 / 159.68 | 49.01 / 57.82 | 2868.77 / 3022.80 |
| zone12dun2-1240 | 28.18 / 34.84 | 14.03 / 13.78 | 113.43 / 219.95 |

Все ROI неба/фона с яркостью/контрастом и точными прямоугольниками:
[image-metrics.json](../../work/rebuild64-renderer-stage3-283-20261106/image-metrics.json).
Включён отдельный cloud ROI на берегу; background ROI измеряет ровный низ sky.

| Кадр / область | Яркость GL / Rebuild64 | Контраст GL / Rebuild64 |
|---|---:|---:|
| zone1-village-1240 / sky_background | 154.90 / 153.32 | 0.00 / 0.00 |
| zone1-village-1930 / sky_background | 138.24 / 138.03 | 0.00 / 0.00 |
| zone1-coast-1240 / sky | 142.17 / 140.89 | 6.52 / 6.56 |
| zone1-coast-1930 / sky | 124.17 / 124.06 | 5.29 / 5.30 |
| zone1-top-1240 / sky_background | 154.90 / 153.32 | 0.00 / 0.00 |
| zone1-top-1930 / sky_background | 138.24 / 138.03 | 0.00 / 0.00 |
| zone12dun2-1240 / sky_background | 0.00 / 0.00 | 0.00 / 0.00 |

На snow небо в выбранной проекции отсутствует; cave background — чёрный,
а не физическое небо. Не выдаётся фиктивная sky-метрика из области суши.

**Незакрыто:** абсолютный контраст вечером ниже GL: village примерно-14%,
top примерно-3%; cave примерно-2%. Чёткость в измеренных ROI выше GL, дневной
контраст/снег выше. Относительный std/mean даёт другую картину, но он не
подставляется вместо заранее выбранного absolute std для объявления PASS.
Отдельные вечерние controls AO off/GI off не дают универсального совпадения;
новый PBR/AO/GI и GL projected-shadow opacity80/255 — разные модели света.
Не выполнялась художественная подгонка intensity/gamma поверх кадра.
Универсальной настройки хоста для совпадения этих двух response не найдено;
это незакрытый результат исследования, а не доказательство невозможности
исправления renderer shader. Новый shader-режим требует отдельного корректного
контракта authored lighting и проверки всех материалов, а не подбора к этим ROI.
Автору: нужен согласованный embedded editor/light-comparison режим, сохраняющий
hourly table и stock shadow response, либо согласованный эталон native evening
look. До этого строгий общий visual gate не объявляется PASS.

### GL regression к282

Два независимых baseline-процесса на каждой карте, дополнительно ещё два на
zone1 из-за80k-water расхождения. Вода фиксирована uniform0 во всех этих запусках.
На самой282 найдены две начальные водные картинки: a4↔d4 меняются80845px,
meanabs0,71348/255; a4↔283 —80838px/0,71330. d4↔283 —9px/0,000219.
Это283/r4; финальный архивный283/r5 попал в состояние a4 (9px/0,000228),
а относительно d4 —80842px/0,71334. Оба состояния воспроизводятся в самой282.
Остальные кадры сравниваются с обычным единичным overlap-pixel noise baseline.
Следовательно,80k difference не доказывает регрессию283; новый GL-вариант уже
присутствует в неизменной282. Причина двух состояний воды при uniform0 отдельно
не установлена. Побайтовое0 diff не заявляется.
[Все сравнения](../../work/rebuild64-renderer-stage3-283-20261106/gl-regression.json),
[4-run baseline proof](../../work/rebuild64-renderer-stage3-283-20261106/final-analysis.json).

## 2. Панели и экспорт

Переносятся те же экземпляры CViewportWidget в native children активной D3D
surface; при GL возвращаются в CView. Логические geometry/Qt-сигналы сохранены.
Их общий класс уже рисует непрозрачную подложку: прозрачные top-level окна
и screen capture не нужны. QPointer защищает от удаления, новые lazy-панели
обнаруживаются на принятых кадрах; resize обновляет размеры скрытого CView.

Фактические непосредственные QWidget-дети CView при обычном старте:
environmentPreviewWidget, timeOfDayWidget, aiGraphPreviewWidget,
viewportMinimapWidget. Дополнительный lazy child — diplomacyMapOverlay.
Сценарии в282 уже находятся в независимом mapScenarioDock/MainWindow;
mapTestToolbar/viewportOperationToolbar — workspaceContainer, autosave notification
— MainWindow. Они не были детьми скрытого CView. Других обычных direct QWidget
детей в runtime inventory нет. QObject-таймеры/GL resources панелями не являются.

| Панель | Проверка обоих backend |
|---|---|
| Миникарта | видимость/geometry, свернуть/развернуть, mouse click меняет общую камеру |
| Время | mouse click меняет slider/effective time, ABI camera немедленно получает час |
| Окружение | сворачивание и сохранение widget/body; sky/fog/static/water/terrain flags в ABI |
| AI navigation | видимость, geometry, collapse click |
| Сценарии | независимый dock, tab/toggle interaction, собственный offscreen render |
| Дипломатия | создана уже при native view, обнаружена, видна, click Group colours принят |

Матрица GL→native→GL выполнена при actual DPR1 и1,5. Ошибочный первый запуск
actual DPR3 отвергнут; env QT_SCALE_FACTOR0,75 на текущем системном DPR2 дал
проверенный actual1,5. uiScalePercent в коде не менялся, профили тестов собственные.
Снимки — renderer readback плюс QWidget::render тех же panel instances по
их geometry, а не снимок экрана или Windows desktop. Native GetParent и входные
обработчики проверены; видимый monitor smoke запрещён владельцем и не делался.

[DPR1](../../work/rebuild64-renderer-stage3-283-20261106/case-panels-3/),
[финальный DPR1,5](../../work/rebuild64-renderer-stage3-283-20261106/case-final-panels-dpr15/),
[inventory](../../work/rebuild64-renderer-stage3-283-20261106/case-panels-3/child-inventory.json).
3D AI/logic/diplomacy glyphs и прочий служебный слой нового вида остаются частью
плана инструментов ниже; показ панели не объявляется реализацией этого слоя.

Генерация миникарты использует существующий hidden GL capture, сохраняя выбранный
Rebuild64, camera/document/dirty. Проверены кодирование и фактический exportTexture
через кнопку production-диалога в собственный RES с последующим строгим reopen
и чтением MMP; catalog registration отключена в тесте. Новому рендеру не добавлен
путь записи MPR/MOB/RES.
[Экспорт](../../work/rebuild64-renderer-stage3-283-20261106/case-minimap-export-r2/).

## 3. Падение DLL, autosave и отдельный процесс

Перед DLL load и каждым document ставится отдельная startup-pid-uuid JSON-метка
в cache хоста. После принятого кадра и при штатном teardown она удаляется.
На следующем старте PID+creation time отличают умерший процесс от живого/повторно
использованного PID. Нечитаемый живой процесс не объявляется crashed.
Оставшаяся метка подавляет автоматический Rebuild64, выбирает GL и сообщает
причину; ui/renderer не переписывается. Ручной выбор разрешает повторный запуск.

Диагностическая DLL той же C ABI вызвала unhandled access violation внутри
document: настоящий exit0xC0000005; метка сохранилась. Следующий процесс открыл GL,
сохранил настройку Rebuild64, вручную загрузил production DLL и снял метку после
первого accepted frame. Живая метка другого текущего запуска не считается сбоем.
Normal close при native view также exit0. Fault/config exports в production DLL нет.

MOB и MPR с реальными несохранёнными изменениями дали валидные autosave payloads
при native view, dirty сохранился. В другом процессе при native view прерван
настоящий commitMapSaveBatch после первой MOB replacement (TerminateProcess77);
следующий процесс восстановил оба MOB побайтно и строго перепарсил их.
Первые fixture-ошибки (не выставлен dirty/не закрыт input handle) исправлены
в тесте, не в Save-коде; их прежние FAIL-логи сохранены.
[Safety receipts](../../work/rebuild64-renderer-stage3-283-20261106/safety-suite.json),
[journal recovery](../../work/rebuild64-renderer-stage3-283-20261106/case-journal-r2/).

**Guard не перехватывает DLL AV.** В процессе маппера такой AV по-прежнему
закрывает маппер; незаписанные изменения после последнего autosave теряются.
SEH-обёртка вокруг неизвестно повреждённого C++/D3D состояния не даёт сохранности.

Рекомендация директору: **отдельный renderer process до включения по умолчанию**.
MainWindow/CView/Undo/Save остаются владельцами документов; render worker получает
immutable snapshots/deltas через приватный IPC и shared-memory ring. При его
смерти supervisor возвращает GL, сохраняя живые документы и текущий мазок.
Он не получает save authority/ProgramData locks. Job/supervisor управляет только
своим PID+creation identity, не мостами/процессами владельца.

| Вариант | Объём и задержка | Сохранность/риск |
|---|---|---|
| In-process C ABI | уже есть worker/device/accepted frame, без IPC | AV/heap corruption губят owner process; guard помогает только от повторных запусков |
| Child HWND/swapchain в другом процессе | новый lifecycle/IPC/supervisor; вывод напрямую в дочерний HWND, camera/latest-state messages | DLL crash локален worker; проверить owner/child lifetime, Z-order, resize, focus, DPR и crash teardown |
| Shared GPU texture + host composition | дополнительно shared-handle/fence ownership и frame-generation tokens; удобно для overlays/readback | отрыв producer/consumer, timeout/fence/device-loss и старые кадры требуют строгого протокола |
| CPU RGBA shared-memory каждый кадр | простой prototype, но1280×720×4=3,69MB/frame,≈221MB/s при60Hz на одну копию | лишняя latency/bandwidth; для постоянного пути не рекомендуется |

Оценка объёма: не переписывать renderer, а вынести имеющийся Session/C ABI,
добавить транспорт, управление жизненным циклом и безошибочный reopen/fallback.
Это самостоятельный крупный этап с матрицей crash/start/load/resize/close,
а не маленький try/catch. Дополнительная задержка здесь **не измерялась**:
целевой дизайн latest-camera/no deliberate queued frame; async texture ring
может добавить кадр. MPR/MOB полные snapshots не следует передавать на каждый
camera tick. Глобальный driver reset/TDR может затронуть оба процесса — процессная
изоляция не является защитой от любого сбоя GPU/ОС. Реализация в283 не входила.

## 4. План инструментов — исследование, без реализации

Данные и операции остаются CView/canonical documents. Не переносить Undo,
hotkeys или Save в DLL. В долгосрочном варианте служебный слой рисует host D3D
из небольших typed lists; временный GL-alpha pass допустим только с проверенным
GPU texture interop и depth contract. CPU readback/composite на каждый кадр
не выбирать без измерения. Стоимость нового overlay pass в283 не измерялась:
он не реализован, и фиктивных таймингов нет.

| Пункт | Где сейчас / перенос | Объём, риск, что нужно автору |
|---|---|---|
| Выделение и рамка | CView::pickObject/drawSelectFrame, CSelectFrame::draw в ogl_utils.cpp; selection state остаётся Qt. Host рисует outline/box/2D marquee по stable entity IDs | Малый raster pass, средняя связка с ID/depth. Рамка/CPU state без автора; точный silhouette/depth требует renderer contract |
| Сетка, кистевой круг | CLandscape::drawTileGrid, CView::drawTerrainPaintOutline и sculpt geometry; те же canonical coordinates в typed line/triangle lists | Средний: depth bias, zero/one depth, alpha, clipping, DPR. CPU outline без автора; доступ к согласованному depth нужен для occlusion |
| Logic/service/scenario/CAM/entrance/AI | CView::drawTestOverlaysOpenGl, node->drawLogicVisual, scenario overlays; данные не превращать в MOB objects | Средний набор glyph/line/text primitives и atlas шрифта. Можно в host; согласовать world-depth/frame generation, не использовать game HUD/engine headers |
| Gizmo | CMoveAxis/CRotateAxis/CScaleAxis в operationmanager.cpp и текущая CView operation state/оси | Средний: только визуализация D3D, прежние constraints/snapping/Undo/keys остаются. Не вводить второй источник transforms; picking ID/depth — API вопрос |
| Picking и рамочное выделение | CView::beginEditorQueryFramebuffer/drawSelectionTerrainDepth/pickObject, glReadPixels; первоначально можно hidden GL ID pass с общей камерой | Средний/крупный. GL query расходится с native cutouts/LOD/displacement; конечный путь — async ID+depth from accepted D3D generation. Нужны stable IDs и depth/ID export/lifetime у автора |
| Рельеф, sector updates | CView::applyTerrainHeightPreview уже собирает QSet затронутых секторов, CTerrainEditModel хранит deltas. Сейчас viewport сериализует полный MPR/MOB и host_document retires whole world/atlases/scene | Крупный: ABI sector deltas, immutable CPU cache, border normals/seams, частичный GPU upload и Terrain2 rebake, Undo/Redo; CPU часть без автора, контракт dirty-cells/ready generation — с автором |
| Units | CUnit::refreshTransientVisual/refreshAnimationPose; CView schedule/apply unit appearance preparation, ProfileDatabaseContext и MOB Import/equipment | Крупный самостоятельный этап: typed appearance/skeleton/pose/parts/material IDs, ownership и animation clock. Не выдавать static seed за unit. Host resolution без private engine возможен; retained/skinning lifetime API — вопрос Илье |
| stst8/742 | Legacy queue пропускает empty assembly helper через isDrawable; новый retained candidate отвергает model node4 как figure_structure | Малый целевой parser/assembly fix + многовидовой regression. Сохранить LNK/BON transform chain и все drawable children; пустой helper transform-only, не удалять root/model data. Нужен подтверждённый renderer empty-node contract |

Для stst8 есть новое read-only подтверждение: original_nid742, visual=stst8,
source rejection0; prepare result11=figure_structure, node_index4. Номер661 в
diagnostic — локальный remapped signed ID хоста, не изменение MOB ID742.
Это показывает также необходимость stable identity map(MOB identity,mapID,part)
до picking: текущая перенумерация combined object list для этого не годится.
Полная коррекция stst8/units в283 не выполнялась, игровые ресурсы не ремонтировались.

750/810мс — измеренный в282 p50/p95 full snapshot→accepted generation,
не чистый GPU bake и не новый замер283. Для30–60мс нельзя маскировать pending
кадр или измерять лишь отправку. Порядок: экспорт уже найденных dirty sectors →
сохранить world/object/material identities → upload только affected buffers →
rebake только зависимые cells/borders → accepted-generation fence. Затем измерить
те же50 distinct height updates, undo/redo, seams и отсутствие stale/marker frames;
отдельно CPU serialization/upload/bake/present. Гарантии30–60мс до этого нет.

Нужные ответы Ильи: общий presentable-generation token (не RGB), partial sector
update/invalidation contract, ID/depth/shared texture access и ownership,
skeletal appearance/pose API, empty helper policy, согласованный authored/editor
light response и draw-distance за1000 при отключённом fog. Вопрос разрешения
автора закрыт и повторно не задаётся.

### Порядок до default

1. Закрыть строгий visual gate/согласовать вечерний и cave light response,
   принять реальные пары владельцем.283 сама этого принятия не означает.
2. Изолировать render process, проверить AV/device-loss/restart без потери
   живого документа; оставить рабочий GL fallback.
3. Stable IDs, ID/depth generation и host service overlay; picking/selection/gizmo
   с прежними координатами и горячими клавишами.
4. Sector-delta pipeline и доказанные30–60мс; кисти/Undo/Redo/сохранение и seams.
5. Полные units/appearance/animation и stst8 hierarchy; реальные многовидовые кадры.
6. Полная матрица tools/Save/autosave/recovery/panels/DPR на новом основном виде,
   ручная приёмка, затем отдельное решение о default. До него default не меняется.

## Release, staging, сохранность

Qt5.12.12/MinGW7.3 x64, MSVC v143 x64 DLL. Сборка из git archive; compilation
через заданный8.3 root. Wrapper собрал mapper/MCP, но канонизация .cmd пути
с пробелом остановила runtime invocation. Оба предусмотренных x86 runtime
скрипта затем выполнены напрямую по8.3 path из того же архива, exit0/0.
Это не PASS wrapper и не копирование старых runtime. Подробности
[release-receipt](../../work/rebuild64-renderer-stage3-283-20261106/release-receipt.json).

- Source tree: `749c302fadb17489ba6723a3626739a85b34e8a0`.
- Mapper SHA256: `912280ed2ff41eb726587665e1bf01c83a1b3db693e1b46d4b231b0846456981`.
- DLL SHA256: `31db700f83308d23cda24af5bedabd987b22e4272f311f6acc00e6d6a3dab01b`.
- Таблица638 shaders переиспользована; shader sources не менялись; NVIDIA SDK отсутствует.

Свежий [staging283](../../build-staging-283-rebuild64-stage3-20261106/). Finalizer/validator exit0; 1364→1364 managed paths, удалено0, добавлено0: ничего. Raw/staged version283 и SHA identities подтверждены до финализации; после неё пакет не запускался. [Состав и hashes](../../work/rebuild64-renderer-stage3-283-20261106/package-receipt.json).

Намеренные изменения при обычном OpenGL startup: **никаких изменений изображения,
инструментов, hotkeys, Save/форматов, MCP-протокола или uiScalePercent**.
Уже существующий backend switch сохраняется. Специально согласованное исключение:
после прерванного Rebuild64 startup выбирается GL с сообщением и без переписывания
предпочтения. После native режима Qt-панели возвращаются с теми же geometry,
видимостью и состоянием; native handles — внутренняя деталь миграции.
Подготовка пакета также включает уже принятый packaging795aae2b директора.
Удалённых функций и поставляемых путей: **ничего**.

Защищённые CPP/H/QM/.mcp.json/отчёт277 сохранены по SHA. Dirty view.cpp и
landscape.cpp не менялись и не включены в archive/commit; собственные строки
ROADMAP отделены от строк владельца. Сборки/настройки/логи/игровые данные не
включаются в Git. Lrelease работал только в archive, не над QM владельца.
Материал пробы и282 не удалялся. Очистка/остатки описаны в W/cleanup-plan.json;
удалены128 собственных map/backup/autosave/journal/export payloads,182515754 байта.
Шесть входных MPR/MOB перед удалением совпали с исходными копиями по SHA.
[Точные удалённые пути/hashes](../../work/rebuild64-renderer-stage3-283-20261106/cleanup-targets.json),
[сохранность защищённых файлов](../../work/rebuild64-renderer-stage3-283-20261106/preservation-audit.json).
subst пуст. Установка и push не выполнялись.


## R2 — поправки после визуальной приёмки директора (2026-11-06)

База: main `b2e35811421bc2fb8b2fdf1af838b86bcc7b598a`, без промежуточных
изменений source/ или tools/rebuild64_renderer. VERSION остаётся283: R1 не
устанавливалась. Предыдущий текст и `pairs/` сохранены; этот раздел уточняет
его выводы. Полный общий visual PASS **не объявлен**. Исправления и причины
семи замечаний ниже проверены; остаточные отличия освещения/воды перечислены.
Установка и push запрещены и не выполнялись. Только эта RTX4070SUPER.

Обозначения: W=`work/rebuild64-renderer-stage3-283-20261106`, T=`W/r2`.
Финальные пары — [pairs-r2](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r2/),
слева GL, справа Rebuild64, исходники каждой стороны1280×720.
Все кадры — собственный GL/D3D readback из изолированных скрытых процессов;
снимков рабочего стола, видимых окон, запуска установки/игры/старых EXE нет.
Старые кадры R1 только читались как данные. Коррекции готового кадра, LUT,
повышения резкости и подгонки экспозиции нет. Контактные пары лишь соединяют
неизменённые пиксели readback с подписью над кадром.

### 1. Одно сообщение на одно падение

Причина подтверждена: R1 удаляла старые метки только при `selected && persist`.
После обнаруженного падения теперь сначала атомарно записывается отдельный
cache-latch `manual-retry-required`, затем показывается сообщение и удаляются
старые метки. Следующие запуски молча остаются на GL до явного выбора вида.
Это исключает автоматический повтор аварийной загрузки при сохранённом
`ui/renderer=Rebuild64`; само предпочтение автоматически не переписывается.
Любой ручной выбор в меню, GL или Rebuild64, убирает старые метки и latch.
Ручной Rebuild64 — осознанная новая попытка; её новое падение даёт одно новое
сообщение. При ошибке записи latch метка сохраняется, чтобы не запустить DLL
автоматически без защиты; обычный проверенный writable cache работает штатно.

Настоящее `EXCEPTION_ACCESS_VIOLATION` внутри собственной диагностической DLL:
exit `0xC0000005` → первый новый процесс:1 сообщение/GL → второй:0 сообщений/GL.
Обе ветки сохранённой настройки, OpenGL и Rebuild64, PASS; исходное значение
сохранено. Отдельно PASS ручной Rebuild64 после предупреждения и первый кадр.
Тест не подменял AV возвратом ошибки; diagnostic fault hook отсутствует в DLL
пакета. [Результаты](../../work/rebuild64-renderer-stage3-283-20261106/r2/guard-results.json),
[ручной повтор](../../work/rebuild64-renderer-stage3-283-20261106/r2/manual-retry.json).

### 2. LIGHT/TORCH и поправка к причине вечернего контраста

**Поправка к R1:** объяснение вечерних−14% только разницей моделей освещения
было неполным: из хоста вообще не передавались точечные источники. Это
конкретная ошибка входа, а не неизбежное отличие renderer.

ABI4 несёт до64 `EirPointLight`: position/color/radius/source_id/casts_shadow.
`cameraSnapshot()` каждый tick берёт те же LIGHT/TORCH из живого состояния,
что CView: Full Environment, visibility, hidden, sceneLights, frustum, preview
position, смещение LIGHT+2Z, TORCH particleWorldPosition/power×10, нормализацию
цвета. Хост вызывает штатный `visual_publish_world_point_lights` перед world
draw. Источники не читаются из сохранённого MOB вместо текущей памяти.

В village1930 GL/ABI имеют10 источников; coast —5; cave —0/0. Превью перемещения
LIGHT меняет ABI и следующий кадр без Save; смена часа меняет camera tuple и
освещение. Повторно сняты все вечерние пары и cave. В контроле на том же кадре
village1930 только отключение/включение LIGHT/TORCH меняет std суши
7.47→9.20 (GL9.39),69732 пикселя; это основная доказанная составляющая
прежней потери вечернего контраста. Суммарный результат R2 включает и другие
исправления, поэтому приписывать весь R1→R2 прирост одним лампам нельзя.
В cave нет этих LIGHT/TORCH: её остаточное отличие ими не объясняется.
[Контроль ламп](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r2/controls/zone1-village-1930-lamps-off-on.png),
T/case-checked-zone1/process-r2-controls.log, T/controls-metrics.json.

### 3. Вода: штатный modern путь и отсутствовавшие source pages

В physical/modern пути при `sky_clouds=0` вода не отражает загруженные хостом
авторские облака как cubemap. SSR берёт текущие opaque color/depth; при промахе
`liquidOwnedSky` использует fog/sky gradient, в том числе
`skyColor*lerp(1,.65,max(reflected.z,0))`. Минимальная Fresnel-доля.22 и
осветление мелководья остаются и сверху. Это объясняет бледную воду при
отключённых procedural clouds; выключение SSR не убирает fallback/пену.

Цвет/opacity взяты из MPR, не белая заглушка: water object0 `(49,82,166)/255`,
opacity.6; object1 `(49,137,92)/255`, opacity.7. Depth приходит из terrain
vertices, не константой хоста. Геометрия/цвет/глубина здесь не исправлялись.
Однако host заполнял GPU atlas и пропускал `TerrainCpuAsset::pages` — см.5.

Для редактора выбран штатный **`water_simple=true`**: авторский tile art,
цвет и opacity, без modern SSR/refraction/foam/shore/ripples. Это выбор
renderer до отрисовки, не обработка полученной картинки. Вечерняя река теперь
синяя, серой «дороги» нет; днём modern-осветление/пена исключены. Вода всё ещё
не пиксельно равна GL: особенно море у coast1240 светлее и иначе читаются
дно/прозрачность. Источники данных одинаковые; оставшаяся разница относится к
штатному простому liquid shading/compositing, а не к exposure/sRGB хоста.
Строгое совпадение воды **не достигнуто**. Для точного game/GL editor profile
Илье нужен вопрос о нормативном liquid light/opacity response и, если оставлять
modern воду, API stock-sky reflection probe вместо procedural fallback.
T/case-checked-zone1 содержит сравнения no-SSR/physical0/foam debug/stock water.

### 4. Белые пятна: два разных механизма

Coast(165..230,503..530) и top(221..226,202..207) — liquid foam pass,
не неверные нормали/маска terrain. `water_surf_debug=3`, physical0 и no-SSR
отделяют surf foam от отражения. Дополнительный точный контроль заменяет
только foam pixel shader на discard **до draw в своей диагностической DLL**:
top pixel(224,205) RGB `(201,225,243)`→`(44,76,94)`. Остальные входы неизменны.
Этот диагностический shader не входит в продукт/lock/table и не является
постобработкой кадра. В итоговом editor profile foam убирает `water_simple`.
[Контроль прохода](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r2/controls/zone1-top-1240-foam-pass.png).

Snow cave(257..423,570..717) — Terrain2/PBR specular от фиксированного
направленного света cave environment. Это не наружное физическое солнце:
таблица GL использует этот свет для diffuse. Патч008 сохраняет авторскую
cave lighting table по sentinel−1; он не меняет mask/normal, но позволил
современному terrain material использовать ту же составляющую как specular.
При строго одинаковых Terrain2=1/SMAA=1/воде переключение только штатного
`material_terrain=1→0` убирает219 пикселей Y>200, max255→141.45.
В editor profile `material_terrain=false` и исправленный terrain_blend0
сохраняют diffuse и устраняют лишний PBR, без обнуления всего cave light.
Патч008 остаётся. [Изолированный PBR-контроль](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r2/controls/zone12dun2-1240-pbr-isolated.png),
T/cave-material-proof.json.

В указанных ROI число пикселей Y>200 R1→R2: coast485→0, top5→0,
cave216→0; в GL во всех трёх0. Это проверяет именно отмеченные дефекты,
не объявляет отсутствие любых бликов по всей карте. T/white-roi.json.

### 5. Болото: ошибка комплектации входа хоста

По прочитанным MPR/material данным это swamp tile type14:2738 swamp и7138
open-water liquid tiles. В source art reference slot0 — чистое болото;
8 из17 swamp frames включают переход к open water (34 transition tiles).
Хост вызывал classifier после одного `parse_terrain_asset`, который не
заполняет source pages. GPU atlas не заменяет их CPU art. Поэтому штатный
`owned_terrain_classify_swamp_frames` не мог получить авторский swamp frame.

Теперь страницы загружаются через ResourceStore/ensure_upload_bytes так же,
как в обычном loader, **до classifier**:19/21 страниц zone1 найдены (две
необязательные отсутствовали и раньше), получен swamp frame128×128.
Вместе с authored-water режимом пёстрые синие разводы на чистом swamp исчезли,
реальные переходы карты не перекрашиваются. Здесь неверным был вход Rebuild64;
доказательств «GL размывает правильное синее болото» нет, GL не менялся.
[Авторский frame без фильтра](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r2/controls/authored-swamp-frame.png),
T/case-checked-zone1/terrain.csv и TERRAIN_SOURCE в его log.

### 6. Зерно: отключённый blend и потерянные mips

Найдены независимые причины. Document feature mask23 заново включал
terrain_blend, несмотря на init0: теперь7. Затем CPU batch constants всё равно
передавали Terrain2 table index при blend0: reviewable-патч009 учитывает
настройку. Наконец E1 GPU surface ingress объявлял один mip и копировал только
subresource0, хотя исходный atlas имел полную цепочку. Патч010 сохраняет
E1Surface mip count, проверяет descriptor и копирует все заявленные уровни.
У старых one-level producers поведение сохраняется. Диагностика проверяет
фактически sampled resource **и SRV**, включая11/12 уровней по размеру atlas,
а не только созданный хостом исходный texture descriptor.

На тех же камерах сняты terrain_blend0/1 и SMAA0/1. Выбран **terrain_blend0**:
авторские atlas transitions вместо Terrain2 reconstruction, при реально
работающей mip-цепочке; **SMAA1** убирает raster edge alias. Это штатное AA,
не фильтр резкости/шумоподавления поверх сохранённого кадра. Лапласиан R1
больше не трактуется как доказательство качества: он растёт и от шума.
В JSON он явно назван frequency activity.

Отдельный наблюдаемый proxy — median absolute residual к локальному3×3
среднему, вычисляемый для анализа, не меняющий изображение. Rocks ROI
(470,160)-(740,440): GL1.715, R1 4.172, R2 2.031; evening road ROI
(700,260)-(820,620): GL.761, R1 1.486, R2 .547. Он смешивает детали и шум,
поэтому не доказывает качество/сохранность деталей. Решает просмотр пар в
исходном масштабе. Ранее рост Laplacian был ошибочно назван большей чёткостью;
этот вывод R1 отозван. [A/B terrain](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r2/controls/),
T/frequency-activity.json и controls-metrics.json.

Readiness также исправлена: выключенный Terrain2 не ждёт его неиспользуемый
bake, но ждёт contact-atlas publication. Временная экспериментальная проверка
resident specialised shader sets была отвергнута: полные general shaders уже
валидны, а ожидание optional optimization удерживало старый кадр и делало A/B
недостоверным. Она отсутствует в итоговой DLL. Только case-checked-* и
case-final2-* используются как итоговые controls/пары; более ранние case-mip,
case-verified и case-trace имеют неудачные/устаревшие прогоны и не считаются
PASS. Итоговый harness ждёт принятый новый кадр и pending=0.

### Парные кадры, числа и незакрытая визуальная граница

Все восемь пар заново сняты с production DLL R2. Zone1 village/coast/top
в12:40/19:30, другой аллод zone12 Ingos и cave zone12dun2. ROI те же R1;
точные координаты, яркость/контраст суши и sky/background — T/image-metrics-r2.json.
Y — display-luma .2126R+.7152G+.0722B (0..255), contrast — population std.
Большие land ROI включают воду и объекты: их числа нельзя выдавать за оценку
каждого отдельного материала. Cave background чёрный, отдельного неба нет.

| Кадр | Суша GL mean/std | Rebuild64 mean/std | Δ std | Sky/background GL mean/std → R2 |
|---|---:|---:|---:|---:|
| zone1-village-1240 | 54.17 / 14.92 | 53.58 / 15.77 | +5.7% | 154.90/0.00 → 153.32/0.00 |
| zone1-village-1930 | 29.98 / 9.39 | 31.04 / 9.20 | -2.1% | 138.24/0.00 → 138.03/0.00 |
| zone1-coast-1240 | 49.94 / 16.32 | 50.84 / 15.35 | -5.9% | 142.17/6.52 → 140.89/6.56 |
| zone1-coast-1930 | 26.49 / 8.00 | 31.88 / 7.73 | -3.4% | 124.17/5.29 → 124.06/5.30 |
| zone1-top-1240 | 69.94 / 12.36 | 61.34 / 14.08 | +13.9% | 154.90/0.00 → 153.32/0.00 |
| zone1-top-1930 | 45.50 / 7.76 | 38.30 / 7.94 | +2.3% | 138.24/0.00 → 138.03/0.00 |
| zone12-1240 | 156.31 / 49.01 | 154.75 / 50.38 | +2.8% | нет отдельного sky ROI |
| zone12dun2-1240 | 28.18 / 14.03 | 32.61 / 13.26 | -5.4% | 0.00/0.00 → 0.00/0.00 |

Сверху absolute contrast обоих zone1 кадров выше GL, однако это само по себе
не доказывает лучшую чёткость. Village evening остаётся≈−2.1%, coast day≈−5.9%,
coast evening≈−3.3%, cave≈−5.5% по указанным ROI. Сверху днём R2 темнее GL,
вечером coast светлее; море/прозрачность, тени, световая модель и некоторые
края отличаются. Их не скрывает средний показатель. Строгий критерий
«ни одного ухудшения против GL» **не подтверждён**. Нужны просмотр директора
и согласованный с Ильёй authored light/shadow/liquid response, если требуется
дальше сближать модели. Добавлять подгоняющий постпроцесс нельзя.

### 7. Итоговая DLL: полный runtime recipe, прежние shader resources

Pinned vendor извлечён из git archive в собственный build root; все10 патчей
lock применены. `OwnedRenderer.vcxproj /t:Rebuild` пересобрал98 объектов;
backend затем собран/перелинкован с этой целой библиотекой. R1-приём замены
renderer_device.obj в существующей lib **не использовался**. SHA runtime lib:
`5b37354dbc44bb63d4f41d3103d08ba40c3e6382f7a1eeba8a5f90ea4c023be9`.
После полного runtime build изменялся только host readiness; финальный host
перекомпилирован и связан с той же неизменной полной lib. Различие tree/hash
промежуточной full-rebuild receipt и итоговой release receipt этим объяснено.

Патчи009/010 меняют CPU данные/копирование, не HLSL. Все190 shader headers/HLSL
совпадают с материалом282;638 shader table/resources переиспользованы без
изменения и без runtime-компиляции в production. Shader bin SHA256:
`38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64`.
Отдельная чистая проверка повторно применила все10 patches к12 затронутым
файлам pinned archive и сравнила с реально собранным vendor. Приватный engine
не копировался, внешние checkout не менялись. NVIDIA SDK не возвращён;
проверены imports DLL, единственный export `ataman_renderer_get_api`.
[Полный rebuild](../../work/rebuild64-renderer-stage3-283-20261106/r2/full-rebuild-receipt.json),
[итоговый audit](../../work/rebuild64-renderer-stage3-283-20261106/r2/final-recipe-audit.json).

### Регресс, панели, сборка и сохранность

GL shader time зафиксирован на0 и прочитан обратно из u_overlayTimeSeconds.
Изображения GL R2 сравнены с **двумя** известными water states R1 r4/r5:
даже при time0 первый этап загрузки даёт два состояния. Для каждого кадра
выбрано ближайшее, а обе дистанции и собственный межзапусковой шум R1
сохранены в T/gl-regression-r2.json. В ближайшем состоянии остаётся0..7
пикселей на921600; максимальная средняя абсолютная ошибка RGB
0.000170356 из255. Это не заявление
побитового равенства всех запусков и не разрешение игнорировать водный state.

Панельный код R2 не менялся, поэтому контрольные DPR1/1.5 R1 не повторялись.
Честная граница: readback renderer + **QWidget::render** и проверки родителя,
geometry/input не доказывают реальное наложение панелей на D3D поверхность
в видимом окне. **Никто этого оконного наложения в этих проверках не видел;
это остаётся проверкой владельца.** Предыдущая формулировка VERIFIED относится
к указанным скрытым проверкам, а не к фактической оконной композиции.

Save/autosave/recovery/MPR/MOB/RES/tools/hotkeys/MCP/uiScalePercent не менялись;
проверки сохранности R1 не заменяются новыми косвенными тестами. R2 только
читает материалы и текущее состояние, не сохраняет карты. При выбранном
OpenGL изменено **только** поведение crash-message/latch из1. Изображение GL
и инструменты намеренно не менялись. Удалённых функций/поставляемых путей:0.

Release из собственного git archive tree `eeb93c9c2562765e815a6aafce52182f15aac6f9`;
Qt5.12.12/MinGW7.3 x64, renderer MSVCv143 x64. Основной wrapper вернул1 на
известном преобразовании8.3 в путь с пробелами перед x86.cmd; это не PASS.
Qt mapper/MCP уже были собраны, оба x86 runtime затем успешно собраны прямым
запуском скриптов из **того же archive** по8.3 пути (0/0), не скопированы из
старой сборки. Final raw/staged --version283 exit0, SHA одинаковые.

Mapper SHA256 `c1f7e59ff1ac53aac0cc2406b930f7d492e1ff0b4a6a135738db323eaa5efd4a`.
DLL SHA256 `57c123580cb6dda441f5886b5bbe596cba4bf88d56237f34575e4d7175c1a309`.
[Свежий staging](../../build-staging-283-rebuild64-stage3-r2-20261106-205516/): финализатор exit0,
1364→1364 против282, **0 потерь,0 добавленных файлов** (перечень добавленных:
пусто). [Квитанция состава](../../work/rebuild64-renderer-stage3-283-20261106/r2/package-receipt.json).
Старый собственный staging без time suffix содержит промежуточный readiness
и не предназначен для передачи: автоматическая approval-проверка отклонила
его удаление. Он оставлен с явной пометкой в cleanup-plan; передаётся только
папка по ссылке выше. Финализатор запускался только на собственной свежей
папке; установленная282 и пользовательские данные не затрагивались.

Чужие CPP/H/QM/.mcp.json/отчёт277 сверены по исходным SHA и не включены ни в
архив сборки, ни в scoped-коммит. В ROADMAP staged только собственные строки;
строки владельца остаются в рабочем файле. Материал пробы/282 сохранён.
Удалены6 тестовых MPR/MOB (8179477 байт), SHA всех входных копий совпали перед удалением.
Пути тестовых копий и точный результат очистки — T/cleanup-plan.json,
W/cleanup-plan.json. subst проверен отдельно; junction не создавались.

Рекомендация и этапы частей3–4 R1 остаются: отдельный render process до
default для сохранности несохранённой памяти; затем stable IDs/service layer/
picking, sector deltas30–60мс, units/stst8, полная приёмка. R2 этого не
реализует и не заменяет. Следующий шаг — визуальная приёмка новых пар и
ответ Ильи по оставшемуся liquid/light response; **STOP BEFORE INSTALLATION**.
