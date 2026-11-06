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
