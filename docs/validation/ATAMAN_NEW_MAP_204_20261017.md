# Ataman 204: новые карты, полный каталог и gipat1

Дата: 2026-10-17. Задача EI-NEW-MAP-204. База main994c420b04f3b39be1bc2d0d63b920a4cfb4defa/203. Поручено исправление, восстановление gipat1 без потери рельефа, проверки, сборка и установка. Новое ядро переходов не возвращалось.

## Причины и исправления

1. Live-журнал203: gipat1 expectedTerrainAtlases19, loaded0; отсутствуют gipat1000.mmp–gipat1018.mmp. New MPR менял внутреннее имя, сохраняя только MP/SEC. Теперь точные MMP одного согласованного источника копируются под новым именем в соседний <map>-atlas.res, туда переносится привязанный к пикселям профиль. MPR и RES сохраняются через commitMapSaveBatch с проверкой, резервом и rollback. Без полного пригодного атласа запись не начинается. Шаблон не заменяется. После создания сняты поисковый/used-only фильтры и включена Modern-палитра.
2. Native через настоящий New MPR выявил AV первого minimap capture. GDB thread1: glDrawElements → CSector::draw → CLandscape::draw → CView::captureMinimapScene → MainWindow::refreshViewportMinimap; crash thread NVIDIA DrvPresentBuffers. unloadLand завершался doneCurrent, loadLandscape создавал буферы секторов без восстановления контекста. Теперь makeCurrent стоит непосредственно перед readMap.
3. Save/reopen новой карты выявил terrain.atlas_profile_layer_mix на1196: сохранённый Grass/Sparse light + Swamp переход терял общую разрешённую область. workingAtlasProfile сохраняет прежнюю совместимость и разрешения фактических переходов рабочего каталога, учитывая владельца маршрута и покрытие. Основные категории палитры, runtime type и сами пиксели не меняются.

## Достаточные проверки

Fresh Release204 и обе32-bit runtime-компоненты собраны штатным wrapper/Qt5.12.12-MinGW7.3. Native production MainWindow/QDialog/GL, скрытые окна на приватных копиях: **412 PASS, exit0**. Источник tests/new_terrain_canvas_check.cpp; журнал build-new-map-204-20261017/final.out.

- Реальный мастер создаёт карту2×1 с новым внутренним именем, отличным от имени файла, и неплоской PNG heightmap; записаны оба файла, карта открыта.
- При прежнем used-only фильтре после создания видны все81 ground и7 overlay ID; проверен каждый из88 реальных источников пикселей через production loader. Полная палитра проверена и после reopen.
- Аппаратный OpenGL: lit/unlit mini-map capture после document transition. Реальные mouse-штампы земли и добавления покрытия; точные Undo/Redo обоих действий.
- Save/reopen сохраняет рисунок, активные поверхности и всю сетку высот; после reopen ещё один mouse-штамп и второй Save/reopen проходят с точным рельефом.
- Отсутствующий источник атласа возвращает отказ с пустым результатом до публикации.
- Source MPR обеих карт не меняются. Проверено 462 исходников/заголовков; все старше финального EXE, hashes в compiler-source-hashes.json. Код теста отслеживает productionObjects через PRE_TARGETDEPS, исключая stale-link.

Промежуточные неуспешные проверки не объявляются PASS: первый modal timeout исправлен в test driver; подтверждённый GPU AV устранён в production; test-only preflight раньше разрешал лишь общий RES, затем явно разрешён private sibling RES. Первое повторение profile-fix оказалось stale-link теста; dependency исправлена. Решающая проверка412 выполнена с актуальными production objects. Общий clipboard UI-smoke не нужен для затронутого сценария и не запускался.

## Восстановление gipat1

Единственный точный MP-донор по всем байтам после maxZ/размеров — zone1.mpr текущей DejaVu. Резерв build-new-map-204-20261017/rescue/gipat1.mpr, SHA256 `9adf9cff2a31f189c9a7108d9c0a746e71a4b8ef0e255efe882bae21babe3ec2`. Проверены19 страниц, точный decoded hash донора, пригодный профиль и полная неизменная сетка высот. Для восстановления подготовлен только новый gipat1-atlas.res, SHA256 `e214f17bca7d374d497e373f32aec2f5789b17828860729c55ceb161fc6001fe`; MPR не заменяется и его байты останутся прежними. Live-commit и установка фиксируются отдельными receipts.

EXE SHA256 `b8aef137c04a9b330268544b67d13f0c1c5345371380a6f2c51f883a8ee02eee`. Сборка build-release-204-new-map; рабочие receipts build-new-map-204-20261017. Ручная оценка изображения владельцем и запуск в игре не выполнялись. GitHub не обновлялся.
