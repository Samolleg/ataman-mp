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


## R3 — игровой профиль вместо сравнения с GL (2026-11-06)

**Поправка владельца/директора:** критерий «не хуже OpenGL» отменён целиком.
Все прежние выводы о необходимости сближать цвет, воду, контраст или чёткость
с GL остаются историей R1/R2 и **не являются критериями этой задачи**.
Рендер и его художественная политика принадлежат автору Rebuild64. Новая цель:
передавать его настройки и исходные факты; собственных улучшений вида нет.
Причина смены критерия — новое решение владельца, не ошибка исполнения R2.

База main `12acdabc5c5e8346cc060ee881930c953d1cf65f`, VERSION283; история не
переписывалась. **Настройки проверены; полная эквивалентность всех игровых
входов НЕ достигнута**: недостающие runtime-контракты перечислены ниже.
Игру не запускали; эталонного игрового кадра нет и совпадение с ним не
заявляется. Кадры сверит владелец. Установка и push не выполнялись.

W=`work/rebuild64-renderer-stage3-283-20261106`, T=`W/r3`.
Новые [восемь кадров Rebuild64](../../work/rebuild64-renderer-stage3-283-20261106/pairs-r3/)
содержат только неизменённый1280×720 readback нового рендера, без GL рядом,
постобработки, масштабирования или повышения резкости. Старые pairs/pairs-r2
и все предыдущие разделы отчёта сохранены. Снимков экрана владельца, desktop/
computer-use, видимых окон, запуска установки/старых EXE/игры не было.

### Что снято и какие патчи оставлены

Удалены `stock_sky.inc` и загрузка/отрисовка nask0sky/Sky00. Штатный
`E1ClaimSky` передаёт небо самому renderer с его sky_clouds. Сняты все
host-quality overrides: shadows/point_shadows/shadow_far, draw_distance1000,
sky_clouds0, fog_preset0, cave_brightness−1, terrain_blend0, SMAA-решения,
water_simple=true, material_terrain=false, material_quality/aniso и document
feature bit, менявший terrain_blend. Эти значения теперь определяет INI.
GL renderLightingMode/sceneLights/frustum и переключатели GL sky/fog не задают
политику Rebuild64. Общие камера/час/регион остаются сервисными входами редактора.

Патчи008/009/010 удалены из lock и из дерева исходников. Для010 отдельная
потеря именно embedded-хоста **не доказана**: проверенный pinned typed-путь
`E1Terrain::E1PrepareTerrainCommand` также публикует surface без mip_count,
а общий get_native_owned_texture выбирает1 mip. Поэтому сохранять R2-расширение
как «правильнее игры» нельзя. Это не утверждение о любом другом игровом
texture producer или о качестве изображения. Исходный путь восстановлен;
embedded shader/appearance-патч вместо010 не вводился.

| Патч | Назначение | Решение R3 |
|---|---|---|
|001 E1Terrain|Публикация immutable CPU snapshot без retail pointers|Оставлен: вход памяти|
|002 Renderer.props|FunctionLevelLinking для независимой линковки|Оставлен: сборка|
|003 OwnedRenderer|Исключение SDK props/DLSS backend, no_ngx provider|Оставлен: сборка без SDK|
|004 renderer.h|Friend для readiness и host bootstrap нормального config reader|Оставлен: доступ к встраиванию, shading не меняется|
|005/006 owned_asset_loader|memory_map/memory_terrain вместо повторного открытия файлов|Оставлены: unsaved memory ingress|
|007 embedded_host|Явный reset после завершения единственного сеанса|Оставлен: lifetime|
|008|Редакторский cave sentinel−1|Снят|
|009|Изменённая admission Terrain2 table|Снят|
|010|Расширенная передача GPU mip chain|Снят: editor-only loss не доказан|

Все001–007 просмотрены: нет поправок художественной модели. Повторная чистая
проверка применила001–007 к pinned archive и сравнила12 файлов со сборкой,
включая восстановленные поверхности/renderer_device/flush/surfaces после008–010.
[Audit](../../work/rebuild64-renderer-stage3-283-20261106/r3/final-recipe-audit.json).

### Настройки: источник, bootstrap и отступления

G в полной таблице ниже означает неизменённый источник
`C:/GOG Games/PZ/Config64/rddraw.ini`, секция `[graphics]`.
S означает `C:/GOG Games/PZ/Config64/settings.ini` с указанной секцией.
Файлы владельца только читались. Перед каждым renderer session worker создаёт
изолированные копии под cache тестового/будущего mapper process. Renderer может
сохранять только свои копии. Missing/invalid required config даёт явный fallback,
а не собственный профиль качества. Строки nvidia_cache_id не переносятся:
это process-cache bookkeeping, не настройка изображения.

Обнаружена и исправлена причина скрытого использования defaults: pinned
`Renderer::ensure_host()` регистрирует options и вызывает apply, **но не
загружает INI**, в отличие от обычного bootstrap с DebugOverlay. Теперь хост
до ensure_host вызывает тот же register_config_options и RendererConfig::load
над копией. Повторная регистрация не сбрасывает уже загруженные значения
(RendererConfig::register_option отвергает duplicate key). В первом R3 прогоне
был draw_distance300/aniso3/material2; он не считается проверкой конфигурации
и не используется в pairs-r3. Финальные процессы подтвердили340/340 source
values через EFFECTIVE_CONFIG, без подмены значений.

Единственные изменения graphics-копии:

| Параметр | Игра/G | Хост | Причина и влияние |
|---|---:|---:|---|
|renderer_backend|1 (D3D12)|0 (D3D11)|Разрешённый backend без D3D12; может менять точность/порядок GPU операций и производительность. Пиксельное равенство не заявляется. Общие shaders/материалы не исправлялись.|
|upscaler|1 (DLSS), quality1|0|NGX/DLSS не входят. Нет их temporal reconstruction/jitter/detail response; render_scale100 оставлен из файла, вывод native resolution.|
|frame_generation|2|0|Streamline/FG отсутствуют: нет генерированных промежуточных кадров/их артефактов, меняется плавность и задержка.|
|reflex|0|0|SDK latency integration отсутствует; на этих настройках значение уже0, собственно изображение не меняется.|

upscaler_quality и upscale_sharpness сохранены из файла, но при off не действуют.
Все остальные340 зарегистрированных values загружены, включая mist/wisps,
clouds, modern water, PBR, Terrain2 и эффекты. Числа float в dump округлены до
6 знаков, comma-list нормализованы штатным reader; сравнение учитывает это.
Нерегистрируемых/пропавших graphics keys в данном INI нет.

| Параметр | G | Effective | Источник |
|---|---|---|---|
| `confine_cursor` | `true` | `true` | G |
| `vsync` | `false` | `false` | G |
| `background_mode` | `1` | `1` | G |
| `background_frame_limit` | `15` | `15` | G |
| `frame_limit` | `60` | `60` | G |
| `shadows` | `true` | `true` | G |
| `terrain_normal_sunlight` | `true` | `true` | G |
| `smaa` | `true` | `true` | G |
| `depth_prepass` | `true` | `true` | G |
| `upscaler` | `1` | `0` | G; разрешённое отступление |
| `upscaler_quality` | `1` | `1` | G |
| `upscale_sharpness` | `0.200000003` | `0.200000` | G |
| `render_scale` | `100` | `100` | G |
| `camp_ui_layout` | `1` | `1` | G |
| `ui_language` | `0` | `0` | G |
| `harness_shot_after_overlay` | `false` | `false` | G |
| `frame_generation` | `2` | `0` | G; разрешённое отступление |
| `frame_generation_min_fps` | `15` | `15` | G |
| `reflex` | `0` | `0` | G |
| `latency_pacing` | `0` | `0` | G |
| `fg_frame_image` | `false` | `false` | G |
| `renderer_backend` | `1` | `0` | G; разрешённое отступление |
| `display_mode` | `0` | `0` | G |
| `window_width` | `0` | `0` | G |
| `window_height` | `0` | `0` | G |
| `owned_ui_text` | `true` | `true` | G |
| `anisotropy` | `4` | `4` | G |
| `water_reflections` | `true` | `true` | G |
| `water_reflection_quality` | `2` | `2` | G |
| `water_reflection_step_pixels` | `8` | `8.000000` | G |
| `water_simple` | `false` | `false` | G |
| `water_physical` | `true` | `true` | G |
| `water_surf_quality` | `2` | `2` | G |
| `water_surf_debug` | `0` | `0` | G |
| `water_surf_wave_height` | `0.600000024` | `0.600000` | G |
| `water_surf_wavelength` | `9` | `9.000000` | G |
| `water_surf_speed` | `1.20000005` | `1.200000` | G |
| `water_surf_choppiness` | `2.5` | `2.500000` | G |
| `water_surf_octaves` | `4` | `4` | G |
| `water_surf_chop_height` | `0.25` | `0.250000` | G |
| `water_surf_segment` | `18` | `18.000000` | G |
| `water_surf_randomness` | `0.75` | `0.750000` | G |
| `water_surf_shore_foam` | `1` | `1.000000` | G |
| `water_surf_shore_width` | `0.600000024` | `0.600000` | G |
| `water_surf_crest_foam` | `0.949999988` | `0.950000` | G |
| `water_surf_foam_cell` | `1.60000002` | `1.600000` | G |
| `water_surf_shallow_tint` | `0.699999988` | `0.700000` | G |
| `water_surf_absorption` | `1` | `1.000000` | G |
| `water_surf_cliff_burst` | `1` | `1.000000` | G |
| `water_surf_no_sea_maps` | `zone7` | `zone7` | G |
| `water_surf_edge_fade` | `40` | `40.000000` | G |
| `bloom` | `2` | `2` | G |
| `emissive_lights` | `2` | `2` | G |
| `emissive_light_strength` | `1` | `1.000000` | G |
| `bloom_strength` | `1` | `1.000000` | G |
| `bloom_threshold` | `1` | `1.000000` | G |
| `depth_of_field` | `1` | `1` | G |
| `depth_of_field_strength` | `0.100000001` | `0.100000` | G |
| `bloom_debug_view` | `false` | `false` | G |
| `ground_cover` | `3` | `3` | G |
| `critters` | `2` | `2` | G |
| `critter_rats` | `true` | `true` | G |
| `critter_rats_weight` | `100` | `100` | G |
| `critter_spiders` | `true` | `true` | G |
| `critter_spiders_weight` | `100` | `100` | G |
| `critter_larvae` | `true` | `true` | G |
| `critter_larvae_weight` | `100` | `100` | G |
| `critter_frogs` | `true` | `true` | G |
| `critter_frogs_weight` | `100` | `100` | G |
| `critter_lizards` | `true` | `true` | G |
| `critter_lizards_weight` | `100` | `100` | G |
| `critter_snakes` | `true` | `true` | G |
| `critter_snakes_weight` | `100` | `100` | G |
| `critter_scolopendras` | `true` | `true` | G |
| `critter_scolopendras_weight` | `100` | `100` | G |
| `critter_dung_beetles` | `true` | `true` | G |
| `critter_dung_beetles_weight` | `100` | `100` | G |
| `critter_small_birds` | `true` | `true` | G |
| `critter_small_birds_weight` | `100` | `100` | G |
| `critter_corvids` | `true` | `true` | G |
| `critter_corvids_weight` | `100` | `100` | G |
| `critter_vultures` | `true` | `true` | G |
| `critter_vultures_weight` | `100` | `100` | G |
| `critter_crabs` | `true` | `true` | G |
| `critter_crabs_weight` | `100` | `100` | G |
| `critter_snow_birds` | `true` | `true` | G |
| `critter_snow_birds_weight` | `100` | `100` | G |
| `critter_snow_mice` | `true` | `true` | G |
| `critter_snow_mice_weight` | `100` | `100` | G |
| `critter_bats` | `true` | `true` | G |
| `critter_bats_weight` | `100` | `100` | G |
| `ambient_particles` | `2` | `2` | G |
| `ambient_particle_density` | `1` | `1.000000` | G |
| `ambient_particle_size` | `1` | `1.000000` | G |
| `shadow_close_range` | `0` | `0` | G |
| `shadow_cascade_quality` | `2` | `2` | G |
| `shadow_close_cascades` | `3` | `3` | G |
| `shadow_far` | `true` | `true` | G |
| `shadow_membership_scroll` | `true` | `true` | G |
| `shadow_neighbour_casters` | `2` | `2` | G |
| `shadow_far_foliage_opaque` | `true` | `true` | G |
| `shadow_early_out` | `true` | `true` | G |
| `hero_light` | `false` | `false` | G |
| `point_shadows` | `true` | `true` | G |
| `point_shadow_quality` | `2` | `2` | G |
| `material_quality` | `3` | `3` | G |
| `material_view` | `0` | `0` | G |
| `material_reflections` | `1` | `1.000000` | G |
| `material_metal_mask` | `true` | `true` | G |
| `material_world` | `true` | `true` | G |
| `material_relief` | `1` | `1.000000` | G |
| `material_world_roughness` | `0` | `0.000000` | G |
| `material_terrain` | `true` | `true` | G |
| `material_terrain_relief` | `0.0599999987` | `0.060000` | G |
| `material_terrain_crevices` | `1` | `1.000000` | G |
| `shadow_softness` | `2` | `2` | G |
| `shadow_sun_angle` | `15` | `15` | G |
| `point_shadow_light_size` | `100` | `100` | G |
| `change_driven_object_poses` | `true` | `true` | G |
| `terrain_far_shadow_prewarm` | `true` | `true` | G |
| `merge_character_parts` | `true` | `true` | G |
| `skip_scenery_draw_walk` | `true` | `true` | G |
| `occlusion_culling` | `true` | `true` | G |
| `render_thread` | `true` | `true` | G |
| `render_thread_frame_latency` | `2` | `2` | G |
| `cull_min_screen_permille` | `5` | `5` | G |
| `shadow_cull_min_texels` | `2` | `2` | G |
| `highlight_briefing` | `true` | `true` | G |
| `highlight_selected` | `false` | `false` | G |
| `highlight_hover_characters` | `true` | `true` | G |
| `highlight_hover_objects` | `true` | `true` | G |
| `cursor_scale` | `100` | `100` | G |
| `pillarbox_fill` | `1` | `1` | G |
| `ui_scale` | `100` | `100` | G |
| `message_panel_width` | `100` | `100.000000` | G |
| `portrait_head` | `true` | `true` | G |
| `portrait_helmet` | `false` | `false` | G |
| `hud_fade_duration` | `250` | `250` | G |
| `hud_fade_in_duration` | `250` | `250` | G |
| `hud_fade_delay` | `2000` | `2000` | G |
| `spell_bar_auto_fade` | `false` | `false` | G |
| `spell_bar_fade_transparency` | `80` | `80` | G |
| `ability_bar_auto_fade` | `false` | `false` | G |
| `ability_bar_fade_transparency` | `80` | `80` | G |
| `minimap_auto_fade` | `false` | `false` | G |
| `minimap_fade_transparency` | `80` | `80` | G |
| `character_info_auto_fade` | `false` | `false` | G |
| `character_info_fade_transparency` | `80` | `80` | G |
| `portraits_auto_fade` | `false` | `false` | G |
| `portraits_fade_transparency` | `80` | `80` | G |
| `weapon_switcher_auto_fade` | `false` | `false` | G |
| `weapon_switcher_fade_transparency` | `80` | `80` | G |
| `quick_items_auto_fade` | `false` | `false` | G |
| `quick_items_fade_transparency` | `80` | `80` | G |
| `message_panel_auto_fade` | `false` | `false` | G |
| `message_panel_fade_transparency` | `80` | `80` | G |
| `quest_items_auto_fade` | `false` | `false` | G |
| `quest_items_fade_transparency` | `80` | `80` | G |
| `stance_auto_fade` | `false` | `false` | G |
| `stance_fade_transparency` | `80` | `80` | G |
| `game_speed_auto_fade` | `false` | `false` | G |
| `game_speed_fade_transparency` | `80` | `80` | G |
| `stance_scale` | `100` | `100.000000` | G |
| `game_speed_scale` | `100` | `100.000000` | G |
| `spell_bar_scale` | `100` | `100.000000` | G |
| `spell_bar_x` | `-1` | `-1.000000` | G |
| `spell_bar_y` | `-1` | `-1.000000` | G |
| `ability_bar_scale` | `100` | `100.000000` | G |
| `ability_bar_x` | `-1` | `-1.000000` | G |
| `ability_bar_y` | `-1` | `-1.000000` | G |
| `minimap_scale` | `100` | `100.000000` | G |
| `minimap_x` | `-1` | `-1.000000` | G |
| `minimap_y` | `-1` | `-1.000000` | G |
| `character_info_scale` | `100` | `100.000000` | G |
| `character_info_x` | `-1` | `-1.000000` | G |
| `character_info_y` | `-1` | `-1.000000` | G |
| `portraits_scale` | `100` | `100.000000` | G |
| `portraits_x` | `-1` | `-1.000000` | G |
| `portraits_y` | `-1` | `-1.000000` | G |
| `weapon_switcher_scale` | `100` | `100.000000` | G |
| `weapon_switcher_x` | `-1` | `-1.000000` | G |
| `weapon_switcher_y` | `-1` | `-1.000000` | G |
| `quick_items_scale` | `100` | `100.000000` | G |
| `quick_items_x` | `-1` | `-1.000000` | G |
| `quick_items_y` | `-1` | `-1.000000` | G |
| `interface_profile` | `default` | `default` | G |
| `quest_items_scale` | `100` | `100.000000` | G |
| `quest_items_x` | `-1` | `-1.000000` | G |
| `quest_items_y` | `-1` | `-1.000000` | G |
| `minimap_border_padding` | `3` | `3` | G |
| `minimap_buttons_scale` | `100` | `100` | G |
| `minimap_buttons_side` | `0` | `0` | G |
| `character_info_border_padding` | `3` | `3` | G |
| `character_info_buttons_scale` | `100` | `100` | G |
| `character_info_buttons_side` | `0` | `0` | G |
| `message_panel_border_padding` | `3` | `3` | G |
| `message_panel_buttons_scale` | `100` | `100` | G |
| `message_panel_buttons_side` | `0` | `0` | G |
| `spell_bar_direction` | `0` | `0` | G |
| `ability_bar_direction` | `0` | `0` | G |
| `portrait_direction` | `0` | `0` | G |
| `weapon_switcher_direction` | `2` | `2` | G |
| `quick_items_direction` | `3` | `3` | G |
| `spell_bar_padding` | `0` | `0` | G |
| `spell_bar_preview` | `false` | `false` | G |
| `spell_bar_preview_count` | `3` | `3` | G |
| `spell_bar_slots_per_column` | `0` | `0` | G |
| `ability_bar_padding` | `0` | `0` | G |
| `ability_bar_preview` | `false` | `false` | G |
| `ability_bar_preview_count` | `3` | `3` | G |
| `ability_bar_slots_per_column` | `0` | `0` | G |
| `portraits_padding` | `0` | `0` | G |
| `portraits_preview` | `false` | `false` | G |
| `portraits_preview_count` | `3` | `3` | G |
| `weapon_switcher_padding` | `0` | `0` | G |
| `weapon_switcher_preview` | `false` | `false` | G |
| `weapon_switcher_preview_count` | `4` | `4` | G |
| `weapon_switcher_slots_per_column` | `0` | `0` | G |
| `quick_items_padding` | `0` | `0` | G |
| `quick_items_preview` | `false` | `false` | G |
| `quick_items_preview_count` | `4` | `4` | G |
| `quick_items_slots_per_column` | `0` | `0` | G |
| `message_panel_text_scale` | `100` | `100` | G |
| `hud_border_scale` | `100` | `100` | G |
| `hud_border_transparency` | `0` | `0` | G |
| `combat_text_scale` | `100` | `100` | G |
| `message_panel_height` | `100` | `100.000000` | G |
| `message_panel_x` | `50` | `50.000000` | G |
| `message_panel_y` | `0` | `0.000000` | G |
| `hud_width` | `2` | `2` | G |
| `point_shadow_fast_overlays` | `4` | `4` | G |
| `point_shadow_merge` | `3` | `3` | G |
| `weather_wind_variation` | `true` | `true` | G |
| `wind_floor` | `0.150000006` | `0.150000` | G |
| `wind_trails` | `2` | `2` | G |
| `precipitation` | `4` | `4` | G |
| `precip_strength` | `0` | `0` | G |
| `precip_test` | `0` | `0` | G |
| `precip_strength_override` | `0` | `0` | G |
| `contact_blend` | `0.699999988` | `0.700000` | G |
| `contact_blend_height` | `0.400000006` | `0.400000` | G |
| `tree_sway` | `true` | `true` | G |
| `tree_sway_strength` | `100` | `100` | G |
| `ao_quality` | `2` | `2` | G |
| `ao_strength` | `0.75` | `0.750000` | G |
| `ao_radius` | `1.5` | `1.500000` | G |
| `ao_low_light_boost` | `1.60000002` | `1.600000` | G |
| `sky_clouds` | `2` | `2` | G |
| `cave_brightness` | `0.699999988` | `0.700000` | G |
| `cave_ambient_floor` | `0.300000012` | `0.300000` | G |
| `cave_gi` | `2` | `2` | G |
| `bounce_debug` | `false` | `false` | G |
| `sun_bounce_strength` | `1` | `1.000000` | G |
| `cave_gi_strength` | `1` | `1.000000` | G |
| `sky_brightness` | `1` | `1.000000` | G |
| `sky_night_brightness` | `1.5` | `1.500000` | G |
| `sky_cloud_shadows` | `1` | `1.000000` | G |
| `sky_cloud_shadow_speed` | `8` | `8.000000` | G |
| `sky_cloud_wind_speed` | `25` | `25.000000` | G |
| `sky_cloud_motion` | `1` | `1.000000` | G |
| `fog_preset` | `2` | `2` | G |
| `fog_volume_quality` | `1` | `1` | G |
| `fog_debug_component` | `0` | `0` | G |
| `sun_shafts` | `3` | `3` | G |
| `sun_shaft_strength` | `2` | `2.000000` | G |
| `fog_strength` | `1` | `1.000000` | G |
| `fog_test_dense` | `false` | `false` | G |
| `fog_visibility` | `5000` | `5000.000000` | G |
| `fog_density` | `0.100000001` | `0.100000` | G |
| `fog_morning_chance` | `0.349999994` | `0.350000` | G |
| `fog_after_rain_hours` | `4` | `4.000000` | G |
| `neighbour_maps` | `true` | `true` | G |
| `load_reveal_hold` | `true` | `true` | G |
| `neighbour_fog_range` | `1` | `1.000000` | G |
| `ground_cover_distance` | `300` | `300` | G |
| `ground_cover_shadows` | `2` | `2` | G |
| `effect_distance` | `500` | `500` | G |
| `impostors` | `true` | `true` | G |
| `impostor_pixels` | `64` | `64.000000` | G |
| `impostor_debug` | `false` | `false` | G |
| `lod` | `true` | `true` | G |
| `lod_pixel_error` | `1` | `1.000000` | G |
| `draw_distance` | `500` | `500` | G |
| `terrain_lod` | `true` | `true` | G |
| `terrain_quality` | `2` | `2` | G |
| `terrain_antitile` | `true` | `true` | G |
| `terrain_cliffs` | `true` | `true` | G |
| `cliff_rock_amount` | `0.550000012` | `0.550000` | G |
| `cliff_rock_softness` | `0.150000006` | `0.150000` | G |
| `cliff_rock_scale` | `1` | `1.000000` | G |
| `cliff_streak_length` | `2` | `2.000000` | G |
| `cliff_steep_start` | `40` | `40.000000` | G |
| `cliff_steep_full` | `60` | `60.000000` | G |
| `cliff_snow_rock` | `0.349999994` | `0.350000` | G |
| `cliff_face_warp` | `0.600000024` | `0.600000` | G |
| `cliff_rock_detail` | `0.600000024` | `0.600000` | G |
| `neighbour_fog_skip` | `true` | `true` | G |
| `terrain_blend_neighbours` | `true` | `true` | G |
| `terrain_blend` | `1` | `1` | G |
| `terrain_tessellation` | `1` | `1` | G |
| `terrain2_cache` | `0` | `0` | G |
| `terrain2_cache_density` | `100` | `100` | G |
| `terrain2_cache_bake` | `4096` | `4096` | G |
| `terrain2_cache_bias` | `0` | `0.000000` | G |
| `terrain_height_blend` | `true` | `true` | G |
| `sky_weather_test` | `0` | `0` | G |
| `critter_labels` | `0` | `0` | G |
| `fur_mode` | `1` | `1` | G |
| `fur_shells` | `16` | `16` | G |
| `fur_length` | `100` | `100` | G |
| `fur_density` | `100` | `100` | G |
| `fur_quality` | `1` | `1` | G |
| `critter_showcase` | `0` | `0` | G |
| `combat_portrait_effects` | `true` | `true` | G |
| `combat_healthplates` | `true` | `true` | G |
| `combat_plate_combat` | `false` | `false` | G |
| `combat_plate_alt` | `true` | `true` | G |
| `combat_plate_paused` | `true` | `true` | G |
| `combat_plate_hover` | `true` | `true` | G |
| `combat_plate_all` | `false` | `false` | G |
| `combat_plate_idle_opacity` | `50` | `50` | G |
| `combat_plate_width` | `120` | `120` | G |
| `combat_plate_height` | `8` | `8` | G |
| `combat_plate_spacing` | `28` | `28` | G |
| `combat_plate_stacking` | `true` | `true` | G |
| `combat_plate_stack_gap` | `4` | `4` | G |
| `combat_icon_scale` | `1.14999998` | `1.150000` | G |
| `combat_split_effects` | `true` | `true` | G |
| `combat_effect_outlines` | `true` | `true` | G |
| `combat_hp_values` | `true` | `true` | G |
| `combat_hp_percent` | `false` | `false` | G |
| `combat_hp_text_size` | `18` | `18` | G |
| `combat_max_effects` | `3` | `3` | G |
| `combat_durations` | `true` | `true` | G |
| `combat_duration_size` | `10` | `10` | G |
| `combat_priority` | `29,34,25,32,30,15,38` | `29, 34, 25, 32, 30, 15, 38` | G |
| `combat_effect_priorities` | `9:99,10:99,11:99,15:102,25:102,26:101,29:102,30:102,31:101,33:101,34:101,38:102` | `9:99, 10:99, 11:99, 15:102, 25:102, 26:101, 29:102, 30:102, 31:101, 33:101, 34:101, 38:102` | G |
| `combat_blacklist` | `13,14,12` | `13, 14, 12` | G |
| `combat_buff_overrides` | `` | `` | G |
| `combat_debuff_overrides` | `16,50` | `16, 50` | G |

Настройки S не являются вторым renderer-quality preset: это источник фактов
legacy/game terrain/camera и flags. Политика современных эффектов — в G.

| S: секция / параметр | Значение | Передача / граница |
|---|---:|---|
|General Settings / VideoResolution|3840×2160|Игровое окно. В маппере native target — размер viewport; тестовый output1280×720 задан заданием, камера — те же восемь кадров.|
|VideoColorDepthInBits / VideoZBufferDepthInBits|32 /16|Не навязываются D3D11 native backend; форматы определяет тот же renderer (включено в backend-отступление).|
|Antialiasing|1|Legacy device flag; native SMAA=true берётся из G, собственное AA не вводится.|
|Dithering / Specular|0 /0|Legacy fixed-state policy. Native PBR регулируется G; defaults fixed-state host не включают specular. Полная TL state snapshot из игры отсутствует.|
|TextureQuality / Mipmapping / Filtering|0 /0 /2|Нет дополнительной редакторской обработки текстур. Pinned typed surface path и native anisotropy4 сохранены; equivalence всех retail TL sampler states не доказана.|
|PerspectiveCorrection|1|Общий 3D pipeline renderer; отдельной экранной коррекции хоста нет.|
|ShowParticles|1|MOB/game runtime particles отсутствуют как полный игровой поток; native ambient_particles2/precipitation4 из G сохранены. См. неполные входы.|
|Brightness / Contrast / Gamma|0 /0 /0|Нейтральные значения; host grade отсутствует. Не утверждается наличие полного игрового TL gamma/state потока.|
|Terrain Settings / LOD1 / LOD2|90 /130|Передаются в OwnedTerrainFrameInputs; прежние10000/20000 сняты. Terrain_lod и качество — G.|
|EnableWaterWaves / EnableDynamicLights|1 /1|Читаются из S и передаются/применяются как flags источника.|
|BorderFogDistance|8|Требует runtime terrain fog extents/flags; они не восстановлены из полного игрового terrain state. Не подменено своим эффектом.|
|EnableSelfShadowing|0|Legacy terrain shadow state не эмулируется; native shadows из G сохранены.|
|EnableFootprints/Bloodprints/Fireprints/BloodprintsRelight|1/1/1/0|В редакторе нет соответствующих live game events; не синтезируются.|
|Camera Settings / NearClipDistance / FarClipDistance|1 /199680|Рабочая камера маппера общая; для кадров projection near1/far2000 как в предыдущих captures. Native draw_distance500 из G.|
|Game Specific Settings / DefaultWindForce|.4|Raw MOB wind передан, нормализованная сила/phase runtime не известны; собственная формула не объявляется игровой.|
|DefaultTimeScale|60|Нет запуска game tick/time-scale simulation; редакторский час и preview clock описаны ниже.|
|ObjectsLightingCoeff|1|Самостоятельный коэффициент host не добавлялся; источник modern world light — таблицы.|
|FogNightStartDistance / FogDayStartDistance|50 /90|Читаются из S; текущие тестовые часы используют day90. Исходный TL fog snapshot отсутствует; это отмечено ниже.|

Все прочие поля S относятся к аудио, вводу/движению камеры, интерфейсу и gameplay;
они не переписываются и не превращаются в настройки Qt маппера. Источники G/S
и шесть Lights-файлов сверены по SHA до/после тестов:
T/game-input-source-hashes.json. Cache copies Lights и settings.ini побайтно
проверяются отдельно; G сравнивается по340 effective values, поскольку его
копия содержит только перечисленные исключения и собственный cache bookkeeping.

### Входы игры и хоста: подтверждённое и недостающее

Проверены только публичные typed adapters и public engine_bridge указанного
lock SHA `81dbcdd29b41baeb346f167c6dfb511dff05f295`. Приватный код движка
не читался и не копировался. Reference contracts: E1World.cpp/h, E1Terrain.cpp,
E1VisualFrame.cpp, E1AmbientEffects.cpp, model_lighting.h, world_fog.h,
sky_clouds.h, cave_lighting.h, engine_bridge.cpp (world environment/light manager).
Доступ к фактам интерфейса не означает наличие их игрового производителя в
редакторе. В таблице нет заявления «все входы совпали».

| Вход | Игровой путь pinned adapters | Хост R3 / действие | Статус |
|---|---|---|---|
|Camera/world_to_clip|E1WorldFrame camera snapshot|Общая камера маппера, D3D depth0..1; те же позиции/FOV кадров|Контракт и конверсия проверены; камера намеренно управляется редактором|
|Час / region / cave|game_hours=ticks×scale+base, wrapped hour; region выбирает Lights; map.txt cave|Час панели/WS_TIME и реальный map.txt catalog: Gipat zone1, Ingos zone12, cave zone12dun2|Выбор source metadata проверен; unwrapped game days/ticks отсутствуют|
|Ambient/sun/sky colours|ModelLightingFacts: packed ambient, source diffuse; Lights[Cave]region tables|GL renderEnvironmentForValues исключён. Чтение реальных таблиц, source interpolation; общий produce_model_lighting выполняет packing/направление, gamma-grade нет|Файлы и typed conversion проверены; полный byte-for-byte runtime tuple игры без её производителя не засвидетельствован|
|Sun track / cave|sky_clouds.h документирует clock track; cave_lighting.h — cave table index0 и fixed ray|Clock formula по публичному adapter contract; cave row0 и fixed ray, затем обычный cave_brightness=.7/floor.3|Не вводит редакторский cave sentinel или цвет|
|Fog colour / clear|Источник — hourly sky colour; modern fog response внутри renderer|Тот же source sky в captured_fog_color/clear; stock_sky удалён|Источник цвета проверен; итоговый haze не корректируется|
|Fog TL start/end/projection|Игра читает реальный TL snapshot start/end/scale и _33/_34/_43|Day90/night50 из S, projection общей камеры; прежние180/620 сняты. Выбор day/night по clock track, полного игрового TL snapshot нет|Частично: переход/clip/runtime extents требуют авторского source seam; строгая эквивалентность не доказана|
|Owned sky|E1ClaimSky запрашивает штатный sky при активном world frame|E1ClaimSky вызывается, sky_clouds2 и weather tuple получает штатный renderer|Gate/config переданы; stock replacement отсутствует|
|World animation clock|world_time — segment+fraction, .055sec/segment, pause/game speed|Независимый preview clock, не связанный с hidden GL paint; в снимках явно segment0|Единицы/передача проверены, не тот же игровой момент/pause/script state|
|Weather/rain/snow|CWeather current/next/start/length, particle power, precipitation strength|Редактор не исполняет game world/scripts. Передан unloaded/clear tuple0, native precip_test/strength из G не отключены|**Неполный игровой вход**; не выдаётся за актуальную погоду игры|
|LIGHT|Готовые point manager center/color/radius/flags; native attenuation/shadows|Все доступные map LIGHT без GL frustum/lighting mode, raw radius/color/position, без +2Z и без GL-эвристики деления color>1 на255. В zone1 передано6, native GI их принимает|Typed publication проверена; runtime manager IDs/attachments/fades полностью не воспроизведены|
|TORCH|Адаптер читает уже созданный runtime CLight; flame-center lift .951×particle scale, затем native light path|R2 (.8,.8,.8), power×10 были синтезом из GL и не имеют доказанного MOB→CLight контракта. Они удалены, а не объявлены игровыми|**4 TORCH-света zone1 теперь не переданы. Требование сохранить весь свет не выполнено до ответа Ильи.**|
|GI light tables|Renderer читает Config/lightsregion.ini относительно последнего Maps-root|В private game-work скопированы неизменённые6 Lights-файлов, добавлен пустой Maps-root. Раньше light table no, теперь yes|Исправлена доказанная потеря source context; ни Base Maps, ни чужие файлы не создаются|
|MPR geometry/material/water|Shared TerrainCpuAsset parser, source pages и properties|Immutable memory MPR, water color/opacity/depth из карты, source pages до swamp classifier сохранены из R2|Ingress исправлен;19/21 source pages найдены,2 отсутствующих optional pages зафиксированы ранее|
|Texture mips / Terrain2|Общий typed surface path / renderer reconstruction|001–007, без010/009; terrain_blend1, antitile/PBR/water из G|Нет редакторского исправления вида; каждый producer игры отдельно не засвидетельствован|
|Terrain wind / animated sectors / border fog|Полный runtime NativeTerrainState, phase arrays и dynamic layer shifts|Raw MOB wind и file-backed wave flag; LOD90/130. Нормализованная сила/phase, runtime layer shifts/fog extents не восстановлены; baseline frozen helper|**Неполный игровой вход**; GL/придуманная нормализация не подставлена|
|Ambient particles/cover/critters|Renderer генерирует по G, shared environment/terrain/visual facts|G оставлен целиком, E1AmbientEffects обычный; доступные retained statics/terrain переданы|Генератор не менялся; полнота зависит от недостающих runtime facts|
|MOB emitters, flame particle stream, units, scripts|Готовые particle/pose/lifetime facts игры|Полного live игрового producer нет; старые ограничения units/stst8/742 и динамики сохраняются|**Не заявляется полнота сцены/частиц как в игре**; это не имитируется собственным цветом/постпроцессом|
|Neighbours/world reveal|Настройки neighbour_maps/load_reveal_hold и runtime placement/layout/state|G не менялся; выбранный memory document/профиль. Полного игрового runtime layout/reveal state нет|Полнота этого входа не подтверждена|

Таким образом, правило «ни своих значений цвета/вида» выполнено в части удаления
политики и синтетических TORCH values, но **полный запрос части3 о всех игровых
входах остаётся невыполненным**. Сохранены ABI/light publication для реальных
LIGHT, source-pages fix, панели и one-shot crash protection. Скрывать потерю
четырёх TORCH под словами «совпадает с игрой» нельзя. Нужен публичный producer
из MOB/MPR/clock или документированный DTO-контракт автора, без копирования
приватного engine code и без запуска игры агентом.

### Заливка, пятна, болото и зерно

На coast/top большая светлая область ниже горизонта осталась. R3 не рисует
вместо неё цилиндр/облака и не исправляет цвет. Проверены штатный sky gate,
sky_clouds2, source hour/sky colour и обычный fog/sky shader path, в котором
нижняя полусфера использует haze. **Не проверено**, что такой же кадр есть в
запущенной игре: её эталона нет, полный runtime tuple ещё неполный. Поэтому
утверждение «заливка точно как в игре» не делается; владелец сверит кадр.

Пена/белые water patches, modern shallow tint/reflectance, Terrain2 noise и
material response оставлены штатными. Ни water_simple, ни material_terrain,
ни terrain_blend ради вида не переключаются. Для swamp сохранён только
обоснованный ingress source pages; дальнейшая пестрота/переходы определяются
материалом карты и обычным renderer. Вопрос Илье о маленьком прямоугольнике
foam возможен как о предполагаемом артефакте discretization; это не назначено
ошибкой из-за отличия от GL. Пещера использует .7/.3 и обычное отключение sun
самим renderer, а не008/редакторский свет. Сравнения яркости/контраста с GL
в R3 не проводились и не служат приёмкой.

### Проверки и замеры

Итоговые случаи — T/case-configured-raw-zone1, -zone12, -zone12dun2.
Во всех exit0,340 effective config values сверены,0 visible process HWNDs,
новые кадры приняты renderer; источник PNG — только собственный readback.
Последние кадры заново сняты после bootstrap, ModelLightingFacts и GI input
исправлений. Более ранние case-native/configured/configured-final — история
исследования; они не используются как итоговый input/config PASS.
Неподходящий первый прогон с defaults явно исключён.

| Кадр | Ready ms | n | frame min ms | median ms | p95 ms | max ms |
|---|---:|---:|---:|---:|---:|---:|
| zone1-village-1240 | 1447 | 21 | 15.68 | 16.63 | 16.99 | 17.36 |
| zone1-coast-1240 | 869 | 21 | 16.01 | 16.64 | 16.80 | 17.26 |
| zone1-top-1240 | 831 | 21 | 15.68 | 16.62 | 16.82 | 17.56 |
| zone1-village-1930 | 861 | 19 | 15.96 | 16.68 | 17.17 | 17.17 |
| zone1-coast-1930 | 887 | 15 | 15.44 | 16.64 | 17.84 | 17.84 |
| zone1-top-1930 | 840 | 21 | 14.18 | 16.54 | 16.79 | 19.02 |
| zone12-1240 | 1329 | 21 | 16.36 | 16.65 | 16.98 | 45.97 |
| zone12dun2-1240 | 877 | 21 | 16.34 | 16.64 | 16.81 | 18.46 |

frame_ms — wall time DLL frame call, включая map submission, composite,
Present/пейсинг и memory query; **не GPU timestamp** и не цена PNG capture.
n — реально полученные samples, часть queued Qt notifications объединяется;
это короткий замер, не статистический performance benchmark. Ready — от
выбора вида до первого принятого кадра для указанной камеры. Затем ждём ещё
>20 frame events/pending0 перед readback. Снимки замораживают world segment0;
обычный preview clock идёт независимо от GL. Один renderer source/одна машина
RTX4070SUPER, фоновая нагрузка владельца не контролировалась и не останавливалась.
Настройки по результату времени не снижались. Timeout ожидания готовности
повышен30→120sec для полного профиля; до него ни один итоговый запуск не дошёл.

Финальный GL-контроль — один прогон T/case-gl-control-final, прежняя камера
village1240, реально прочитанный water uniform0. С R2-final отличаются3 из
921600 пикселей, mean absolute RGB .00004919/255; учтены два известных водных
состояния, без нового запуска старого EXE. Это technical GL regression check,
а не сравнение качества Rebuild64. T/gl-control.json.

В логах остаются libpng IDAT CRC warnings; тот же класс был в R2 (16 против
10 в исходном zone1 прогоне R3). Исходники игровых ресурсов не исправлялись,
по этому предупреждению не объявлен новый renderer PASS/дефект и оно не
скрыто. Итоговая сборка шейдеров не имеет compilation failures.

Панельный код/переезд/native parent/DPR-логика и selectRebuild guard из R2 не
менялись; повтор DPR проверок не назначался. Их предыдущая доказательная
граница сохраняется: readback + QWidget::render не показывают реального
наложения на видимую D3D-поверхность; никто этого здесь не видел. Это проверка
владельца. Реальный AV и one-shot retry уже проверены в R2 для обоих saved
preferences; эта логика сохранена. При выбранном OpenGL новых изменений
изображения/инструментов/Save/MCP/uiScalePercent в R3 нет.

### Сборка, staging, сохранность и оставшиеся вопросы

Полностью пересобран pinned runtime с001–007:98 объектов, SHA lib
`17e5bb43c8754d33f255c1dd4d96ac5a22789b4806db596603a7dac2b80b13ca`.
Затем host последовательно перелинкован с этой неизменной целой lib после
исправлений входа. Одного obj splice нет. Материал исходной пробы/282 не
менялся; переиспользован собственный source283/work/r2-renderer build root,
а не ещё одна полная копия.638 prebuilt shader resources сохранены:190
headers/HLSL и34 raw shader blocks в public CPP совпадают с282. Шейдеры
не менялись, новая таблица не нужна. Shader bin SHA
`38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64`.
Проверены DLL imports/no NVIDIA SDK и единственный export ataman_renderer_get_api.

Release из git archive tree `3dc00032c5dfc908892ad3b418f7c5ff8b5fbaf8`.
Wrapper вернул1 на известной передаче нормализованного пути с пробелами в
x86.cmd; это не записано как PASS. Qt/MCP собраны; оба runtime из **того же
архива** собраны напрямую по8.3 пути, exit0/0, затем включены в пакет.
Raw/staged --version283 exit0, одинаковый SHA.

Mapper SHA256 `f9394979fcb30eb4a2bdb861af42352c71856d7c8a950c00657262532ed779f2`.
DLL SHA256 `9f1d59dffa63852d0d69dfd1159a91e939bb22bcc4bb53f1f7bbb56151601ef7`.
[Новый staging](../../build-staging-283-rebuild64-stage3-r3-20261106/): финализатор exit0,
1364→1364 против282,0 потерь,0 добавленных файлов (список добавленных пуст).
[Квитанция](../../work/rebuild64-renderer-stage3-283-20261106/r3/package-receipt.json).
Staging после финализации не запускался/не изменялся. **Он не является
подтверждённым полным игровым preview** из-за перечисленных пробелов входа.
Установка и push не выполнялись. Настройки владельца, AppData/ProgramData,
установка282, мосты/чужие процессы не затрагивались; subst пуст, junction нет.

Защищённые CPP/H/QM/.mcp.json/отчёт277 сохранены по SHA и исключены из archive/
коммита, свои строки ROADMAP отделены. VERSION остаётся283. Из программы
сняты только перечисленные stock_sky/GL policies/008–010 и неподтверждённый
синтетический TORCH input; инструменты/горячие клавиши/сохранение/форматы/MCP
не менялись, MPR/MOB/RES новый вид не записывает. Очистка тестовых копий и
оставленный build/evidence материал — T/cleanup-plan.json и W/cleanup-plan.json.
Удалены6 собственных map/resource payloads, 8179477 байт;18 копий Lights и3 settings.ini побайтно совпали с источниками.
Удалены также только5 устаревших копий снятых файлов из собственного archive/host cache, не материал282/пробы.

Илье нужен конкретный контракт: как из MOB TORCH_STRENGHT/PTLINK получить
точные runtime CLight color/radius/scale/attachment, и есть ли публичный
preview producer для CWeather, unwrapped clock, нормализованного wind/phase,
terrain fog/sector state и связанных emitters/pose facts. Отдельно — нормативный
TL fog tuple/day-night transition в standalone host и, если foam-квадрат
непреднамеренный, его проверка в renderer. Авторское разрешение уже закрыто;
это технические вопросы о данных, не повторный запрос разрешения.

Порядок до default: закрыть source contracts/полноту входа → владелец сверяет
с игрой → process isolation для сохранности памяти → service/picking → sector
deltas → units/stst8/динамика → приёмка. Прежний GL visual gate отменён.
**STOP BEFORE INSTALLATION, no push.**
