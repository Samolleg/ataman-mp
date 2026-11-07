# EI-REBUILD64-LAUNCH-287 — запуск и полная карта переноса (2026-11-07)

База инвентаризации: main `8668cfc49e017aec23be93641f1e75c7953bc124`, VERSION286. AGENTS SHA256 `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7`. Исходные195 функциональных строк и861 низкоуровневое определение были записаны до программных правок. В ходе проверки отдельно вынесены double-click и MOB без MPR, уже находившиеся в исходном перечне методов; итоговая функциональная таблица197 строк. Дополнительная проверка free/inline функций и macro wrappers приведена ниже. Таблица построена по исходникам HEAD; старые отчёты не использованы как доказательство полноты или работоспособности.

Решение владельца2026-11-07 дословно: «переноси все функции на новый рендер, не тормози. Включая запуск маппер и открытия карты». Общая цель EI-REBUILD64-MIGRATION: все функции, запуск и открытие карт в Rebuild64; OpenGL — запасной переключатель. Этот этап287: inventory, запуск, default/migration/guard/fallback, первый готовый кадр, lazy legacy work, отсутствие молчаливых пропусков. Этапы288+ без отдельного поручения не начинать. Вид Rebuild64/шейдеры не менять.

## Метод и границы перечня

Функциональные строки — пользовательские операции, не только известные инструменты286. Низкоуровневые реализации перечислены отдельно: все определения CView, непосредственные GL-вызовы и основные GL-примитивы. Дополнительно пройдены MainWindow slots, все UI-файлы, operationmanager, Undo, resource manager, все MCP методы (число ниже вычислено из `ai::toolSchemas`, а не задано вручную). Документные операции включены и тогда, когда их данные CPU-only: их конечная визуализация/позиционирование всё ещё связано с CView. Тестовые *_check исключены из пользовательских функций; их находки не доказывают продуктовый путь.

«Работает по коду» означает связанный production путь, а не новый runtime PASS. «Не проверялась» не заменяет проверку и не означает native поддержку. GL-only preview/FBO не считается native кадром. Отдельных инструментов создания воды/рек/дорог, помимо MPR liquid/overlay, лепки, material paint и MCP carve_river/auto_paint, в обработчиках не обнаружено; эти реализации перечислены, самостоятельная кнопка не выдумана. Вода native принадлежит рендеру, хост не подменяет её GL-картинкой.

## Часть1. Функциональная таблица состояния286, до кода287

| Функция | Код286 (файл:строка) | Состояние в286 | Как переносить / временный путь | Этап |
|---|---|---|---|---|
| Обычный запуск / сохранённый backend | source/render/rebuild64_viewport_widget.cpp:259 (`CRebuild64Viewport::CRebuild64Viewport`) | работает по коду; в 287 ещё не проверялась | default + однократная миграция; guard имеет приоритет | 287 |
| Пустой viewport / нет карты | source/render/rebuild64_viewport_widget.cpp:413 (`if(!m_gl->m_pLand`) | не проверялась | отдельная Qt-страница состояния без GL/D3D кадра | 287 |
| Первый запуск / Resource Setup | source/app/main_window.cpp:1569 (`showGameAndModSetup()`) | не проверялась | сохранить штатный диалог; скрытая проверка настоящего main | 287 |
| Восстановление автосохранений | source/app/main_window.cpp:8706 (`MainWindow::showRecoverableAutosaves`) | не проверялась | тот же документ/Save; перед первым Present барьер готовности | 287 |
| Стартовые аргументы MPR/MOB | source/app/main.cpp:4475 (`QStringList startupDocuments`) | не проверялась | общий путь openDocument; не показывать предыдущую карту | 287 |
| Предыдущая сессия | source/app/main_window.cpp:7103 (`MainWindow::loadLastSession`) | не проверялась | те же документы; дождаться окончательной когорты MOB | 287 |
| Меню Открыть | source/app/main_window.cpp:8202 (`MainWindow::on_actionOpen_triggered`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Открыть MPR | source/app/main_window.cpp:8732 (`MainWindow::on_actionOpen_MPR_triggered`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Открыть MOB / AddMob | source/app/main_window.cpp:9662 (`MainWindow::on_actionOpen_MOB_triggered`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Отдельный MOB / коллекция без MPR | source/render/rebuild64_viewport_widget.cpp:413 (`if(!m_gl->m_pLand`) | молча не работает в нативном кадре | явный переход в GL с сообщением в287; native мир без terrain — отдельный штатный ingress | 287 (guard), 289–290 (native) |
| Недавний документ | source/app/main_window.cpp:10124 (`MainWindow::on_actionOpen_recent_triggered`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Замена карты / сосед | source/app/main_window.cpp:7402 (`MainWindow::openMapPairWithCameraPolicy`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Создание MPR | source/app/main_window.cpp:8744 (`MainWindow::on_actionNew_MPR_triggered`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Создание MOB | source/app/main_window.cpp:9187 (`MainWindow::on_actionNew_MOB_triggered`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Коллекция документов | source/app/main_window.cpp:1314 (`MainWindow::openCollection`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Закрытие всех / повторное открытие | source/app/main_window.cpp:8167 (`MainWindow::closeAll`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Завершение приложения | source/app/main_window.cpp:9928 (`MainWindow::closeEvent`) | не проверялась | общий CView-владелец документов; барьер публикации/очистка старого кадра | 287 |
| Галерея карт / двойной щелчок | source/ui/maps_browser.cpp:92 (`CMapsBrowser::activateItem`) | не проверялась | общий MainWindow::openMapPair | 287 |
| Карта мира / открытие / переключение | source/app/main_window_travel.cpp:154 (`openMapPair(mpr,mob,target.zoneId)`) | не проверялась | общий сигнал открытия карты | 287 |
| Headless main / разрешения / невидимые виджеты | source/app/main.cpp:4216 (`if(agentHeadless)`) | не проверялась | оставить GL для нужных MCP FBO; не выбирать/не создавать D3D | 287 |
| Startup guard / аварийная метка | source/render/rebuild64_viewport_widget.cpp:274 (`m_interruptedStarts=`) | работает по коду; в 287 ещё не проверялась | миграция не снимает latch; явное меню повторяет | 287 |
| Fallback DLL / Config64 / D3D / frame | source/render/rebuild64_viewport_widget.cpp:346 (`CRebuild64Viewport::fallback`) | работает по коду; в 287 ещё не проверялась | одно русское сообщение + полноценный GL | 287 |
| Подготовка шейдеров / кеш / параллельные экземпляры | tools/rebuild64_renderer/host/host.cpp:161 (`probe1b_create(`) | не проверялась | не менять шейдеры; прогресс и безопасный кеш в слое встраивания | 287 |
| Соседние GL-ландшафты и объекты | source/render/view_neighbor_load.cpp:20 (`CView::loadNeighborLandscapes`) | работает по коду; в 287 ещё не проверялась | отложить GL, сохранить размещения; по требованию GL-функций | 287 |
| Native соседние карты | tools/rebuild64_renderer/host/embed_api.inc:26 (`host_document(`) | работает по коду; в 287 ещё не проверялась | сохранить native stream + all-neighbours/materials barrier | 287 |
| Смена набора MOB/видимости | source/render/rebuild64_viewport_widget.cpp:430 (`const bool cohortChanged`) | работает по коду; в 287 ещё не проверялась | одна immutable generation, не смешивать ordinals | 287 |
| Обновление объекта / полный reload | source/render/rebuild64_viewport_widget.cpp:272 (`CRebuild64Viewport::tick`) | работает по коду; в 287 ещё не проверялась | пока preview и полный commit; штатный object ingress спросить Илью | 288 |
| Клик / Shift / рамка | source/render/view.cpp:11420 (`CView::pickObject`) | работает по коду; в 287 ещё не проверялась | общий скрытый GL query; ID/depth native — согласованный API, без подгонки | 291 |
| Выделение по параметрам | source/render/view.cpp:11229 (`CView::select(`) | работает по коду; в 287 ещё не проверялась | общий скрытый GL query; ID/depth native — согласованный API, без подгонки | 291 |
| Программное выделение / outliner | source/render/view.cpp:11971 (`CView::selectNodes`) | работает по коду; в 287 ещё не проверялась | общий скрытый GL query; ID/depth native — согласованный API, без подгонки | 291 |
| Рамка выделения | source/render/view.cpp:16333 (`CView::drawSelectFrame`) | работает по коду; в 287 ещё не проверялась | общий скрытый GL query; ID/depth native — согласованный API, без подгонки | 291 |
| Мировая позиция под мышью | source/render/view.cpp:16439 (`CView::getTerrainPos`) | работает по коду; в 287 ещё не проверялась | общий скрытый GL query; ID/depth native — согласованный API, без подгонки | 291 |
| Положение для вставки | source/render/view.cpp:24608 (`CView::objectPlacementPositionAt`) | работает по коду; в 287 ещё не проверялась | общий скрытый GL query; ID/depth native — согласованный API, без подгонки | 291 |
| Move / XYZ / привязка / ограничения | source/render/view.cpp:25518 (`CView::moveTo`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Поворот | source/render/view.cpp:25537 (`CView::rotateTo`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Масштаб | source/render/view.cpp:25565 (`CView::scaleTo`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Подтверждение операции | source/render/view.cpp:25311 (`CView::operationApply`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Отмена незавершённого | source/render/view.cpp:24975 (`CView::cancelPendingOperation`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Удаление | source/render/view.cpp:25605 (`CView::deleteSelectedNodes`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Дубликат | source/render/view.cpp:26950 (`CView::duplicateSelectedObjects`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Копирование объектов во временный буфер | source/render/view.cpp:25855 (`CView::selectedObjectToClipboardBuffer`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Вставка объектов / preview | source/render/view.cpp:26025 (`CView::clipboradObjectsToScene`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Скрыть/показать объекты | source/render/view.cpp:26412 (`CView::hideSelectedNodes`) | работает по коду; в 287 ещё не проверялась | одна логика CView/Undo; native preview/commit; системный буфер в тесте не трогать | 288 |
| Мышь / колесо / контекстное меню | source/render/rebuild64_viewport_widget.cpp:520 (`CRebuild64Viewport::editorInput`) | работает по коду; в 287 ещё не проверялась | пересылать оригинальное Qt-событие CView | 287 |
| Горячие клавиши / nativeVirtualKey | source/app/view_keybinding.cpp:193 (`CView::keyPressEvent`) | работает по коду; в 287 ещё не проверялась | единый key manager; явно маршрутизировать GL-only действия | 287 |
| Undo | source/app/main_window.cpp:730 (`MainWindow::on_actionUndo_triggered`) | работает по коду; в 287 ещё не проверялась | документ и штатная транзакция остаются в хосте | 287 |
| Redo | source/app/main_window.cpp:732 (`MainWindow::on_actionRedo_triggered`) | работает по коду; в 287 ещё не проверялась | документ и штатная транзакция остаются в хосте | 287 |
| История до индекса | source/app/main_window.cpp:2988 (`MainWindow::runUndoToIndex`) | работает по коду; в 287 ещё не проверялась | документ и штатная транзакция остаются в хосте | 287 |
| Save / Save All | source/app/main_window.cpp:9712 (`MainWindow::on_actionSave_triggered`) | работает по коду; в 287 ещё не проверялась | документ и штатная транзакция остаются в хосте | 287 |
| Save As | source/app/main_window.cpp:9682 (`MainWindow::on_actionSave_as_triggered`) | работает по коду; в 287 ещё не проверялась | документ и штатная транзакция остаются в хосте | 287 |
| Автосохранение | source/app/main_window.cpp:2192 (`MainWindow::performAutosave`) | работает по коду; в 287 ещё не проверялась | документ и штатная транзакция остаются в хосте | 287 |
| Камера WASD / колесо / вращение | source/render/camera.cpp:162 (`CCamera::update`) | работает по коду; в 287 ещё не проверялась | единая камера, native matrix; во время загрузки Qt остаётся отзывчивым | 287 |
| Дальняя камера | source/render/view.cpp:27141 (`CView::setTelephotoCameraEnabled`) | работает по коду; в 287 ещё не проверялась | передать текущую матрицу; GL-настройки картинки не копировать | 287 |
| Камера к выделению | source/render/view.cpp:22834 (`CView::moveCamToSelectedObjects`) | работает по коду; в 287 ещё не проверялась | передать текущую матрицу; GL-настройки картинки не копировать | 287 |
| Камера по миникарте | source/render/view.cpp:26545 (`CView::moveCameraToMinimapPoint`) | работает по коду; в 287 ещё не проверялась | передать текущую матрицу; GL-настройки картинки не копировать | 287 |
| Сброс камеры | source/render/view.cpp:27113 (`CView::resetCamPosition`) | работает по коду; в 287 ещё не проверялась | передать текущую матрицу; GL-настройки картинки не копировать | 287 |
| Лепка Land: Raise | source/ui/landscape_tools.h:71 (`Raise`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Land: Lower | source/ui/landscape_tools.h:72 (`Lower`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Land: Flatten | source/ui/landscape_tools.h:73 (`Flatten`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Land: Smooth | source/ui/landscape_tools.h:74 (`Smooth`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Land: Noise | source/ui/landscape_tools.h:75 (`Noise`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Land: BoundaryFill | source/ui/landscape_tools.h:76 (`BoundaryFill`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Overlay: Raise | source/ui/landscape_tools.h:71 (`Raise`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Overlay: Lower | source/ui/landscape_tools.h:72 (`Lower`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Overlay: Flatten | source/ui/landscape_tools.h:73 (`Flatten`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Overlay: Smooth | source/ui/landscape_tools.h:74 (`Smooth`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Overlay: Noise | source/ui/landscape_tools.h:75 (`Noise`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Лепка Overlay: BoundaryFill | source/ui/landscape_tools.h:76 (`BoundaryFill`) | блокируется с сообщением | общая CPU-модель и Undo; native sector delta/preview, пока явный GL-путь | 288 |
| Покраска Modern, цель Tile | source/ui/landscape_tools.h:51 (`Modern`) | блокируется с сообщением | сохранить131 и авторский атлас; обновление секторов без полного reload | 288 |
| Покраска Modern, цель Vertex | source/ui/landscape_tools.h:51 (`Modern`) | блокируется с сообщением | сохранить131 и авторский атлас; обновление секторов без полного reload | 288 |
| Покраска ClassicAuto, цель Tile | source/ui/landscape_tools.h:52 (`ClassicAuto`) | блокируется с сообщением | сохранить131 и авторский атлас; обновление секторов без полного reload | 288 |
| Покраска ClassicAuto, цель Vertex | source/ui/landscape_tools.h:52 (`ClassicAuto`) | блокируется с сообщением | сохранить131 и авторский атлас; обновление секторов без полного reload | 288 |
| Покраска Manual, цель Tile | source/ui/landscape_tools.h:53 (`Manual`) | блокируется с сообщением | сохранить131 и авторский атлас; обновление секторов без полного reload | 288 |
| Покраска Manual, цель Vertex | source/ui/landscape_tools.h:53 (`Manual`) | блокируется с сообщением | сохранить131 и авторский атлас; обновление секторов без полного reload | 288 |
| Покраска overlay | source/render/view.cpp:19905 (`CView::setTile(`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Пипетка / выбор плитки | source/render/view.cpp:19185 (`CView::pickTile`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Поворот плитки / быстрые слоты | source/render/view.cpp:20699 (`CView::addTileRotation`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Размер кисти / круг / квадрат | source/render/view.cpp:20708 (`CView::adjustTerrainPaintSize`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Astral / специальная кисть | source/render/view.cpp:20731 (`CView::astralBrushSelected`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Предпросмотр плитки | source/render/view.cpp:20600 (`CView::updatePreviewTile`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Круг/контур кисти | source/render/view.cpp:12034 (`CView::drawTerrainPaintOutline`) | блокируется с сообщением | общий инструмент; до этапа288 объяснить требование GL | 288 |
| Поверхность overlay: PaintExisting | source/ui/landscape_tools.h:64 (`PaintExisting`) | блокируется с сообщением | общая модель поверхности/материала; native refresh этап288 | 288 |
| Поверхность overlay: Activate | source/ui/landscape_tools.h:65 (`Activate`) | блокируется с сообщением | общая модель поверхности/материала; native refresh этап288 | 288 |
| Поверхность overlay: Deactivate | source/ui/landscape_tools.h:66 (`Deactivate`) | блокируется с сообщением | общая модель поверхности/материала; native refresh этап288 | 288 |
| Импорт heightmap | source/render/view.cpp:1858 (`CView::importTerrainHeightmap`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Вертикальный масштаб рельефа | source/render/view.cpp:23861 (`CView::applyTerrainVerticalScalePreview`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Настройки MPR / размеры / вода | source/render/view.cpp:1885 (`CView::openMapParameters`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Материалы MPR / liquid / анимация | source/render/view.cpp:1887 (`CView::openMprMaterials`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Атлас плиток | source/render/view.cpp:19168 (`CView::openAtlasTiles`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Просмотр точного HD-профиля | source/render/view.cpp:1881 (`CView::reviewTerrainAtlasProfile`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Поиск недостающих blends | source/render/view.cpp:21257 (`CView::resolveMissingTerrainBlends`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Диагностика покраски | source/render/view.cpp:1883 (`CView::exportTerrainPaintDiagnostic`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Метки дефектов покраски | source/render/view.cpp:22956 (`CView::appendMarkedTerrainPaintIssue`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Информация плитки | source/render/view.cpp:22870 (`CView::showTerrainTileInformation`) | не проверялась | GL-preview/публикацию явно маркировать или штатно переключать; данные сохранить | 288 |
| Конверсия Base→HD | source/render/view_base_hd.cpp:31 (`CView::convertBaseTerrainToHd`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Уплотнение атласа | source/render/view_atlas_compact.cpp:18 (`CView::compactTerrainAtlasRequested`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Очистка terrain pack | source/render/view_atlas_compact.cpp:94 (`CView::cleanTerrainPackRequested`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Изменение холста | source/render/view_canvas_resize.cpp:42 (`CView::applyCanvasResizeToCurrent`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Выбор карт совместной лепки | source/render/view_world_terrain.cpp:212 (`CView::chooseWorldTerrainMaps`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Лепка границ / соседей | source/render/view_world_terrain.cpp:349 (`CView::sculptWorldTerrainAt`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Покраска границ / соседей | source/render/view_world_terrain.cpp:1130 (`CView::paintWorldTerrainAt`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Undo/Redo рельефа мира | source/render/view_world_terrain.cpp:429 (`CView::worldTerrainHistory`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Save рельефа мира | source/render/view_world_terrain.cpp:467 (`CView::saveWorldTerrainMaps`) | не проверялась | сохранить131/world session/транзакции; refresh native, пока явный GL-путь | 288 |
| Главная поверхность видимость | source/render/view.cpp:1982 (`CView::setDrawLand`) | молча не работает в нативном кадре | настройка CView не реализована в camera.visuals=31; явный GL-путь | 291 |
| Overlay/вода видимость | source/render/view.cpp:1967 (`CView::setDrawWater`) | молча не работает в нативном кадре | настройка CView не реализована в camera.visuals=31; явный GL-путь | 291 |
| Landscape channel visibility | source/render/view.cpp:11920 (`CView::setLandscapeVisible`) | молча не работает в нативном кадре | настройка CView не реализована в camera.visuals=31; явный GL-путь | 291 |
| Сетка плиток | source/render/view.cpp:6525 (`CView::drawTestOverlaysOpenGl`) | работает по коду; в 287 ещё не проверялась | RGBA GL service поверх native; глубина приближённая, те же настройки | 291 |
| Номера материалов overlay | source/render/view.cpp:6472 (`CView::rebuildOverlayMaterialSlotLabels`) | работает по коду; в 287 ещё не проверялась | RGBA GL service поверх native; глубина приближённая, те же настройки | 291 |
| Входы / выходы / тестовая точка | source/render/view.cpp:24532 (`CView::setTestEntranceOverlays`) | работает по коду; в 287 ещё не проверялась | RGBA GL service поверх native; глубина приближённая, те же настройки | 291 |
| Сценарные метки / связи / спавны | source/render/view.cpp:17247 (`CView::setScenarioOverlay`) | работает по коду; в 287 ещё не проверялась | RGBA GL service поверх native; глубина приближённая, те же настройки | 291 |
| Выделение / логика / служебные модели | source/render/view_editor_service.cpp:18 (`CView::drawEditorObjectServices`) | работает по коду; в 287 ещё не проверялась | текущий RGBA+GL depth; native ID/depth согласовать | 291 |
| Гизмо / привязки / диагонали | source/app/operationmanager.cpp:274 (`CMoveAxis::`) | работает по коду; в 287 ещё не проверялась | общий инструмент и service; точность native depth отдельно | 291 |
| Магнит / ghost / модельные соединения | source/render/view_model_connections.cpp:120 (`CView::updateModelMagnet`) | не проверялась | GL query и service; явно предупредить до полной native проверки | 291 |
| Рычаг / точки подхода | source/ui/lever_approach_dialog.cpp:26 (`LeverApproachDialog::LeverApproachDialog`) | не проверялась | service линии; legacy model preview явно GL | 289 |
| Environment / небо / туман / свет | source/render/view.cpp:2676 (`m_environmentPreviewWidget =`) | не проверялась | данные времени/региона/ветра — native; GL-only visual policy явно GL, native вид не подгонять | 291 |
| Время суток | source/render/view.cpp:2841 (`m_timeOfDayWidget =`) | не проверялась | данные времени/региона/ветра — native; GL-only visual policy явно GL, native вид не подгонять | 291 |
| ИИ-граф панель / слой | source/render/view.cpp:2268 (`m_aiGraphPreviewWidget =`) | не проверялась | данные времени/региона/ветра — native; GL-only visual policy явно GL, native вид не подгонять | 291 |
| Каналы видимости / статические / динамические | source/render/view.cpp:3079 (`CView::syncVisualChannelButtons`) | не проверялась | данные времени/региона/ветра — native; GL-only visual policy явно GL, native вид не подгонять | 291 |
| Частицы / FX | source/render/view.cpp:7041 (`CView::drawParticles`) | не проверялась | данные времени/региона/ветра — native; GL-only visual policy явно GL, native вид не подгонять | 291 |
| Тени / диагностика | source/render/view.cpp:5869 (`CView::captureShadowDiagnostics`) | не проверялась | данные времени/региона/ветра — native; GL-only visual policy явно GL, native вид не подгонять | 291 |
| CAM панель / файл / track | source/app/main_window_camera.cpp:45 (`MainWindow::createCameraEditor`) | не проверялась | Qt panel + service track; камера shared | 291 |
| ИИ: загрузка/перестроение старого графа | source/render/view.cpp:4611 (`CView::beginAiGraphPreviewRegeneration`) | не проверялась | тот же CPU builder/документ; service overlay | 291 |
| Navmesh: bake / cancel / save / import | source/render/view_navmesh.cpp:24 (`CView::createNavmeshMenu`) | не проверялась | Qt panel/host bake без изменения vendor; service | 291 |
| Navmesh: слой / legend / comparison-click | source/render/view_navmesh.cpp:56 (`CView::drawNavmeshOverlay`) | не проверялась | service overlay; проверить отдельный click/legend | 291 |
| Проходимость / navigation support surface | source/render/view_unit_support.cpp:8 (`CView::ensureUnitSupportSurface`) | не проверялась | CPU collider общий; точность GL/native различий не скрывать | 289 |
| Travel / входы / reverse / paired route | source/render/view_travel.cpp:90 (`CView::travelMousePress`) | не проверялась | общие Qt события и CPU документы; service | 291 |
| Карта мира: placement / snapping / seams | source/render/view_world_placement.cpp:46 (`CView::updateWorldMapPlacement`) | не проверялась | общие события/сервис; GL neighbor geometry по требованию | 291 |
| Карта мира: расположение / persist / align | source/app/main_window_world.cpp:452 (`MainWindow::commitWorldLayoutChanges`) | не проверялась | общий layout; перезагрузить native только после подтверждения | 291 |
| Дипломатия / цвета NPC / anchor | source/render/view_diplomacy.cpp:33 (`CView::setDiplomacyMapMode`) | не проверялась | Qt/документ сохраняются; раскраска native отсутствует, явный GL-путь | 289 |
| Персонажи / модели / appearance | source/objects/unit.cpp:28 (`CUnit::`) | не проверялась | правила appearance/анимации передать native штатно; до этого явный GL-путь | 289 |
| stst8 / части / комплекция | source/objects/unit.cpp:1159 (`CUnit::update`) | не проверялась | правила appearance/анимации передать native штатно; до этого явный GL-путь | 289 |
| Анимация персонажей | source/objects/unit.cpp:100 (`CUnit::draw`) | не проверялась | правила appearance/анимации передать native штатно; до этого явный GL-путь | 289 |
| Unit texture preview | source/app/main_window.cpp:1500 (`MainWindow::showUnitTexturePreview`) | не проверялась | общие документы; CPreview честно GL или native offscreen на этапе290 | 289 |
| DB / выбор прототипа | source/app/main_window.cpp:689 (`MainWindow::showDatabaseEditor`) | не проверялась | общие документы; CPreview честно GL или native offscreen на этапе290 | 289 |
| Новый объект / предпросмотр | source/app/main_window.cpp:9772 (`MainWindow::on_actionCreate_new_object_triggered`) | не проверялась | общие документы; CPreview честно GL или native offscreen на этапе290 | 289 |
| Рандомизация параметров | source/app/main_window.cpp:10136 (`MainWindow::on_actionRandomize_parameter_triggered`) | не проверялась | общие документы; CPreview честно GL или native offscreen на этапе290 | 289 |
| DB размещение / указатель | source/render/view.cpp:24295 (`CView::beginDatabasePlacement`) | не проверялась | общий CView/Undo/service; units требуют GL до289 | 289 |
| Техническая/логическая расстановка | source/render/view.cpp:24339 (`CView::beginTechnicalPlacement`) | не проверялась | общий CView/Undo/service; units требуют GL до289 | 289 |
| Логические точки / patrol/look/trap | source/render/view.cpp:27222 (`CView::addLogicPoint`) | не проверялась | общий CView/Undo/service; units требуют GL до289 | 289 |
| Все5 logic slots / copy/paste | source/render/view.cpp:25722 (`CView::copyLogicSlot`) | не проверялась | общий CView/Undo/service; units требуют GL до289 | 289 |
| Сброс logic paths | source/render/view.cpp:25583 (`CView::resetUnitLogicPaths`) | не проверялась | общий CView/Undo/service; units требуют GL до289 | 289 |
| Сборки / группы / placement | source/render/view_assemblies.cpp:117 (`CView::beginPreparedAssemblyPlacement`) | не проверялась | общие данные + native placement; миниатюры CPU, без GL; native standalone preview отдельно | 290 |
| Галерея объектов | source/render/view_assemblies.cpp:187 (`CView::openObjectGallery`) | не проверялась | общие данные + native placement; миниатюры CPU, без GL; native standalone preview отдельно | 290 |
| Каталог сборок | source/render/view_assemblies.cpp:207 (`CView::openAssemblyCatalog`) | не проверялась | общие данные + native placement; миниатюры CPU, без GL; native standalone preview отдельно | 290 |
| Изолированный preview модели / вращение / zoom | source/render/preview.cpp:86 (`CPreview::paintGL`) | не проверялась | сейчас отдельный GL, честная маркировка; native offscreen позже | 290 |
| Миниатюры галереи / кеш | source/ui/model_gallery.cpp:260 (`CModelGallery::loadVisibleThumbnail`) | не проверялась | resourceThumbnails использует CPU rasterization/QImage, без OpenGL; сохраняется при Rebuild64 | 290 |
| Конструктор предметов/заклинаний preview | source/ui/item_spell_constructor_dialog.cpp:202 (`CPreview`) | не проверялась | Qt/DB отдельно; GL preview маркировать | 290 |
| Миникарта: встроенная текстура | source/app/main_window.cpp:5246 (`MainWindow::refreshViewportMinimapSources`) | не проверялась | Qt image без перерендера мира; сохранить | 287 |
| Миникарта: автогенерация на открытии | source/app/main_window.cpp:5189 (`MainWindow::scheduleViewportMinimapRefresh`) | не проверялась | не запускать GL bake до запроса; embedded/default Qt состояние | 287 |
| Миникарта: регенерация / export / Test Map | source/render/view.cpp:9245 (`CView::captureMinimapScene`) | не проверялась | GL-only capture обозначить/переключить; native ortho export позже | 290 |
| Topdown: composite / terrain / vegetation / water / masks / IDs | source/app/topdown_export.cpp:31 (`CTopdownExport::`) | не проверялась | GL offscreen сейчас; явный GL API; native ortho + IDs запросить Илью | 290 |
| Снимок viewport | source/render/view_topdown.cpp:15 (`CView::captureTopdownPass`) | не проверялась | GL FBO не должен выдаваться за Rebuild; native readback готового кадра | 290 |
| Экспорт текстур/моделей / ресурсный браузер | source/ui/resource_browser.cpp:428 (`CResourceBrowser::`) | не проверялась | CPU byte exports оставить; GL preview маркировать | 290 |
| Скрипт / сценарный редактор / навигация | source/ui/scenario_panel.cpp:37 (`CScenarioPanel::`) | не проверялась | Qt и документы прежние; overlay общий | 291 |
| Outliner / таблица параметров / коннекторы | source/layout_components/tablemanager.cpp:551 (`CTableManager::`) | не проверялась | общий CView/Undo; units и GL-only поля маршрутизировать | 289 |
| MCP list_instances | source/app/ai_agent_protocol.cpp:89 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP connect_instance | source/app/ai_agent_protocol.cpp:90 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP disconnect_instance | source/app/ai_agent_protocol.cpp:91 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP shutdown_headless | source/app/ai_agent_protocol.cpp:92 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP get_context | source/app/ai_agent_protocol.cpp:93 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP read_terrain | source/app/ai_agent_protocol.cpp:94 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP export_map_topdown | source/app/ai_agent_protocol.cpp:95 | не проверялась | 286 всегда GL FBO даже при native виде; в287 явный renderer/captureKind и сообщение/GL путь | 290 |
| MCP read_export_image | source/app/ai_agent_protocol.cpp:99 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP read_paint | source/app/ai_agent_protocol.cpp:102 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP material_palette | source/app/ai_agent_protocol.cpp:103 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP list_objects | source/app/ai_agent_protocol.cpp:106 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP search_database | source/app/ai_agent_protocol.cpp:108 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP list_profile_maps | source/app/ai_agent_protocol.cpp:109 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP read_profile_map | source/app/ai_agent_protocol.cpp:111 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP operation_status | source/app/ai_agent_protocol.cpp:112 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP cancel_operation | source/app/ai_agent_protocol.cpp:113 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP window_snapshot | source/app/ai_agent_protocol.cpp:114 | не проверялась | 286 всегда GL FBO даже при native виде; в287 явный renderer/captureKind и сообщение/GL путь | 290 |
| MCP get_revision | source/app/ai_agent_protocol.cpp:115 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP edit_objects | source/app/ai_agent_protocol.cpp:129 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP list_units | source/app/ai_agent_protocol.cpp:133 | не проверялась | CPU native MOB формат прежний; native appearance отдельно, сообщить ограничение вида | 289 |
| MCP get_unit | source/app/ai_agent_protocol.cpp:135 | не проверялась | CPU native MOB формат прежний; native appearance отдельно, сообщить ограничение вида | 289 |
| MCP edit_units | source/app/ai_agent_protocol.cpp:146 | не проверялась | CPU native MOB формат прежний; native appearance отдельно, сообщить ограничение вида | 289 |
| MCP rebuild_ai_graph | source/app/ai_agent_protocol.cpp:147 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP query_passability | source/app/ai_agent_protocol.cpp:148 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP validate_map | source/app/ai_agent_protocol.cpp:149 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP sculpt | source/app/ai_agent_protocol.cpp:162 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP paint | source/app/ai_agent_protocol.cpp:166 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP generate_terrain | source/app/ai_agent_protocol.cpp:177 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP erode | source/app/ai_agent_protocol.cpp:179 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP carve_river | source/app/ai_agent_protocol.cpp:182 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP auto_paint | source/app/ai_agent_protocol.cpp:196 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP preview | source/app/ai_agent_protocol.cpp:197 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP apply | source/app/ai_agent_protocol.cpp:201 | не проверялась | CPU prepare/preview/Undo прежние; refresh native/explicit GL viewport preview, не путать с кадром | 288 |
| MCP create_map | source/app/ai_agent_protocol.cpp:202 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP open_map | source/app/ai_agent_protocol.cpp:206 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP close_map | source/app/ai_agent_protocol.cpp:208 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP place_map_in_world | source/app/ai_agent_protocol.cpp:211 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP set_world_position | source/app/ai_agent_protocol.cpp:212 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP create_mob | source/app/ai_agent_protocol.cpp:213 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP undo_last_agent_operation | source/app/ai_agent_protocol.cpp:217 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |
| MCP save | source/app/ai_agent_protocol.cpp:218 | не проверялась | общий сервер/документ/authority; D3D не нужен в headless | 287 |

| Двойной щелчок по соседу в viewport | source/render/rebuild64_viewport_widget.cpp:496 (`eventFilter`, база286); source/render/view.cpp:23434 (`mouseDoubleClickEvent`, база286) | молча не работает в нативном кадре | переслать исходное Qt-событие; общая геометрия соседей по первому запросу, с сообщением и отменой при смене камеры | 287 |

Всего функциональных строк: **197**. работает по коду; в 287 ещё не проверялась: 42; не проверялась: 122; блокируется с сообщением: 28; молча не работает в нативном кадре: 5. MCP методов: 41.

## Очерёдность оставшихся этапов (план, не начаты)

1. **288 — быстрые object/terrain updates и весь terrain editing.** Сначала согласованный штатный ingress объектов/секторов и fence готовности, затем все кисти/оверлеи/лепка/вода/дороги/реки/world seams/Undo/MCP. CPU документы и131 остаются единственными; отделить delta от полного world reload. Это фундамент интерактивности следующих этапов.
2. **289 — units, appearance, stst8 и расстановка.** Согласовать весь native формат appearance/анимации/комплекции/5 logic slots, DB placement, навигационные поверхности, дипломатия и технические/логические объекты. Никакой подгонки native моделей под GL.
3. **290 — native offscreen outputs и независимые previews.** Сначала API offscreen/current accepted frame, затем window_snapshot, topdown/миникарты, галереи/миниатюры, предметы/заклинания/модельные просмотры; headless создаёт D3D только по явному native-запросу. Кеши и captureKind однозначны.
4. **291 — точные native query/depth и весь редакторский сервис.** ID/depth/cutouts/LOD/neighbor coordinates согласовать с Ильёй; перенести GL-only каналы/метки/picking, все child panels, navmesh/AI/scenario/CAM/travel/diplomacy/магнит/legend. Снять временные GL-маршруты только после сценарных проверок.
5. **292 — закрытие общей миграции.** Повторный обход полного перечня, все меню/клавиши/MCP/headless, cold/warm launch, recovery, два экземпляра и данные; OpenGL остаётся полностью рабочим запасным переключателем. Нельзя объявлять цель завершённой при открытых строках.

## Низкоуровневый перечень зависимостей286

Статус всех строк приложения ниже: **не проверялась отдельно**; функциональный статус и конкретный этап уточнены выше. Перенос: сохранить документную/CPU логику в хосте; прямой GL consumer временно остаётся явно именованной legacy capability; его native замена только на указанном функциональном этапе288–291. Этот список намеренно консервативен: наличие `makeCurrent` в документной операции не означает, что документ нужно переносить в DLL.

| Реализация | Код286 | Зависимость | Перенос / этап |
|---|---|---|---|
| `CAiEditAdapter::applyObjects` | source/app/ai_agent_objects.cpp:627 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CAiReadAdapter::terrain` | source/app/ai_agent_read.cpp:192 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CAiAgentServer::captureSnapshot` | source/app/ai_agent_server.cpp:433 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CAiAgentServer::finishCommit` | source/app/ai_agent_server.cpp:504 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::cancelBackgroundMobs` | source/app/background_workspace_load.cpp:38 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::queueBackgroundMobSource` | source/app/background_workspace_load.cpp:50 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::pumpBackgroundMobs` | source/app/background_workspace_load.cpp:70 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::showDatabaseEditor` | source/app/main_window.cpp:689 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::MainWindow` | source/app/main_window.cpp:793 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::~MainWindow` | source/app/main_window.cpp:2455 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateUnitTexturePreview` | source/app/main_window.cpp:2577 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateLogicSlotBarVisibility` | source/app/main_window.cpp:2620 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::renderUnitTexturePreview` | source/app/main_window.cpp:2713 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::testFunc1` | source/app/main_window.cpp:2839 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::createUndoView` | source/app/main_window.cpp:2899 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::runUndoToIndex` | source/app/main_window.cpp:2988 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::eventFilter` | source/app/main_window.cpp:3058 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::commitLoadedMapVisibilityDrag` | source/app/main_window.cpp:3308 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::mapTestObstacles` | source/app/main_window.cpp:3344 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateEntranceOverlays` | source/app/main_window.cpp:3458 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshMapTestContext` | source/app/main_window.cpp:3492 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::setCustomSpawnPicking` | source/app/main_window.cpp:3644 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::preserveCustomSpawnAfterDocumentReplacement` | source/app/main_window.cpp:3721 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::moveCameraToActiveSpawn` | source/app/main_window.cpp:3773 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::setupCompactViewportTools` | source/app/main_window.cpp:3895 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::openDiplomacy` | source/app/main_window.cpp:4118 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::createViewportToolbars` | source/app/main_window.cpp:4190 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateMapTestAction` | source/app/main_window.cpp:4511 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::mapTestStagingMob` | source/app/main_window.cpp:4651 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::launchCurrentMapTest` | source/app/main_window.cpp:4689 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshViewportMinimap` | source/app/main_window.cpp:5352 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::ensureGeneratedMinimap` | source/app/main_window.cpp:5495 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::positionViewportMinimap` | source/app/main_window.cpp:5521 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::persistAiGraph` | source/app/main_window.cpp:5532 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::generateMinimap` | source/app/main_window.cpp:5730 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::mobFamilyValidationRequest` | source/app/main_window.cpp:5986 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::nidAllocationContextDigest` | source/app/main_window.cpp:6049 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::allocateFamilyNid` | source/app/main_window.cpp:6093 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshMobFamilyValidation` | source/app/main_window.cpp:6143 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::applyMobFamilyValidationReport` | source/app/main_window.cpp:6209 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::selectValidationObject` | source/app/main_window.cpp:6704 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::reviewAndRepairNidCollisions` | source/app/main_window.cpp:6764 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::notifyFamilyPreflight` | source/app/main_window.cpp:6952 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::openCollection` | source/app/main_window.cpp:6999 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::newCollection` | source/app/main_window.cpp:7074 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::setWorkspaceIdentity` | source/app/main_window.cpp:7089 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::loadLastSession` | source/app/main_window.cpp:7103 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::saveCollection` | source/app/main_window.cpp:7336 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::openMapPairWithCameraPolicy` | source/app/main_window.cpp:7402 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateOperationAvailability` | source/app/main_window.cpp:7622 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::rebuildLoadedMapPanel` | source/app/main_window.cpp:7660 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateOutlinerSelectionPath` | source/app/main_window.cpp:7752 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateDocumentActionAvailability` | source/app/main_window.cpp:7861 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::beginWorkspaceLoad` | source/app/main_window.cpp:7891 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::endWorkspaceLoad` | source/app/main_window.cpp:7906 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::showLandscapeTools` | source/app/main_window.cpp:7931 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::hideLandscapeTools` | source/app/main_window.cpp:7949 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::activateObjectSelection` | source/app/main_window.cpp:7959 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::handleEscapeShortcut` | source/app/main_window.cpp:7985 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::isExitAllowed` | source/app/main_window.cpp:8120 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::closeAll` | source/app/main_window.cpp:8167 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionOpen_triggered` | source/app/main_window.cpp:8202 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::showGameAndModSetup` | source/app/main_window.cpp:8226 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::openDocument` | source/app/main_window.cpp:8366 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::replaceLoadedLandscape` | source/app/main_window.cpp:8396 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::loadMobWithDependencies` | source/app/main_window.cpp:8452 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::loadMobWithProgress` | source/app/main_window.cpp:8461 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::loadLandscapeWithProgress` | source/app/main_window.cpp:8492 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::beginAutosaveCountdown` | source/app/main_window.cpp:8537 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::performAutosave` | source/app/main_window.cpp:8616 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionNew_MOB_triggered` | source/app/main_window.cpp:9187 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionOpen_MOB_triggered` | source/app/main_window.cpp:9662 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionSave_as_triggered` | source/app/main_window.cpp:9682 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionSave_triggered` | source/app/main_window.cpp:9712 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_action_Mob_parameters_triggered` | source/app/main_window.cpp:9722 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionUndo_triggered` | source/app/main_window.cpp:9729 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionRedo_triggered` | source/app/main_window.cpp:9750 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionOpen_MOB_Script_triggered` | source/app/main_window.cpp:9759 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionDuplicate_selected_objects_triggered` | source/app/main_window.cpp:9765 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionCreate_new_object_triggered` | source/app/main_window.cpp:9772 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::updateWindowTitle` | source/app/main_window.cpp:9783 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionSave_all_MOB_s_triggered` | source/app/main_window.cpp:9867 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionReset_cam_position_triggered` | source/app/main_window.cpp:9886 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionChange_mod_e_triggered` | source/app/main_window.cpp:9892 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionCopy_IDs_to_clipboard_triggered` | source/app/main_window.cpp:9900 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::closeEvent` | source/app/main_window.cpp:9928 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionReset_logic_paths_triggered` | source/app/main_window.cpp:10130 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionReset_selected_IDs_triggered` | source/app/main_window.cpp:10143 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionSave_landscape_MPR_as_triggered` | source/app/main_window.cpp:10163 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionMap_parameters_triggered` | source/app/main_window.cpp:10169 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::on_actionSave_landscape_MPR_triggered` | source/app/main_window.cpp:10187 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::createCameraEditor` | source/app/main_window_camera.cpp:45 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshCameraEditor` | source/app/main_window_camera.cpp:211 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::createTravelEditor` | source/app/main_window_travel.cpp:75 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshTravelContext` | source/app/main_window_travel.cpp:297 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshTravelAreas` | source/app/main_window_travel.cpp:346 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::resolveTravelClose` | source/app/main_window_travel.cpp:539 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::saveMapWithTravel` | source/app/main_window_travel.cpp:560 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::attachCompanionMob` | source/app/main_window_world.cpp:87 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::refreshEditorWorld` | source/app/main_window_world.cpp:133 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::addMapToWorld` | source/app/main_window_world.cpp:208 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::commitWorldMapPlacement` | source/app/main_window_world.cpp:324 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::manageWorldMaps` | source/app/main_window_world.cpp:344 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `MainWindow::alignWorldMapTiles` | source/app/main_window_world.cpp:473 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelect::keyPress` | source/app/operationmanager.cpp:61 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelect::mousePressEvent` | source/app/operationmanager.cpp:168 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelect::mouseReleaseEvent` | source/app/operationmanager.cpp:197 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelect::mouseMoveEvent` | source/app/operationmanager.cpp:234 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::CMoveAxis` | source/app/operationmanager.cpp:274 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::canConfirmWithEnter` | source/app/operationmanager.cpp:292 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::keyPress` | source/app/operationmanager.cpp:298 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::axisOperate` | source/app/operationmanager.cpp:417 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::mouseReleaseEvent` | source/app/operationmanager.cpp:479 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::synchronizePointerAfterCameraNavigation` | source/app/operationmanager.cpp:506 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveAxis::mouseMoveEvent` | source/app/operationmanager.cpp:512 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRotateAxis::CRotateAxis` | source/app/operationmanager.cpp:564 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRotateAxis::keyPress` | source/app/operationmanager.cpp:576 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRotateAxis::mouseReleaseEvent` | source/app/operationmanager.cpp:694 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRotateAxis::mouseMoveEvent` | source/app/operationmanager.cpp:713 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRotateAxis::axisOperate` | source/app/operationmanager.cpp:737 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScaleAxis::CScaleAxis` | source/app/operationmanager.cpp:847 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScaleAxis::keyPress` | source/app/operationmanager.cpp:859 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScaleAxis::axisOperate` | source/app/operationmanager.cpp:962 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScaleAxis::mouseReleaseEvent` | source/app/operationmanager.cpp:1022 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScaleAxis::mouseMoveEvent` | source/app/operationmanager.cpp:1037 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `COperation::mousePressEvent` | source/app/operationmanager.cpp:1084 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `COperation::mouseMoveEvent` | source/app/operationmanager.cpp:1117 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::CTileBrush` | source/app/operationmanager.cpp:1187 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::keyPress` | source/app/operationmanager.cpp:1196 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::mousePressEvent` | source/app/operationmanager.cpp:1258 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::mouseReleaseEvent` | source/app/operationmanager.cpp:1289 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::synchronizePointerAfterCameraNavigation` | source/app/operationmanager.cpp:1325 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::mouseMoveEvent` | source/app/operationmanager.cpp:1333 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTileBrush::wheelEvent` | source/app/operationmanager.cpp:1445 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTopdownExport::releaseScene` | source/app/topdown_export.cpp:37 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTopdownExport::sceneIdentity` | source/app/topdown_export.cpp:50 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTopdownExport::initialize` | source/app/topdown_export.cpp:89 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTopdownExport::resolveObject` | source/app/topdown_export.cpp:164 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTopdownExport::step` | source/app/topdown_export.cpp:258 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::handleLandscapeShortcut` | source/app/view_keybinding.cpp:91 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::eventFilter` | source/app/view_keybinding.cpp:132 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::keyPressEvent` | source/app/view_keybinding.cpp:193 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::keyReleaseEvent` | source/app/view_keybinding.cpp:222 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CAlignModelCommand::apply` | source/core/undo.cpp:20 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CDeleteNodeCommand::undo` | source/core/undo.cpp:76 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CDeleteNodeCommand::redo` | source/core/undo.cpp:84 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateNodeCommand::undo` | source/core/undo.cpp:139 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateNodeCommand::redo` | source/core/undo.cpp:161 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateNodesCommand::undo` | source/core/undo.cpp:229 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateNodesCommand::redo` | source/core/undo.cpp:238 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CConvertWorldObjectToLeverCommand::apply` | source/core/undo.cpp:272 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectionCommand::undo` | source/core/undo.cpp:309 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectionCommand::redo` | source/core/undo.cpp:315 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeLogicParam::undo` | source/core/undo.cpp:403 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeLogicParam::redo` | source/core/undo.cpp:428 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeMobParametersCommand::apply` | source/core/undo.cpp:489 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeMobParametersCommand::redo` | source/core/undo.cpp:506 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeMobScriptCommand::apply` | source/core/undo.cpp:526 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreatePatrolCommand::undo` | source/core/undo.cpp:594 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreatePatrolCommand::redo` | source/core/undo.cpp:615 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateTrapPointCommand::undo` | source/core/undo.cpp:652 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateTrapPointCommand::redo` | source/core/undo.cpp:670 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CAppendBehaviorPointCommand::refresh` | source/core/undo.cpp:730 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CBehaviorPasteCommand::refresh` | source/core/undo.cpp:791 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveBehaviorPointCommand::apply` | source/core/undo.cpp:833 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMoveTrapPointCommand::apply` | source/core/undo.cpp:885 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CReplaceLogicSlotsCommand::apply` | source/core/undo.cpp:947 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CDeleteLogicPoint::undo` | source/core/undo.cpp:986 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CDeleteLogicPoint::redo` | source/core/undo.cpp:1020 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRoundMobCommand::undo` | source/core/undo.cpp:1061 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRoundMobCommand::redo` | source/core/undo.cpp:1066 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeActiveMobCommand::CChangeActiveMobCommand` | source/core/undo.cpp:1293 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeActiveMobCommand::undo` | source/core/undo.cpp:1301 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeActiveMobCommand::redo` | source/core/undo.cpp:1306 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CObjectEditBatchCommand::undo` | source/core/undo.cpp:1339 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CObjectEditBatchCommand::redo` | source/core/undo.cpp:1346 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeProp::undo` | source/core/undo.cpp:1358 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CChangeProp::redo` | source/core/undo.cpp:1390 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CResetIdCommand::invalidatesAiGraphPreview` | source/core/undo.cpp:1481 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CResetIdCommand::undo` | source/core/undo.cpp:1494 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CResetIdCommand::redo` | source/core/undo.cpp:1506 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CBrushTileCommand::undo` | source/core/undo.cpp:1532 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CBrushTileCommand::redo` | source/core/undo.cpp:1537 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CButtonConnector::clickButton` | source/layout_components/connectors_ui.cpp:86 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CComboStItem::CComboStItem` | source/layout_components/tablemanager.cpp:1962 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CComboStItem::eventFilter` | source/layout_components/tablemanager.cpp:1996 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CComboDynItem::CComboDynItem` | source/layout_components/tablemanager.cpp:2052 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CComboDynItem::eventFilter` | source/layout_components/tablemanager.cpp:2459 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::setShowAllMobs` | source/layout_components/tree_view.cpp:124 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::finishRefresh` | source/layout_components/tree_view.cpp:159 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::setRefreshSuspended` | source/layout_components/tree_view.cpp:185 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::syncSelectionToView` | source/layout_components/tree_view.cpp:237 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::selectGroup` | source/layout_components/tree_view.cpp:264 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::nodeMatches` | source/layout_components/tree_view.cpp:335 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::keyPressEvent` | source/layout_components/tree_view.cpp:489 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::contextMenuEvent` | source/layout_components/tree_view.cpp:526 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::itemClickedOver` | source/layout_components/tree_view.cpp:649 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::onNodeDelete` | source/layout_components/tree_view.cpp:708 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::addNodeToTree` | source/layout_components/tree_view.cpp:714 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::itemDoubleClickedOver` | source/layout_components/tree_view.cpp:785 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::markNode` | source/layout_components/tree_view.cpp:815 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::expandBehaviorHierarchy` | source/layout_components/tree_view.cpp:913 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::onChangeNodeId` | source/layout_components/tree_view.cpp:938 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTreeView::onChangeObjectName` | source/layout_components/tree_view.cpp:963 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMob::deserialize` | source/mob/mob.cpp:200 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMob::updateObjects` | source/mob/mob.cpp:506 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMob::freeMapId` | source/mob/mob.cpp:1506 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMobParameters::CMobParameters` | source/mob/mob_parameters.cpp:29 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMobParameters::copyDiplomacyFromLoadedMob` | source/mob/mob_parameters.cpp:396 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMobParameters::applyParameterChanges` | source/mob/mob_parameters.cpp:568 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMobParameters::on_isPrimaryBox_clicked` | source/mob/mob_parameters.cpp:738 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMobParameters::on_pushButtonOpenExtEditor_clicked` | source/mob/mob_parameters.cpp:842 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMagicTrap::drawLogicVisual` | source/objects/magictrap.cpp:105 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CActivationZone::draw` | source/objects/magictrap.cpp:840 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CObjectBase::retainVisualTextures` | source/objects/object_base.cpp:400 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CObjectBase::draw` | source/objects/object_base.cpp:409 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CObjectBase::drawSelect` | source/objects/object_base.cpp:497 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLogic::draw` | source/objects/unit.cpp:1462 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLogic::drawSelect` | source/objects/unit.cpp:1581 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLogic::drawHelp` | source/objects/unit.cpp:1741 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPatrolPoint::draw` | source/objects/unit.cpp:2402 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPatrolPoint::update` | source/objects/unit.cpp:2510 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::previewTerrainAtlasFullUpload` | source/render/landscape.cpp:178 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::previewTerrainAtlasIncremental` | source/render/landscape.cpp:203 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::previewTerrainAtlasRegridCopy` | source/render/landscape.cpp:298 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::previewTerrainAtlas` | source/render/landscape.cpp:511 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::previewTerrainAtlasIfChanged` | source/render/landscape.cpp:611 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::clearPreviewTerrainAtlas` | source/render/landscape.cpp:656 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::uploadTerrainShadowTexture` | source/render/landscape.cpp:1208 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CLandscape::atlasLifetimeRegressionCheck` | source/render/landscape.cpp:1693 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectFrame::CSelectFrame` | source/render/ogl_utils.cpp:7 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectFrame::~CSelectFrame` | source/render/ogl_utils.cpp:13 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectFrame::draw` | source/render/ogl_utils.cpp:19 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectFrame::updateFrame` | source/render/ogl_utils.cpp:56 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectFrame::reset` | source/render/ogl_utils.cpp:89 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::CPart` | source/render/part.cpp:190 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::CPart` | source/render/part.cpp:197 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::~CPart` | source/render/part.cpp:225 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::setCacheScope` | source/render/part.cpp:231 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::deferGpuCleanupToContextDestruction` | source/render/part.cpp:236 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::releaseRetainedVertexArrays` | source/render/part.cpp:241 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::sharedMeshCount` | source/render/part.cpp:250 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::sharedMeshBytes` | source/render/part.cpp:255 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::attachSharedMesh` | source/render/part.cpp:260 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::attachPrivateMesh` | source/render/part.cpp:278 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::ensureMesh` | source/render/part.cpp:284 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::updateLocalBounds` | source/render/part.cpp:294 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::update` | source/render/part.cpp:322 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::setMorphDeltas` | source/render/part.cpp:329 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::draw` | source/render/part.cpp:361 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::drawImmediate` | source/render/part.cpp:366 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::drawInstanced` | source/render/part.cpp:410 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::drawSelect` | source/render/part.cpp:488 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPart::drawSelectInstanced` | source/render/part.cpp:527 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::CPreview` | source/render/preview.cpp:13 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::attachNode` | source/render/preview.cpp:21 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::resetView` | source/render/preview.cpp:30 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::attachSettings` | source/render/preview.cpp:40 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::~CPreview` | source/render/preview.cpp:46 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::initializeGL` | source/render/preview.cpp:52 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::resizeGL` | source/render/preview.cpp:75 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::paintGL` | source/render/preview.cpp:86 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::wheelEvent` | source/render/preview.cpp:95 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::mousePressEvent` | source/render/preview.cpp:102 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::mouseMoveEvent` | source/render/preview.cpp:109 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::mouseReleaseEvent` | source/render/preview.cpp:135 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::draw` | source/render/preview.cpp:141 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreview::refreshCam` | source/render/preview.cpp:174 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::instance` | source/render/render_queue.cpp:23 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::begin` | source/render/render_queue.cpp:29 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::begin` | source/render/render_queue.cpp:34 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::beginSelection` | source/render/render_queue.cpp:84 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::end` | source/render/render_queue.cpp:90 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::submitSelection` | source/render/render_queue.cpp:99 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::releaseGpuResources` | source/render/render_queue.cpp:118 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::reserveInstanceBytes` | source/render/render_queue.cpp:128 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::submit` | source/render/render_queue.cpp:150 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::prepareStaticItem` | source/render/render_queue.cpp:191 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::submitPrepared` | source/render/render_queue.cpp:208 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::shadowCasterGroups` | source/render/render_queue.cpp:220 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::shadowCasterBounds` | source/render/render_queue.cpp:269 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::staticShadowFingerprint` | source/render/render_queue.cpp:287 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::flushShadow` | source/render/render_queue.cpp:320 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `RenderQueueContext::flush` | source/render/render_queue.cpp:409 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::residentBytes` | source/render/sector.cpp:10 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::accumulateMemory` | source/render/sector.cpp:13 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::~CSector` | source/render/sector.cpp:48 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::CSector` | source/render/sector.cpp:57 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::previewCanonicalPayload` | source/render/sector.cpp:199 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::previewCanonicalTerrainDelta` | source/render/sector.cpp:294 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::previewCanonicalOverlayPayload` | source/render/sector.cpp:384 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::previewCanonicalOverlayDelta` | source/render/sector.cpp:461 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::setPreviewMaxZ` | source/render/sector.cpp:569 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::serializeSector` | source/render/sector.cpp:580 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `SSpecificQuad::rotate` | source/render/sector.cpp:664 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::makeVertexData` | source/render/sector.cpp:699 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::generateVertexDataFromTile` | source/render/sector.cpp:847 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::generateWaterVertexData` | source/render/sector.cpp:866 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::updateBounds` | source/render/sector.cpp:974 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::updateLandTiles` | source/render/sector.cpp:1018 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::updateWaterTiles` | source/render/sector.cpp:1080 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::updateWaterDrawData` | source/render/sector.cpp:1252 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::updatePosition` | source/render/sector.cpp:1291 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::pickTile` | source/render/sector.cpp:1370 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::draw` | source/render/sector.cpp:1410 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::collectTerrainHeights` | source/render/sector.cpp:1483 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::drawWater` | source/render/sector.cpp:1501 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::intersectsFrustum` | source/render/sector.cpp:1581 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::drawTileGrid` | source/render/sector.cpp:1586 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::projectPt` | source/render/sector.cpp:1608 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::setTile` | source/render/sector.cpp:1638 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::setTile` | source/render/sector.cpp:1671 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::existsTileIndices` | source/render/sector.cpp:1688 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::updateDrawData` | source/render/sector.cpp:1703 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSector::setTerrainAtlasLayout` | source/render/sector.cpp:1710 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `STile::STile` | source/render/sector.cpp:1721 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `STile::packData` | source/render/sector.cpp:1728 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::CTile` | source/render/tile.cpp:31 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::CTile` | source/render/tile.cpp:37 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::~CTile` | source/render/tile.cpp:53 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::resetVertices` | source/render/tile.cpp:68 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::generateDrawVertexData` | source/render/tile.cpp:117 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::isProjectPoint` | source/render/tile.cpp:161 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::isProjectTile` | source/render/tile.cpp:186 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::tileIndex` | source/render/tile.cpp:208 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::setTile` | source/render/tile.cpp:214 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::setAtlasLayout` | source/render/tile.cpp:221 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::packData` | source/render/tile.cpp:229 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::reset` | source/render/tile.cpp:234 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTile::pos` | source/render/tile.cpp:245 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::CPreviewTile` | source/render/tile.cpp:255 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::~CPreviewTile` | source/render/tile.cpp:259 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::updateTile` | source/render/tile.cpp:268 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::hasStagedGeometry` | source/render/tile.cpp:300 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::hasPendingGeometry` | source/render/tile.cpp:305 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::hasCurrentCompatibleGpuContext` | source/render/tile.cpp:311 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::abandonGpuResources` | source/render/tile.cpp:319 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::uploadPendingGeometry` | source/render/tile.cpp:332 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::releaseGpuResources` | source/render/tile.cpp:398 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CPreviewTile::draw` | source/render/tile.cpp:434 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::CView` | source/render/view.cpp:1704 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::attachLandscapeTools` | source/render/view.cpp:1839 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setDrawWater` | source/render/view.cpp:1967 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setDrawLand` | source/render/view.cpp:1982 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::activateLandscapeTilePainting` | source/render/view.cpp:1997 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareLandscapeTilePainting` | source/render/view.cpp:2003 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setLandscapeEditingEnabled` | source/render/view.cpp:2018 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setLandscapeInteractionSuspended` | source/render/view.cpp:2031 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::closeLandscapeTools` | source/render/view.cpp:2055 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncLandscapeToolState` | source/render/view.cpp:2060 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearLandscapeBrushSelection` | source/render/view.cpp:2160 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureManualTileSelection` | source/render/view.cpp:2171 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateReadState` | source/render/view.cpp:2218 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::~CView` | source/render/view.cpp:2224 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::initEnvironmentPreviewWidget` | source/render/view.cpp:2639 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::persistViewportVisualState` | source/render/view.cpp:3053 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncVisualChannelButtons` | source/render/view.cpp:3079 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateVisualChannelAvailability` | source/render/view.cpp:3118 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setNeighborMapContext` | source/render/view.cpp:3182 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::neighborResourceFingerprint` | source/render/view.cpp:3272 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::neighborTerrainFingerprint` | source/render/view.cpp:3283 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::neighborObjectResourceFingerprint` | source/render/view.cpp:3308 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::trimNeighborRenderCache` | source/render/view.cpp:3319 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setNeighborCacheTransition` | source/render/view.cpp:3349 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::retainActiveLandscape` | source/render/view.cpp:3355 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::retainActiveNeighborObjects` | source/render/view.cpp:3386 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearNeighborLandscapes` | source/render/view.cpp:3429 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::populateNeighborObjects` | source/render/view.cpp:3461 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshNeighborRenderCache` | source/render/view.cpp:3530 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::neighborMapAtViewport` | source/render/view.cpp:3557 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::viewportPointOnHorizontalPlane` | source/render/view.cpp:3706 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateNeighborLayoutDrag` | source/render/view.cpp:3737 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cancelNeighborLayoutDrag` | source/render/view.cpp:3786 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateNeighborLayoutStatus` | source/render/view.cpp:3829 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::isObjectVisualVisible` | source/render/view.cpp:3870 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::positionEnvironmentPreviewWidget` | source/render/view.cpp:3880 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::initAiGraphPreviewWidget` | source/render/view.cpp:3907 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncAiGraphPreviewWidget` | source/render/view.cpp:4173 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshAiGraphPreviewSource` | source/render/view.cpp:4246 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureAiGraphPreviewBuildInput` | source/render/view.cpp:4308 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::buildGeneratedAiGraphCandidate` | source/render/view.cpp:4459 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::startAiGraphPreviewRegeneration` | source/render/view.cpp:4581 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshAiGraphOwnership` | source/render/view.cpp:4606 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginAiGraphPreviewRegeneration` | source/render/view.cpp:4611 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::invalidateAiGraphPreview` | source/render/view.cpp:4696 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cancelPendingAiGraphPreviewRegeneration` | source/render/view.cpp:4710 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyEnvironmentCompatibilityMode` | source/render/view.cpp:4718 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncEnvironmentPreviewWidget` | source/render/view.cpp:4755 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resizeEvent` | source/render/view.cpp:4775 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyRenderScheduling` | source/render/view.cpp:4782 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshAfterResourceSetup` | source/render/view.cpp:4796 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::hideEvent` | source/render/view.cpp:4831 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::showEvent` | source/render/view.cpp:4839 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateWindow` | source/render/view.cpp:4845 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rebuildSpatialIndices` | source/render/view.cpp:4851 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::spatiallyVisibleNodes` | source/render/view.cpp:4900 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::sceneSourceFingerprint` | source/render/view.cpp:4917 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::initShaders` | source/render/view.cpp:4941 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::initializeGL` | source/render/view.cpp:5069 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resizeGL` | source/render/view.cpp:5163 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawSkybox` | source/render/view.cpp:5181 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyEnvironmentUniforms` | source/render/view.cpp:5248 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::releaseShadowFramebuffers` | source/render/view.cpp:5292 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginEditorQueryFramebuffer` | source/render/view.cpp:5313 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::endEditorQueryFramebuffer` | source/render/view.cpp:5350 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawSelectionTerrainDepth` | source/render/view.cpp:5358 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureShadowFramebuffers` | source/render/view.cpp:5382 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::renderShadowMap` | source/render/view.cpp:5441 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureShadowDiagnostics` | source/render/view.cpp:5869 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyShadowUniforms` | source/render/view.cpp:6105 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::paintGL` | source/render/view.cpp:6176 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::stopForApplicationShutdown` | source/render/view.cpp:6330 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::shutdownWorkersFinished` | source/render/view.cpp:6344 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareForApplicationShutdown` | source/render/view.cpp:6350 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rebuildAiGraphOverlayGeometry` | source/render/view.cpp:6377 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rebuildOverlayMaterialSlotLabels` | source/render/view.cpp:6472 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawTestOverlaysOpenGl` | source/render/view.cpp:6525 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureParticleRendererInitialized` | source/render/view.cpp:6978 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawParticles` | source/render/view.cpp:7041 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::draw` | source/render/view.cpp:8486 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureMinimapScene` | source/render/view.cpp:9245 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::finishMinimapSceneCapture` | source/render/view.cpp:9415 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::loadLandscape` | source/render/view.cpp:9452 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::deferTerrainAutoCatalogBuild` | source/render/view.cpp:9905 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::queueDeferredTerrainAutoCatalogBuild` | source/render/view.cpp:9915 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureTerrainAutoCatalogReady` | source/render/view.cpp:9939 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::unloadLand` | source/render/view.cpp:10113 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveLand` | source/render/view.cpp:10195 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveLandAs` | source/render/view.cpp:10243 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveModernDraftAs` | source/render/view.cpp:10298 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::modernTerrainPublicationRequired` | source/render/view.cpp:10318 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::currentTerrainMprSaveRoute` | source/render/view.cpp:10324 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainMprSavePresentation` | source/render/view.cpp:10339 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::persistedModernDraftRequiresPublication` | source/render/view.cpp:10344 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveLandscapeDocument` | source/render/view.cpp:10355 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::buildValidatedLandscapeCandidate` | source/render/view.cpp:10362 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveChangedLandscapeDocument` | source/render/view.cpp:10615 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::adoptCommittedLandscapeBaseline` | source/render/view.cpp:11169 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::select` | source/render/view.cpp:11229 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pickObject` | source/render/view.cpp:11420 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::getColorFromRect` | source/render/view.cpp:11436 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::changeCurrentMob` | source/render/view.cpp:11466 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::changeCurrentMob` | source/render/view.cpp:11496 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onParamChangeLogic` | source/render/view.cpp:11509 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::armOpenGlDiagnostics` | source/render/view.cpp:11554 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::armSceneFrameTelemetry` | source/render/view.cpp:11564 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::markObservedSceneFrameFlicker` | source/render/view.cpp:11589 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::logOpenGlErrors` | source/render/view.cpp:11637 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::logOpenGlDiagnosticState` | source/render/view.cpp:11661 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::logShaderProgramDiagnostics` | source/render/view.cpp:11732 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::logOpenGlData` | source/render/view.cpp:11744 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ownerMob` | source/render/view.cpp:11764 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::isMobVisible` | source/render/view.cpp:11778 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::behaviorCollectionOwner` | source/render/view.cpp:11783 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::trapCollectionOwner` | source/render/view.cpp:11824 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setMobVisible` | source/render/view.cpp:11861 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setWorkspaceVisibility` | source/render/view.cpp:11880 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setLandscapeVisible` | source/render/view.cpp:11920 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::hasEditableSelection` | source/render/view.cpp:11934 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectNode` | source/render/view.cpp:11961 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectNodes` | source/render/view.cpp:11971 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawTilePreview` | source/render/view.cpp:12012 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateTileForm` | source/render/view.cpp:12023 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawTerrainPaintOutline` | source/render/view.cpp:12034 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rebuildTerrainAutoCatalog` | source/render/view.cpp:12279 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::startDeferredGlobalTerrainPaletteHydration` | source/render/view.cpp:12800 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyDeferredGlobalTerrainPaletteHydration` | source/render/view.cpp:13017 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::queueModernTerrainFamilyPrewarm` | source/render/view.cpp:13133 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prewarmModernTerrainFamily` | source/render/view.cpp:13155 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::adoptPrewarmedTerrainFamily` | source/render/view.cpp:13350 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareExactModernTerrainStamp` | source/render/view.cpp:13406 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshClassicAutoPalette` | source/render/view.cpp:13507 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshManualAtlasPalette` | source/render/view.cpp:13595 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshWorkingTerrainPalettes` | source/render/view.cpp:13686 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshTerrainFamilyUsage` | source/render/view.cpp:13704 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainAtlasUsage` | source/render/view.cpp:13768 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::reviewTerrainAtlasProfile` | source/render/view.cpp:13792 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::reviewTerrainAtlasProfileAtSlot` | source/render/view.cpp:13797 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureModernTerrainArtworkAssigned` | source/render/view.cpp:13876 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateTerrainAtlasViewerAnnotations` | source/render/view.cpp:13979 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::exportTerrainPaintDiagnostic` | source/render/view.cpp:14005 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::workingTerrainAtlasUsage` | source/render/view.cpp:14313 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::adoptCompleteTerrainBaseline` | source/render/view.cpp:14334 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::buildTerrainSemanticFieldMember` | source/render/view.cpp:14519 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::restoreTerrainSemanticFieldFromArchive` | source/render/view.cpp:14581 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::restoreTerrainSemanticFieldFromArchive` | source/render/view.cpp:14592 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainLayerUsesWorkingExtension` | source/render/view.cpp:14671 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::retainTerrainSourceCaches` | source/render/view.cpp:14691 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureGlobalTerrainMaterialSources` | source/render/view.cpp:14710 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::loadWorldTerrainMaterialSources` | source/render/view.cpp:14783 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::realizeWorkingTerrainSignature` | source/render/view.cpp:14810 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncWorkingTerrainAtlas` | source/render/view.cpp:15107 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::primeWorkingTerrainAtlasPreview` | source/render/view.cpp:15261 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::presentCompletedTerrainPublication` | source/render/view.cpp:15299 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::finalizeVirtualTerrainAtlas` | source/render/view.cpp:15308 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onChangeCursorTile` | source/render/view.cpp:16260 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onMapMaterialUpdate` | source/render/view.cpp:16305 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onRestoreTileData` | source/render/view.cpp:16313 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::checkNewLandVersion` | source/render/view.cpp:16318 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawSelectFrame` | source/render/view.cpp:16333 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pickObject` | source/render/view.cpp:16345 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::getTerrainPos` | source/render/view.cpp:16439 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::getTerrainBrushPos` | source/render/view.cpp:16452 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::getTerrainBrushPos` | source/render/view.cpp:16452 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::loadMob` | source/render/view.cpp:16561 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::adoptLoadedMob` | source/render/view.cpp:16604 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setWorkspaceLoadInProgress` | source/render/view.cpp:16660 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::scheduleUnitAppearancePreparation` | source/render/view.cpp:16685 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveMobAs` | source/render/view.cpp:16950 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveActiveMob` | source/render/view.cpp:16974 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveAllMob` | source/render/view.cpp:16984 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveMobDocument` | source/render/view.cpp:17013 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::unloadActiveMob` | source/render/view.cpp:17045 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::unloadMobDocument` | source/render/view.cpp:17050 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openActiveMobEditParams` | source/render/view.cpp:17158 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openMobDiplomacyTable` | source/render/view.cpp:17181 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openActiveMobScript` | source/render/view.cpp:17195 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::scenarioScriptText` | source/render/view.cpp:17202 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openScenarioSource` | source/render/view.cpp:17215 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setScenarioOverlay` | source/render/view.cpp:17247 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::scenarioMarkerPosition` | source/render/view.cpp:17257 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::focusScenarioPosition` | source/render/view.cpp:17266 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openExternalMobScript` | source/render/view.cpp:17272 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::commitMobParameterEdit` | source/render/view.cpp:17286 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::commitMobScriptEdit` | source/render/view.cpp:17297 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::notifyMobScriptChanged` | source/render/view.cpp:17306 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::malachiteHistoryStep` | source/render/view.cpp:17314 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::importExternalMobScript` | source/render/view.cpp:17320 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::closeMobScriptWindow` | source/render/view.cpp:17331 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::closeMobParameterWindow` | source/render/view.cpp:17337 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::closeAllMobParameterWindows` | source/render/view.cpp:17348 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::closeAllMobScriptWindows` | source/render/view.cpp:17359 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::unloadMob` | source/render/view.cpp:17370 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::nSelectedNodes` | source/render/view.cpp:17504 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectedNodes` | source/render/view.cpp:17526 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectionSnapshot` | source/render/view.cpp:17538 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::restoreSelection` | source/render/view.cpp:17559 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectObjectByProvenance` | source/render/view.cpp:17596 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearObjectSelection` | source/render/view.cpp:17635 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::recordSelectionChange` | source/render/view.cpp:17655 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setRandomComplection` | source/render/view.cpp:17672 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resetSelectedId` | source/render/view.cpp:17704 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openMapParameters` | source/render/view.cpp:17836 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::closeMprSettingsDock` | source/render/view.cpp:19142 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openMprMaterials` | source/render/view.cpp:19151 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshMprSettingsDock` | source/render/view.cpp:19162 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openAtlasTiles` | source/render/view.cpp:19168 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pickTile` | source/render/view.cpp:19185 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginTerrainPaintSampling` | source/render/view.cpp:19375 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::endTerrainPaintSampling` | source/render/view.cpp:19381 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resolveTerrainPaintFootprint` | source/render/view.cpp:19387 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::runTerrainStampRegression` | source/render/view.cpp:19429 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainStampRegressionReady` | source/render/view.cpp:19898 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTile` | source/render/view.cpp:19905 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTile` | source/render/view.cpp:20551 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updatePreviewTile` | source/render/view.cpp:20600 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::addTileRotation` | source/render/view.cpp:20699 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::adjustTerrainPaintSize` | source/render/view.cpp:20708 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::adjustTerrainPaintMaterial` | source/render/view.cpp:20717 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::manualTilePaintingEnabled` | source/render/view.cpp:20726 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::astralBrushSelected` | source/render/view.cpp:20731 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainPaintStampSpacing` | source/render/view.cpp:20743 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onLandscapeToolsShown` | source/render/view.cpp:20753 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainTileEditLayerIsLand` | source/render/view.cpp:20781 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainBrushProjectsToLand` | source/render/view.cpp:20788 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::showOutliner` | source/render/view.cpp:20798 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pickQuickAccessTile` | source/render/view.cpp:20803 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::endTileBrushGroup` | source/render/view.cpp:20812 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cancelTileBrushGroup` | source/render/view.cpp:21137 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectTerrainPaintPaletteMode` | source/render/view.cpp:21207 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::activeTerrainAtlasSlotCount` | source/render/view.cpp:21227 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainResolveUiActive` | source/render/view.cpp:21235 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::requestResolveMissingTerrainBlends` | source/render/view.cpp:21243 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resolveMissingTerrainBlends` | source/render/view.cpp:21257 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginTileBrushGroup` | source/render/view.cpp:21800 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainTileCommandInvalidatesAiGraph` | source/render/view.cpp:21870 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyTerrainTilePreview` | source/render/view.cpp:21889 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshTerrainTilePreview` | source/render/view.cpp:21903 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyOverlaySurfacePreview` | source/render/view.cpp:21952 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginTerrainPreviewBatch` | source/render/view.cpp:22006 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::endTerrainPreviewBatch` | source/render/view.cpp:22018 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onExit` | source/render/view.cpp:22075 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateParameter` | source/render/view.cpp:22131 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::viewParameters` | source/render/view.cpp:22161 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshDatabaseVisuals` | source/render/view.cpp:22251 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setLogicSlotIndex` | source/render/view.cpp:22294 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onParamChange` | source/render/view.cpp:22321 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyFamilyNidRepairs` | source/render/view.cpp:22429 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::suggestFamilyNid` | source/render/view.cpp:22497 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onPropertiesChange` | source/render/view.cpp:22505 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::collectObjectTreeData` | source/render/view.cpp:22534 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshSelectContextHelp` | source/render/view.cpp:22746 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshBehaviorOrder` | source/render/view.cpp:22816 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshLogicSlotOutliner` | source/render/view.cpp:22823 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onRestoreCursor` | source/render/view.cpp:22829 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::moveCamToSelectedObjects` | source/render/view.cpp:22834 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainCellAtViewport` | source/render/view.cpp:22855 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::showTerrainTileInformation` | source/render/view.cpp:22870 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::appendMarkedTerrainPaintIssue` | source/render/view.cpp:22956 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::mousePressEvent` | source/render/view.cpp:23213 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::mouseDoubleClickEvent` | source/render/view.cpp:23441 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::sculptTerrainAt` | source/render/view.cpp:23492 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::sculptTerrainTowardLatestPoint` | source/render/view.cpp:23648 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::makeTerrainMobHeightState` | source/render/view.cpp:23671 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureTerrainMobHeights` | source/render/view.cpp:23680 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainMobHeightStateAvailable` | source/render/view.cpp:23709 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyTerrainMobHeights` | source/render/view.cpp:23725 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyTerrainHeightPreview` | source/render/view.cpp:23773 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyTerrainVerticalScalePreview` | source/render/view.cpp:23861 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::importTerrainHeightmap` | source/render/view.cpp:23911 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateOverlaySculptPointer` | source/render/view.cpp:23979 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rawTerrainHeight` | source/render/view.cpp:24006 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateLandscapeSculptCoordinates` | source/render/view.cpp:24014 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::mouseMoveEvent` | source/render/view.cpp:24028 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::mouseReleaseEvent` | source/render/view.cpp:24191 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::operationAnchor` | source/render/view.cpp:24288 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginDatabasePlacement` | source/render/view.cpp:24295 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginTechnicalPlacement` | source/render/view.cpp:24339 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginPreparedObjectPlacement` | source/render/view.cpp:24356 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateDatabasePlacementPointer` | source/render/view.cpp:24393 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateLogicPlacementPointer` | source/render/view.cpp:24413 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTestSpawnMarker` | source/render/view.cpp:24427 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTestSpawnMarkerSelected` | source/render/view.cpp:24435 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::testSpawnMarkerAt` | source/render/view.cpp:24441 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearTestSpawnMarker` | source/render/view.cpp:24508 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setMapSkyType` | source/render/view.cpp:24515 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTestEntranceOverlays` | source/render/view.cpp:24532 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshUiScale` | source/render/view.cpp:24589 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::moveCameraToPoint` | source/render/view.cpp:24598 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::objectPlacementPositionAt` | source/render/view.cpp:24608 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainPositionAt` | source/render/view.cpp:24618 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::projectToRegeneratedNavigation` | source/render/view.cpp:24699 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::anchorCameraRotationToViewportCenter` | source/render/view.cpp:24709 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::wheelEvent` | source/render/view.cpp:24719 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::focusOutEvent` | source/render/view.cpp:24802 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pickObject` | source/render/view.cpp:24820 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::getTerrainPos` | source/render/view.cpp:24884 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::changeOperation` | source/render/view.cpp:24937 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cancelPendingOperation` | source/render/view.cpp:24975 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::hasPendingOperation` | source/render/view.cpp:25114 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareDocumentTransition` | source/render/view.cpp:25126 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareUndo` | source/render/view.cpp:25150 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareHistoryNavigation` | source/render/view.cpp:25155 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::operationSetBackup` | source/render/view.cpp:25172 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::operationRevert` | source/render/view.cpp:25267 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::operationApply` | source/render/view.cpp:25311 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::moveTo` | source/render/view.cpp:25518 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rotateTo` | source/render/view.cpp:25537 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::scaleTo` | source/render/view.cpp:25565 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resetUnitLogicPaths` | source/render/view.cpp:25583 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::deleteSelectedNodes` | source/render/view.cpp:25605 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::copyLogicSlot` | source/render/view.cpp:25722 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pasteLogicSlot` | source/render/view.cpp:25780 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pasteLogicSlotFromClipboard` | source/render/view.cpp:25840 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectedObjectToClipboardBuffer` | source/render/view.cpp:25855 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clipboradObjectsToScene` | source/render/view.cpp:26025 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::pasteBehaviorPoints` | source/render/view.cpp:26121 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectBehaviorParent` | source/render/view.cpp:26309 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectPreviousBehaviorPoint` | source/render/view.cpp:26343 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectNextBehaviorPoint` | source/render/view.cpp:26348 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::selectAdjacentBehaviorPoint` | source/render/view.cpp:26353 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::hideSelectedNodes` | source/render/view.cpp:26412 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::minimapCameraFootprint` | source/render/view.cpp:26426 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::moveCameraToMinimapPoint` | source/render/view.cpp:26545 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureCameraLocation` | source/render/view.cpp:26557 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::rebaseCameraForOpenedNeighbor` | source/render/view.cpp:26566 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::unHideAll` | source/render/view.cpp:26605 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setDirtyMpr` | source/render/view.cpp:26618 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::mprParametersDirty` | source/render/view.cpp:26628 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncMprDirtyState` | source/render/view.cpp:26636 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyMprParametersPreview` | source/render/view.cpp:26653 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyMprParametersPreviewTransaction` | source/render/view.cpp:26681 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::finishSculptStroke` | source/render/view.cpp:26707 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginObjectEditBatch` | source/render/view.cpp:26864 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::endObjectEditBatch` | source/render/view.cpp:26869 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setDurty` | source/render/view.cpp:26889 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::confirmScriptReferencedObjectIds` | source/render/view.cpp:26913 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::duplicateSelectedObjects` | source/render/view.cpp:26950 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::canConvertSelectedWorldObjectToLever` | source/render/view.cpp:26965 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::convertSelectedWorldObjectToLever` | source/render/view.cpp:26982 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshMobDirtyStates` | source/render/view.cpp:27102 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resetCamPosition` | source/render/view.cpp:27113 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cameraSpeedMultiplier` | source/render/view.cpp:27118 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setCameraSpeedMultiplier` | source/render/view.cpp:27123 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::telephotoCameraEnabled` | source/render/view.cpp:27136 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTelephotoCameraEnabled` | source/render/view.cpp:27141 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::telephotoCameraMultiplier` | source/render/view.cpp:27151 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTelephotoCameraMultiplier` | source/render/view.cpp:27157 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::telephotoCameraDirection` | source/render/view.cpp:27171 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTelephotoCameraDirection` | source/render/view.cpp:27177 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateCameraProjection` | source/render/view.cpp:27190 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::overridesWorldTime` | source/render/view.cpp:27199 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::effectiveWorldTimeHours` | source/render/view.cpp:27206 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::addLogicPoint` | source/render/view.cpp:27222 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::copySelectedIDsToClipboard` | source/render/view.cpp:27384 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::execMobSwitch` | source/render/view.cpp:27403 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearHistory` | source/render/view.cpp:27412 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::onMobParamEditFinished` | source/render/view.cpp:27417 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::roundActiveMob` | source/render/view.cpp:27422 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::undo_roundActiveMob` | source/render/view.cpp:27444 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::execUnloadCommand` | source/render/view.cpp:27467 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::iterateRoundMob` | source/render/view.cpp:27485 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyRoundMob` | source/render/view.cpp:27508 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveSession` | source/render/view.cpp:27523 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::loadSession` | source/render/view.cpp:27554 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setSessionWorkspaceIdentity` | source/render/view.cpp:27608 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::isRecentAvailable` | source/render/view.cpp:27617 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::renameActiveMobUnits` | source/render/view.cpp:27622 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setAssemblyWorldPosition` | source/render/view_assemblies.cpp:39 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::assemblySelection` | source/render/view_assemblies.cpp:43 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::normalizeAssemblySelection` | source/render/view_assemblies.cpp:51 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::groupSelectedAssembly` | source/render/view_assemblies.cpp:64 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::disassembleSelection` | source/render/view_assemblies.cpp:70 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureSelectedAssembly` | source/render/view_assemblies.cpp:75 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginAssemblyPlacement` | source/render/view_assemblies.cpp:110 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginPreparedAssemblyPlacement` | source/render/view_assemblies.cpp:117 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveSelectionAsAssembly` | source/render/view_assemblies.cpp:153 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginGalleryPlacement` | source/render/view_assemblies.cpp:164 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openObjectGallery` | source/render/view_assemblies.cpp:187 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::openAssemblyCatalog` | source/render/view_assemblies.cpp:207 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::compactTerrainAtlasRequested` | source/render/view_atlas_compact.cpp:18 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cleanTerrainPackRequested` | source/render/view_atlas_compact.cpp:94 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::convertBaseTerrainToHdRequested` | source/render/view_base_hd.cpp:31 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::convertBaseTerrainToHd` | source/render/view_base_hd.cpp:41 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureCamPose` | source/render/view_camera_track.cpp:7 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setCameraTrack` | source/render/view_camera_track.cpp:12 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawCameraTrack` | source/render/view_camera_track.cpp:38 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearCanvasNavigationStale` | source/render/view_canvas_resize.cpp:30 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyCanvasResizeToCurrent` | source/render/view_canvas_resize.cpp:42 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setDiplomacyMapMode` | source/render/view_diplomacy.cpp:33 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setDiplomacyAnchor` | source/render/view_diplomacy.cpp:87 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearDiplomacyAnchor` | source/render/view_diplomacy.cpp:92 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateDiplomacyOverlay` | source/render/view_diplomacy.cpp:94 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::diplomacyNpcColor` | source/render/view_diplomacy.cpp:136 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawDiplomacyObjectVisual` | source/render/view_diplomacy.cpp:149 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::focusDiplomacyNpc` | source/render/view_diplomacy.cpp:159 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::editDiplomacyRelation` | source/render/view_diplomacy.cpp:177 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::createDiplomacyTable` | source/render/view_diplomacy.cpp:195 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawEditorObjectServices` | source/render/view_editor_service.cpp:18 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawEditorPickIds` | source/render/view_editor_service.cpp:81 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawEditorServiceScene` | source/render/view_editor_service.cpp:136 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::acceptMapCompanionsCheckpoint` | source/render/view_map_save.cpp:11 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::stageMapCompanion` | source/render/view_map_save.cpp:20 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::commitMapCohortMpr` | source/render/view_map_save.cpp:27 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveMapCompanions` | source/render/view_map_save.cpp:37 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::stageMapCompanionFile` | source/render/view_map_save.cpp:54 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveMobCohort` | source/render/view_map_save.cpp:81 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::terrainAtlasInventoryDirectories` | source/render/view_map_save.cpp:95 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveMapFamily` | source/render/view_map_save.cpp:100 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::event` | source/render/view_model_connections.cpp:15 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::clearModelMagnet` | source/render/view_model_connections.cpp:26 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::prepareModelMagnet` | source/render/view_model_connections.cpp:36 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setModelMagnetEnabled` | source/render/view_model_connections.cpp:81 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setModelConnectionSource` | source/render/view_model_connections.cpp:89 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::restoreFreeModelMove` | source/render/view_model_connections.cpp:93 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::connectionScreenPoint` | source/render/view_model_connections.cpp:100 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::applyModelConnection` | source/render/view_model_connections.cpp:106 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateModelMagnet` | source/render/view_model_connections.cpp:120 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cycleModelConnectionTarget` | source/render/view_model_connections.cpp:155 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::createNavmeshMenu` | source/render/view_navmesh.cpp:24 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncNavmeshContext` | source/render/view_navmesh.cpp:35 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::navigationMapSaved` | source/render/view_navmesh.cpp:45 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawNavmeshOverlay` | source/render/view_navmesh.cpp:56 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::drawNavmeshLegend` | source/render/view_navmesh.cpp:82 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::navmeshComparisonClick` | source/render/view_navmesh.cpp:110 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::deferNeighborLoading` | source/render/view_neighbor_load.cpp:10 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::loadNeighborLandscapes` | source/render/view_neighbor_load.cpp:20 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::startNeighborLoadStage` | source/render/view_neighbor_load.cpp:40 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::acceptNeighborScene` | source/render/view_neighbor_load.cpp:120 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::finishNeighborLoadStage` | source/render/view_neighbor_load.cpp:168 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::randomizeSelectedObjects` | source/render/view_object_randomization.cpp:81 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::randomizeSelectedObjectRotation` | source/render/view_object_randomization.cpp:87 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::randomizeSelectedObjectsCombined` | source/render/view_object_randomization.cpp:93 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureTopdownPass` | source/render/view_topdown.cpp:15 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::captureEditorServiceLayer` | source/render/view_topdown.cpp:130 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::focusTravelArea` | source/render/view_travel.cpp:8 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTravelAreas` | source/render/view_travel.cpp:19 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setWorldTravelAreas` | source/render/view_travel.cpp:35 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::projectTravelOverlay` | source/render/view_travel.cpp:44 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::setTravelEditing` | source/render/view_travel.cpp:63 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginTravelArea` | source/render/view_travel.cpp:67 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cancelTravelGesture` | source/render/view_travel.cpp:74 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::travelMousePress` | source/render/view_travel.cpp:90 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::travelMouseMove` | source/render/view_travel.cpp:133 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::travelMouseRelease` | source/render/view_travel.cpp:159 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureUnitSupportSurface` | source/render/view_unit_support.cpp:8 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::unitPlacementPositionAt` | source/render/view_unit_support.cpp:23 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::movementPointerPosition` | source/render/view_unit_support.cpp:39 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::moveAlongUnitSurface` | source/render/view_unit_support.cpp:50 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginWorldMapPlacement` | source/render/view_world_placement.cpp:7 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::beginWorldMapPlacement` | source/render/view_world_placement.cpp:12 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::cancelWorldMapPlacement` | source/render/view_world_placement.cpp:30 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::showWorldMap` | source/render/view_world_placement.cpp:40 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateWorldMapPlacement` | source/render/view_world_placement.cpp:46 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::snapWorldMap` | source/render/view_world_placement.cpp:76 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::previewWorldMapOrigins` | source/render/view_world_placement.cpp:96 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::recordWorldTerrainStroke` | source/render/view_world_terrain.cpp:54 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::nextWorldTerrainStroke` | source/render/view_world_terrain.cpp:62 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::isWorldTerrainHistoryCommand` | source/render/view_world_terrain.cpp:69 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::validateHistoryCommand` | source/render/view_world_terrain.cpp:74 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::updateWorldTerrainStatus` | source/render/view_world_terrain.cpp:98 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::syncWorldTerrainActive` | source/render/view_world_terrain.cpp:115 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::restoreWorldTerrainActive` | source/render/view_world_terrain.cpp:127 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::refreshWorldTerrainNeighbors` | source/render/view_world_terrain.cpp:160 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::chooseWorldTerrainMaps` | source/render/view_world_terrain.cpp:212 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::worldTerrainPosition` | source/render/view_world_terrain.cpp:288 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::worldTerrainTile` | source/render/view_world_terrain.cpp:299 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::projectWorldTerrainPoint` | source/render/view_world_terrain.cpp:327 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::sculptWorldTerrainAt` | source/render/view_world_terrain.cpp:349 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::worldTerrainHistory` | source/render/view_world_terrain.cpp:429 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::replayWorldTerrainStroke` | source/render/view_world_terrain.cpp:441 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::saveWorldTerrainMaps` | source/render/view_world_terrain.cpp:467 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resolveWorldTerrainClose` | source/render/view_world_terrain.cpp:703 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::worldMaterialNeighbors` | source/render/view_world_terrain.cpp:768 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::eraseWorldOverlayAt` | source/render/view_world_terrain.cpp:809 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::ensureWorldTerrainAtlas` | source/render/view_world_terrain.cpp:845 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::resolveWorldMaterialBlends` | source/render/view_world_terrain.cpp:952 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::paintWorldTerrainAt` | source/render/view_world_terrain.cpp:1130 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CView::previewWorldPaintState` | source/render/view_world_terrain.cpp:1371 | CView или GL-примитив; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::parse` | source/resources/resourcemanager.cpp:925 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::uploadStaticTexture` | source/resources/resourcemanager.cpp:1180 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::loadTextureResource` | source/resources/resourcemanager.cpp:1284 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::texture` | source/resources/resourcemanager.cpp:1359 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::texture` | source/resources/resourcemanager.cpp:1389 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::layeredTexture` | source/resources/resourcemanager.cpp:1505 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::initResource` | source/resources/resourcemanager.cpp:1580 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::buildLandTex` | source/resources/resourcemanager.cpp:1987 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::setTerrainMipmappingEnabled` | source/resources/resourcemanager.cpp:2261 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::layeredTextureRefreshLifetimeRegressionCheck` | source/resources/resourcemanager.cpp:2297 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::retainSceneTexture` | source/resources/resourcemanager.cpp:2325 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CTextureList::collectRetiredTextures` | source/resources/resourcemanager.cpp:2350 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::CCreateObjectForm` | source/ui/createobjectform.cpp:53 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::onObjectChoose` | source/ui/createobjectform.cpp:282 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::on_buttonCreate_clicked` | source/ui/createobjectform.cpp:446 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::showEvent` | source/ui/createobjectform.cpp:567 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::refreshDatabase` | source/ui/createobjectform.cpp:624 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::openModelGallery` | source/ui/createobjectform.cpp:859 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CCreateObjectForm::selectDatabaseEntry` | source/ui/createobjectform.cpp:890 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `ItemSpellConstructorDialog::~ItemSpellConstructorDialog` | source/ui/item_spell_constructor_dialog.cpp:283 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `ItemSpellConstructorDialog::updateItemPreview` | source/ui/item_spell_constructor_dialog.cpp:534 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::~LeverApproachDialog` | source/ui/lever_approach_dialog.cpp:72 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::initialize` | source/ui/lever_approach_dialog.cpp:73 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::contextValid` | source/ui/lever_approach_dialog.cpp:142 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::preview` | source/ui/lever_approach_dialog.cpp:150 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::place` | source/ui/lever_approach_dialog.cpp:166 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::eventFilter` | source/ui/lever_approach_dialog.cpp:177 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `LeverApproachDialog::done` | source/ui/lever_approach_dialog.cpp:216 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMalachiteBridge::receive` | source/ui/malachite_bridge.cpp:75 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CMobScriptWindow::applyCurrentText` | source/ui/mob_script_window.cpp:137 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CRandomizeForm::on_push_Randomize_clicked` | source/ui/randomize_form.cpp:45 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::setLayerEnabled` | source/ui/scenario_panel.cpp:117 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::scheduleRefresh` | source/ui/scenario_panel.cpp:122 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::refresh` | source/ui/scenario_panel.cpp:132 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::selectedKey` | source/ui/scenario_panel.cpp:183 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::navigateCode` | source/ui/scenario_panel.cpp:270 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::navigateTarget` | source/ui/scenario_panel.cpp:281 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::showSourceLocation` | source/ui/scenario_panel.cpp:329 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CScenarioPanel::updateOverlay` | source/ui/scenario_panel.cpp:343 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectForm::selectAll` | source/ui/select_window.cpp:29 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectForm::on_push_Select_clicked` | source/ui/select_window.cpp:108 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |
| `CSelectForm::on_push_Add_to_select_clicked` | source/ui/select_window.cpp:115 | прямой GL/потребитель CView; не проверялась отдельно | хост/общий документ; capability по функциональной строке, 288–291 |

Низкоуровневых определений: 861. Полный машинный список с вызовами сохранён приватно в work/rebuild64-launch-287-20261107/definitions.json; игровые данные/приватный engine код не включены.


## Дополнение: свободные и inline функции, GL wrappers286

Повторная сверка всех tracked source .cpp/.h/.hpp расширяет первоначальную выборку методов. Это трассировка реализаций, а не дополнительные пользовательские кнопки или runtime PASS. Сигнатуры/тела проверены по8668; тестовые *_check исключены. Макросы gl_dispatch перечислены по каждому созданному entry point. Нет оснований переносить GL-формулы картинки в renderer: сохраняется GL запасной путь, native использует штатный мир; CPU данные остаются общими.

| Реализация | Код286 | Состояние286 | Как переносить / этап |
|---|---|---|---|
| `idFor` | source/app/ai_agent_edit.cpp:25 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `undo` | source/app/ai_agent_edit.cpp:413 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `redo` | source/app/ai_agent_edit.cpp:414 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `run` | source/app/ai_agent_lifecycle.cpp:220 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `run` | source/app/ai_agent_objects.cpp:595 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `mapKey` | source/app/background_workspace_load.cpp:33 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `runHeadlessCommand` | source/app/main.cpp:535 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `main` | source/app/main.cpp:4184 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CDatabaseHistoryCommand` | source/app/main_window.cpp:289 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `refresh` | source/app/main_window.cpp:298 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `view` | source/app/main_window.h:130 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `travelEditorIntegrationCheck` | source/app/main_window_travel.cpp:636 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CSelect::CSelect` | source/app/operationmanager.cpp:1179 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CState` | source/app/operationmanager.h:31 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `drawObjectVisual` | source/core/node.h:35 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `drawLogicVisual` | source/core/node.h:37 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CAlignModelCommand::CAlignModelCommand` | source/core/undo.cpp:16 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CDeleteNodeCommand::CDeleteNodeCommand` | source/core/undo.cpp:58 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCreateNodeCommand::CCreateNodeCommand` | source/core/undo.cpp:100 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCreateNodeCommand::CCreateNodeCommand` | source/core/undo.cpp:114 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCreateNodesCommand::CCreateNodesCommand` | source/core/undo.cpp:219 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CSelectionCommand::CSelectionCommand` | source/core/undo.cpp:244 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CConvertWorldObjectToLeverCommand::CConvertWorldObjectToLeverCommand` | source/core/undo.cpp:256 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CChangeLogicParam::CChangeLogicParam` | source/core/undo.cpp:330 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CChangeMobParametersCommand::CChangeMobParametersCommand` | source/core/undo.cpp:480 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CChangeMobScriptCommand::CChangeMobScriptCommand` | source/core/undo.cpp:518 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCreatePatrolCommand::CCreatePatrolCommand` | source/core/undo.cpp:548 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCreateTrapPointCommand::CCreateTrapPointCommand` | source/core/undo.cpp:641 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CDeleteLogicPoint::CDeleteLogicPoint` | source/core/undo.cpp:697 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CAppendBehaviorPointCommand::CAppendBehaviorPointCommand` | source/core/undo.cpp:706 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CBehaviorPasteCommand::CBehaviorPasteCommand` | source/core/undo.cpp:779 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMoveBehaviorPointCommand::CMoveBehaviorPointCommand` | source/core/undo.cpp:821 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMoveTrapPointCommand::CMoveTrapPointCommand` | source/core/undo.cpp:874 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CReplaceLogicSlotsCommand::CReplaceLogicSlotsCommand` | source/core/undo.cpp:918 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CRoundMobCommand::CRoundMobCommand` | source/core/undo.cpp:1055 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CChangeProp::CChangeProp` | source/core/undo.cpp:1313 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CChangeProp::CChangeProp` | source/core/undo.cpp:1318 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CResetIdCommand::CResetIdCommand` | source/core/undo.cpp:1473 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CBrushTileCommand::CBrushTileCommand` | source/core/undo.cpp:1520 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CObjectEditBatchCommand` | source/core/undo.h:26 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CButtonConnector::attach` | source/layout_components/connectors_ui.cpp:55 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `attachView` | source/layout_components/tree_view.h:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMob::attach` | source/mob/mob.cpp:69 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `view` | source/mob/mob.h:129 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMagicTrap::CMagicTrap` | source/objects/magictrap.cpp:11 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMagicTrap::CMagicTrap` | source/objects/magictrap.cpp:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMagicTrap::CMagicTrap` | source/objects/magictrap.cpp:53 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMagicTrap::draw` | source/objects/magictrap.cpp:94 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMagicTrap::drawObjectVisual` | source/objects/magictrap.cpp:100 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CActivationZone::CActivationZone` | source/objects/magictrap.cpp:662 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTrapCastPoint::draw` | source/objects/magictrap.cpp:1072 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CObjectBase::staticRenderItems` | source/objects/object_base.cpp:352 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CObjectBase::setTexture` | source/objects/object_base.cpp:393 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CUnit::draw` | source/objects/unit.cpp:100 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CUnit::drawObjectVisual` | source/objects/unit.cpp:106 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CUnit::drawLogicVisual` | source/objects/unit.cpp:122 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CUnit::drawSelect` | source/objects/unit.cpp:130 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CLogic::CLogic` | source/objects/unit.cpp:1412 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CPatrolPoint::CPatrolPoint` | source/objects/unit.cpp:2367 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CPatrolPoint::CPatrolPoint` | source/objects/unit.cpp:2374 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CPatrolPoint::draw` | source/objects/unit.cpp:2397 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CPatrolPoint::drawSelect` | source/objects/unit.cpp:2497 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `aiGraphObjectPropertyInvalidatesPreview` | source/render/ai_graph_preview_invalidation.cpp:7 | не проверялась отдельно | CPU raster сохранить; отдельный native offscreen/preview вход, GL FBO до переноса; 290 |
| `aiGraphUndoCommandInvalidatesPreview` | source/render/ai_graph_preview_invalidation.cpp:35 | не проверялась отдельно | CPU raster сохранить; отдельный native offscreen/preview вход, GL FBO до переноса; 290 |
| `CCamera::CCamera` | source/render/camera.cpp:10 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::CCamera` | source/render/camera.cpp:26 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::reset` | source/render/camera.cpp:42 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::setPreviewView` | source/render/camera.cpp:66 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::clearPreviewView` | source/render/camera.cpp:77 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::getLocation` | source/render/camera.cpp:86 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::setLocation` | source/render/camera.cpp:94 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::setOrbitPivotPreservingView` | source/render/camera.cpp:103 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::xRotate` | source/render/camera.cpp:115 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::zRotate` | source/render/camera.cpp:123 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::setSpeedMultiplier` | source/render/camera.cpp:131 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::setTelephotoMultiplier` | source/render/camera.cpp:136 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::setTelephotoDirection` | source/render/camera.cpp:141 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::movementInputActive` | source/render/camera.cpp:146 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::update` | source/render/camera.cpp:162 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::viewMatrix` | source/render/camera.cpp:195 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::logicalCullingPosition` | source/render/camera.cpp:206 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::matrixFor` | source/render/camera.cpp:221 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::syncMotionInterpolation` | source/render/camera.cpp:233 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeRight` | source/render/camera.cpp:243 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeLeft` | source/render/camera.cpp:254 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeForward` | source/render/camera.cpp:265 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::shiftForward` | source/render/camera.cpp:276 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeBack` | source/render/camera.cpp:287 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::shiftBack` | source/render/camera.cpp:298 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeUp` | source/render/camera.cpp:309 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeUpward` | source/render/camera.cpp:315 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeDown` | source/render/camera.cpp:325 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::strafeDownward` | source/render/camera.cpp:331 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::move` | source/render/camera.cpp:341 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::shift` | source/render/camera.cpp:357 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::translate` | source/render/camera.cpp:375 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::enlarge` | source/render/camera.cpp:385 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::move` | source/render/camera.cpp:412 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::moveTo` | source/render/camera.cpp:446 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::moveAwayOn` | source/render/camera.cpp:455 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCamera::attachSettings` | source/render/camera.cpp:468 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `~CCamera` | source/render/camera.h:20 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `step` | source/render/camera.h:40 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `speedMultiplier` | source/render/camera.h:41 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoEnabled` | source/render/camera.h:43 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setTelephotoEnabled` | source/render/camera.h:44 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoMultiplier` | source/render/camera.h:45 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoDirection` | source/render/camera.h:47 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `attachKeyManager` | source/render/camera.h:63 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cameraMotionActive` | source/render/camera_orbit.h:16 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `advanceCameraFixedStep` | source/render/camera_orbit.h:25 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cameraFrameMovementScale` | source/render/camera_orbit.h:39 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `reanchoredCameraPosition` | source/render/camera_orbit.h:48 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `boundedTelephotoDirection` | source/render/camera_telephoto.h:29 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoYawDegrees` | source/render/camera_telephoto.h:37 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `boundedTelephotoMultiplier` | source/render/camera_telephoto.h:53 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoViewDirection` | source/render/camera_telephoto.h:61 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoGroundForward` | source/render/camera_telephoto.h:71 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoGroundRight` | source/render/camera_telephoto.h:79 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cameraEyeFromMatrix` | source/render/camera_telephoto.h:86 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoCameraFrame` | source/render/camera_telephoto.h:110 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoProjectionMatrix` | source/render/camera_telephoto.h:131 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `activeCameraViewMatrix` | source/render/camera_telephoto.h:141 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `activeCameraProjectionMatrix` | source/render/camera_telephoto.h:154 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cameraRotationInputAllowed` | source/render/camera_telephoto.h:163 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `telephotoZoomedPosition` | source/render/camera_telephoto.h:168 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::SHeader::read` | source/render/figure.cpp:14 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::CFigure` | source/render/figure.cpp:21 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::~CFigure` | source/render/figure.cpp:27 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readFormCount` | source/render/figure.cpp:37 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readVectors` | source/render/figure.cpp:45 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readRadii` | source/render/figure.cpp:55 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readVertices` | source/render/figure.cpp:71 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readNormals` | source/render/figure.cpp:98 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readTextureCoords` | source/render/figure.cpp:118 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readIndices` | source/render/figure.cpp:128 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `readVertexComponents` | source/render/figure.cpp:138 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `convertUVCoords` | source/render/figure.cpp:155 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::generateTriangles` | source/render/figure.cpp:170 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::readData` | source/render/figure.cpp:197 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::readAssemblyOffset` | source/render/figure.cpp:304 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::applyAssemblyOffset` | source/render/figure.cpp:323 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::getPartNames` | source/render/figure.cpp:343 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `calcComplection` | source/render/figure.cpp:354 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::calculateConstitution` | source/render/figure.cpp:375 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::getVertexData` | source/render/figure.cpp:412 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::getAssemblyHierarchy` | source/render/figure.cpp:417 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::appendVertexData` | source/render/figure.cpp:429 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::CFigure::getMinimumBboxZ` | source/render/figure.cpp:469 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `completeCaptureGeometry` | source/render/figure.h:45 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `addChild` | source/render/figure.h:53 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `children` | source/render/figure.h:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setName` | source/render/figure.h:55 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `name` | source/render/figure.h:56 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setAssemblySourceOrder` | source/render/figure.h:57 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setOffset` | source/render/figure.h:58 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `offset` | source/render/figure.h:59 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `figureFormCount` | source/render/figure_format.h:10 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `decodeFigureVertexComponent` | source/render/figure_vertex_component.h:11 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `figureSourceVertexIndex` | source/render/figure_vertex_component.h:27 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CFramePresentationHistory::CFramePresentationHistory` | source/render/frame_presentation_telemetry.cpp:3 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CFramePresentationHistory::arm` | source/render/frame_presentation_telemetry.cpp:9 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CFramePresentationHistory::disarm` | source/render/frame_presentation_telemetry.cpp:16 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CFramePresentationHistory::append` | source/render/frame_presentation_telemetry.cpp:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CFramePresentationHistory::snapshot` | source/render/frame_presentation_telemetry.cpp:40 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `classifyFramePresentation` | source/render/frame_presentation_telemetry.cpp:56 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `framePresentationDiagnosisName` | source/render/frame_presentation_telemetry.cpp:112 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `formatFramePresentationTelemetry` | source/render/frame_presentation_telemetry.cpp:136 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shadowFramebufferPassIndex` | source/render/frame_presentation_telemetry.h:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shadowFramebufferPassBit` | source/render/frame_presentation_telemetry.h:28 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isValid` | source/render/frame_presentation_telemetry.h:103 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isArmed` | source/render/frame_presentation_telemetry.h:113 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `capacity` | source/render/frame_presentation_telemetry.h:114 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `size` | source/render/frame_presentation_telemetry.h:115 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `usesGles` | source/render/gl_dispatch.h:14 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `glesProc` | source/render/gl_dispatch.h:20 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `EI_GL_DISPATCH_VOID` | source/render/gl_dispatch.h:37 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `Enable` | source/render/gl_dispatch.h:68 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `Disable` | source/render/gl_dispatch.h:82 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `GetError` | source/render/gl_dispatch.h:96 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `GetString` | source/render/gl_dispatch.h:106 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `IsEnabled` | source/render/gl_dispatch.h:116 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `AlphaFunc` | source/render/gl_dispatch.h:128 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `Frustum` | source/render/gl_dispatch.h:132 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `Ortho` | source/render/gl_dispatch.h:137 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `LineStipple` | source/render/gl_dispatch.h:142 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `helperFrustum` | source/render/helper_visibility.h:7 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CHelperVisibilityScope` | source/render/helper_visibility.h:12 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `~CHelperVisibilityScope` | source/render/helper_visibility.h:14 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `add` | source/render/helper_visibility.h:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `add` | source/render/helper_visibility.h:33 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `visible` | source/render/helper_visibility.h:35 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `retainedSunFields` | source/render/landscape.cpp:44 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `sunFieldKey` | source/render/landscape.cpp:46 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::baselineSha256` | source/render/landscape.cpp:54 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::residentBytes` | source/render/landscape.cpp:57 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::accumulateMemory` | source/render/landscape.cpp:60 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::swapRenderState` | source/render/landscape.cpp:69 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::getInstance` | source/render/landscape.cpp:117 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::createReadOnlyBackground` | source/render/landscape.cpp:124 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::unloadMpr` | source/render/landscape.cpp:129 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::CLandscape` | source/render/landscape.cpp:165 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::releaseCleanEditorPreview` | source/render/landscape.cpp:644 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::~CLandscape` | source/render/landscape.cpp:689 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::readHeader` | source/render/landscape.cpp:694 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::serializeMpr` | source/render/landscape.cpp:732 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `genSectorSuffix` | source/render/landscape.cpp:774 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::readMap` | source/render/landscape.cpp:798 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::readBackgroundMap` | source/render/landscape.cpp:803 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::readPreparedBackground` | source/render/landscape.cpp:811 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::readMapInternal` | source/render/landscape.cpp:819 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::save` | source/render/landscape.cpp:935 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::saveMapAs` | source/render/landscape.cpp:942 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::setWorldOffset` | source/render/landscape.cpp:1034 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::adoptCanonicalSave` | source/render/landscape.cpp:1043 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::setMaterials` | source/render/landscape.cpp:1054 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::setAnimTils` | source/render/landscape.cpp:1060 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::refreshMaterialDrawDataIfNeeded` | source/render/landscape.cpp:1066 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::adoptCanonicalTerrainSave` | source/render/landscape.cpp:1078 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::draw` | source/render/landscape.cpp:1113 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `sampleTerrainHeight` | source/render/landscape.cpp:1144 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `traceTerrainSunVisibility` | source/render/landscape.cpp:1170 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::applyTerrainShadowUniforms` | source/render/landscape.cpp:1253 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::updateTerrainShadowVisibility` | source/render/landscape.cpp:1269 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::drawWater` | source/render/landscape.cpp:1414 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::projectPt` | source/render/landscape.cpp:1441 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::projectPt` | source/render/landscape.cpp:1458 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::unitSurfacePosition` | source/render/landscape.cpp:1470 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::logicDisplayPosition` | source/render/landscape.cpp:1480 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::projectPosition` | source/render/landscape.cpp:1489 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::pickTile` | source/render/landscape.cpp:1533 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::setTile` | source/render/landscape.cpp:1560 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::updateSectorDrawData` | source/render/landscape.cpp:1588 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::invalidateTerrainShadowField` | source/render/landscape.cpp:1593 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::drawTileGrid` | source/render/landscape.cpp:1600 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::previewCanonicalSector` | source/render/landscape.cpp:1607 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::previewCanonicalTerrainSectorDelta` | source/render/landscape.cpp:1615 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::previewCanonicalOverlaySector` | source/render/landscape.cpp:1626 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::previewCanonicalOverlaySectorDelta` | source/render/landscape.cpp:1635 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::previewCanonicalMaxZ` | source/render/landscape.cpp:1646 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::previewCanonicalTile` | source/render/landscape.cpp:1659 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CLandscape::projectPositions` | source/render/landscape.cpp:1683 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `isMprLoad` | source/render/landscape.h:82 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `isReadOnlyBackground` | source/render/landscape.h:83 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `setCanonicalTerrainModel` | source/render/landscape.h:108 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `filePath` | source/render/landscape.h:112 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `glTexture` | source/render/landscape.h:113 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `captureSourceAtlas` | source/render/landscape.h:114 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `captureUsesPreviewAtlas` | source/render/landscape.h:115 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `textureNum` | source/render/landscape.h:116 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `widthInWorldUnits` | source/render/landscape.h:117 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `heightInWorldUnits` | source/render/landscape.h:118 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `maximumHeight` | source/render/landscape.h:119 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `materials` | source/render/landscape.h:120 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `tileTypes` | source/render/landscape.h:122 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `setTileTypes` | source/render/landscape.h:123 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `animTiles` | source/render/landscape.h:124 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `setDirty` | source/render/landscape.h:147 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `isDirty` | source/render/landscape.h:148 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `originalMprData` | source/render/landscape.h:149 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `mapName` | source/render/landscape.h:157 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `shadowGeneration` | source/render/landscape.h:158 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `shadowPreparationPending` | source/render/landscape.h:159 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `append` | source/render/neighbor_render_cache.h:19 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `withinDistance` | source/render/neighbor_render_cache.h:31 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `neighborCell` | source/render/neighbor_render_cache.h:42 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `neighborDistantLod` | source/render/neighbor_render_cache.h:44 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `neighborFileRevision` | source/render/neighbor_scene_data.cpp:26 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `neighborFileSha256` | source/render/neighbor_scene_data.cpp:53 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `neighborContentFingerprint` | source/render/neighbor_scene_data.cpp:98 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `indexSections` | source/render/neighbor_scene_data.cpp:106 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `leaf` | source/render/neighbor_scene_data.cpp:115 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `text` | source/render/neighbor_scene_data.cpp:121 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `floats` | source/render/neighbor_scene_data.cpp:127 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `neighborStaticObjects` | source/render/neighbor_scene_data.cpp:143 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `prepareNeighborScene` | source/render/neighbor_scene_data.cpp:179 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `prepareNeighborVisuals` | source/render/neighbor_visual_preparation.cpp:9 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `buildNewTerrainAtlas` | source/render/new_terrain_canvas.cpp:19 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `commitNewTerrainCanvas` | source/render/new_terrain_canvas.cpp:122 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `tableSine` | source/render/overlay_material_animation.cpp:11 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `nextPreviewRandom` | source/render/overlay_material_animation.cpp:20 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `previewNoise` | source/render/overlay_material_animation.cpp:26 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `overlayMaterialAnimation` | source/render/overlay_material_animation.cpp:80 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `overlayTileAnimation` | source/render/overlay_material_animation.cpp:92 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainMaterialEmission` | source/render/overlay_material_animation.cpp:118 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainMaterialOpacity` | source/render/overlay_material_animation.cpp:128 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `sharedOverlayMaterialIndex` | source/render/overlay_material_animation.cpp:133 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainUnderlayEmission` | source/render/overlay_material_animation.cpp:182 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `overlayWindAnimation` | source/render/overlay_material_animation.cpp:189 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `overlayWindNoise` | source/render/overlay_material_animation.cpp:212 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `overlayVertexDisplacement` | source/render/overlay_material_animation.cpp:219 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `liveMeshCount` | source/render/part.cpp:17 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `liveMeshBytes` | source/render/part.cpp:18 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `~CSharedPartMesh` | source/render/part.cpp:32 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shutdownRetainedMeshes` | source/render/part.cpp:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `locationsFor` | source/render/part.cpp:75 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `supportsVertexArrays` | source/render/part.cpp:98 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `arraysFor` | source/render/part.cpp:108 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `bindVertexArray` | source/render/part.cpp:123 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `meshKey` | source/render/part.cpp:162 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `createMesh` | source/render/part.cpp:171 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `vertData` | source/render/part.h:28 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `vertData` | source/render/part.h:29 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `name` | source/render/part.h:30 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `name` | source/render/part.h:31 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setName` | source/render/part.h:32 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setVisible` | source/render/part.h:33 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setParentName` | source/render/part.h:43 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `parentName` | source/render/part.h:44 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setAssemblySourceOrder` | source/render/part.h:45 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `assemblySourceOrder` | source/render/part.h:46 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setRestOffset` | source/render/part.h:47 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `restOffset` | source/render/part.h:48 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setTextureNumber` | source/render/part.h:49 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `textureNumber` | source/render/part.h:50 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setTextureUvScale` | source/render/part.h:51 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `textureUvScale` | source/render/part.h:52 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setPoseMatrix` | source/render/part.h:53 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `poseMatrix` | source/render/part.h:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setSourceVertexIndices` | source/render/part.h:55 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sourceVertexIndices` | source/render/part.h:56 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isDrawable` | source/render/part.h:58 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `canInstance` | source/render/part.h:59 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `meshIdentity` | source/render/part.h:60 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `localBoundsCenter` | source/render/part.h:61 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `localBoundsRadius` | source/render/part.h:62 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setAuthoredNavigationBounds` | source/render/part.h:63 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `hasAuthoredNavigationBounds` | source/render/part.h:69 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `authoredNavigationMinimum` | source/render/part.h:70 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `authoredNavigationMaximum` | source/render/part.h:72 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `billboardDiameter` | source/render/particle_animation.h:29 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `parametersForScale` | source/render/particle_animation.h:44 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `familyForKind` | source/render/particle_animation.h:64 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `atlasUv` | source/render/particle_animation.h:76 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `Isaac32` | source/render/particle_animation.h:91 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:93 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `next` | source/render/particle_animation.h:111 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `unit` | source/render/particle_animation.h:121 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `mix` | source/render/particle_animation.h:127 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `initialize` | source/render/particle_animation.h:140 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `refill` | source/render/particle_animation.h:181 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:243 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:256 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:276 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:294 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `update` | source/render/particle_animation.h:297 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:333 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:360 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:432 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:445 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:461 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:464 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:469 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:481 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:516 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:565 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:579 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:595 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:598 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:610 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:618 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:691 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:706 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:724 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `generating` | source/render/particle_animation.h:725 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:728 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallPoint` | source/render/particle_animation.h:733 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBiasedSpherePoint` | source/render/particle_animation.h:747 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaForFrame` | source/render/particle_animation.h:762 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:771 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:791 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:806 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticle` | source/render/particle_animation.h:861 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:893 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:941 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:957 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:975 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `sourceCount` | source/render/particle_animation.h:976 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:979 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:984 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `initializeSources` | source/render/particle_animation.h:999 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:1019 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:1057 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticle` | source/render/particle_animation.h:1074 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1102 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1145 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1158 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1178 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1181 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:1186 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `createGrid` | source/render/particle_animation.h:1199 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1221 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1263 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1277 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1295 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1298 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:1303 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:1317 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:1337 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticle` | source/render/particle_animation.h:1380 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1400 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1446 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1457 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1473 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1476 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:1478 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:1490 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1508 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1553 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1565 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1585 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1588 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:1590 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomDirection` | source/render/particle_animation.h:1602 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:1614 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1623 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1671 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1684 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1700 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `sourceCount` | source/render/particle_animation.h:1701 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1704 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:1709 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:1723 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:1738 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1754 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1808 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1834 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1858 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1861 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:1866 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:1884 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:1952 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:1967 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:1985 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:1988 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomDisk` | source/render/particle_animation.h:1993 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2000 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:2025 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2078 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2160 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2175 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2191 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:2194 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2199 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:2213 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2234 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2316 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2329 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2345 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:2348 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2353 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `sourcePosition` | source/render/particle_animation.h:2379 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnTail` | source/render/particle_animation.h:2385 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnClosingRing` | source/render/particle_animation.h:2400 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2420 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2482 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2495 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2511 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:2514 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2519 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:2534 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2553 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2605 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2620 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2636 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2639 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnTrail` | source/render/particle_animation.h:2656 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2666 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2720 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2735 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2753 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `sizeAt` | source/render/particle_animation.h:2756 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2766 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:2778 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2787 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2830 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2845 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2861 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:2864 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:2869 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:2881 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:2896 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:2967 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:2982 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:2998 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:3001 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:3006 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:3018 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:3046 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `SphereShellEmitter` | source/render/particle_animation.h:3119 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:3121 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:3136 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:3152 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `sizeAt` | source/render/particle_animation.h:3155 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:3173 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallDirection` | source/render/particle_animation.h:3178 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:3192 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateScale` | source/render/particle_animation.h:3207 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:3216 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:3235 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `SphereAcidEmitter` | source/render/particle_animation.h:3285 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `SphereElectricityEmitter` | source/render/particle_animation.h:3291 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `SphereFireEmitter` | source/render/particle_animation.h:3297 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `AntimagicEmitter` | source/render/particle_animation.h:3303 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:3333 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:3351 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:3367 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `birthAlpha` | source/render/particle_animation.h:3370 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `anchorAlpha` | source/render/particle_animation.h:3378 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:3390 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomSourceDirection` | source/render/particle_animation.h:3395 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:3411 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:3452 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:3477 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:3509 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnPass` | source/render/particle_animation.h:3542 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:3550 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:3603 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:3619 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:3635 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `radiusAt` | source/render/particle_animation.h:3638 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:3649 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:3658 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:3663 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:3686 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:3708 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:3736 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:3805 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:3821 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:3839 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:3842 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:3847 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:3861 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:3893 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:3946 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:3959 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:3977 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:3980 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:3989 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:3994 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:4006 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:4019 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:4058 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:4071 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:4088 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `brightnessAt` | source/render/particle_animation.h:4091 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:4100 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:4110 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:4153 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:4168 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:4190 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:4193 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:4202 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnBatch` | source/render/particle_animation.h:4213 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:4247 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:4302 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:4316 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:4335 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `colorAt` | source/render/particle_animation.h:4338 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:4351 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:4360 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallPoint` | source/render/particle_animation.h:4365 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `sizeAt` | source/render/particle_animation.h:4379 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:4384 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:4401 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:4425 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:4449 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnPass` | source/render/particle_animation.h:4476 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:4482 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:4515 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:4530 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:4543 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:4548 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:4590 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:4604 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:4622 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:4625 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:4630 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:4645 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:4679 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnPass` | source/render/particle_animation.h:4730 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:4736 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:4785 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:4801 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:4819 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:4822 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:4833 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `initializeSources` | source/render/particle_animation.h:4850 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:4871 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:4900 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:4936 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnPass` | source/render/particle_animation.h:4979 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:4985 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5027 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5042 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5054 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5057 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5069 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5120 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5135 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5153 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5156 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomSphereDirection` | source/render/particle_animation.h:5161 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5177 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnInitialPopulation` | source/render/particle_animation.h:5189 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:5217 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5249 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5277 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5293 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5311 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5314 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5319 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:5332 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:5346 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnPass` | source/render/particle_animation.h:5368 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5375 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5401 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5415 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5433 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5436 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5441 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5453 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5501 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5515 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5533 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5536 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallPoint` | source/render/particle_animation.h:5541 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5555 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:5567 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:5588 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5619 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5649 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5663 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5681 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5684 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:5689 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5698 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:5711 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:5723 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5749 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5785 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5799 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5820 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5823 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomDiskPoint` | source/render/particle_animation.h:5828 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:5840 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5848 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:5862 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:5876 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:5898 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:5930 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:5944 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:5962 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:5965 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallPoint` | source/render/particle_animation.h:5970 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:5984 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:5996 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:6005 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:6045 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:6059 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:6077 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:6080 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallPoint` | source/render/particle_animation.h:6085 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:6099 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:6108 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:6120 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:6131 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:6178 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:6193 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:6211 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:6214 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomUnitDirection` | source/render/particle_animation.h:6219 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaAt` | source/render/particle_animation.h:6234 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:6243 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:6260 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:6280 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:6309 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:6366 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:6380 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:6396 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:6399 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomBallPoint` | source/render/particle_animation.h:6404 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:6416 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `initializeSources` | source/render/particle_animation.h:6434 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:6453 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `gatherAlpha` | source/render/particle_animation.h:6475 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:6488 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:6515 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:6528 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:6579 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:6593 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:6609 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:6612 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:6617 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `createSource` | source/render/particle_animation.h:6632 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawnTail` | source/render/particle_animation.h:6654 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateParticles` | source/render/particle_animation.h:6673 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `updateSources` | source/render/particle_animation.h:6703 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:6718 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceTo` | source/render/particle_animation.h:6757 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `samples` | source/render/particle_animation.h:6770 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `activeCount` | source/render/particle_animation.h:6786 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `randomRange` | source/render/particle_animation.h:6789 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `reset` | source/render/particle_animation.h:6794 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `spawn` | source/render/particle_animation.h:6806 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `alphaForLife` | source/render/particle_animation.h:6820 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `advanceOne` | source/render/particle_animation.h:6828 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `simplifyStaticMesh` | source/render/prepared_static_visual.cpp:7 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `valid` | source/render/prepared_static_visual.h:29 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `attachView` | source/render/preview.h:25 | не проверялась отдельно | CPU raster сохранить; отдельный native offscreen/preview вход, GL FBO до переноса; 290 |
| `CRebuild64StartupGuard` | source/render/rebuild64_startup_guard.h:20 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `~CRebuild64StartupGuard` | source/render/rebuild64_startup_guard.h:24 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `arm` | source/render/rebuild64_startup_guard.h:25 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `clear` | source/render/rebuild64_startup_guard.h:36 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `interrupted` | source/render/rebuild64_startup_guard.h:37 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `birth` | source/render/rebuild64_startup_guard.h:53 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64NativeSurface` | source/render/rebuild64_viewport_widget.cpp:48 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `paintEngine` | source/render/rebuild64_viewport_widget.cpp:55 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `~Worker` | source/render/rebuild64_viewport_widget.cpp:82 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `pause` | source/render/rebuild64_viewport_widget.cpp:87 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `failure` | source/render/rebuild64_viewport_widget.cpp:90 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `canPresent` | source/render/rebuild64_viewport_widget.cpp:93 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `run` | source/render/rebuild64_viewport_widget.cpp:103 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::CRebuild64Viewport` | source/render/rebuild64_viewport_widget.cpp:259 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::~CRebuild64Viewport` | source/render/rebuild64_viewport_widget.cpp:301 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::movePanels` | source/render/rebuild64_viewport_widget.cpp:302 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::backendLoaded` | source/render/rebuild64_viewport_widget.cpp:322 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::selectRebuild` | source/render/rebuild64_viewport_widget.cpp:323 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::fallback` | source/render/rebuild64_viewport_widget.cpp:346 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::cameraSnapshot` | source/render/rebuild64_viewport_widget.cpp:354 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `editorMobOrder` | source/render/rebuild64_viewport_widget.cpp:394 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `editorPoses` | source/render/rebuild64_viewport_widget.cpp:397 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::tick` | source/render/rebuild64_viewport_widget.cpp:411 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::capture` | source/render/rebuild64_viewport_widget.cpp:504 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::eventFilter` | source/render/rebuild64_viewport_widget.cpp:507 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::editorInput` | source/render/rebuild64_viewport_widget.cpp:520 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::serviceInput` | source/render/rebuild64_viewport_widget.cpp:565 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `CRebuild64Viewport::syncEditorViewport` | source/render/rebuild64_viewport_widget.cpp:590 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `rebuildSelected` | source/render/rebuild64_viewport_widget.h:24 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `lastFailure` | source/render/rebuild64_viewport_widget.h:27 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `statistics` | source/render/rebuild64_viewport_widget.h:28 | не проверялась отдельно | единый host/document путь и capability; перенос вычислений/записи в DLL не нужен; 287 / функциональный этап |
| `worldFloat` | source/render/render_environment.cpp:12 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `lerp` | source/render/render_environment.cpp:21 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `rgb` | source/render/render_environment.cpp:28 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `gipatSunlight` | source/render/render_environment.cpp:33 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `gipatAmbient` | source/render/render_environment.cpp:46 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ingosAmbient` | source/render/render_environment.cpp:59 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `gipatSky` | source/render/render_environment.cpp:72 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ingosSky` | source/render/render_environment.cpp:85 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ingosSunlight` | source/render/render_environment.cpp:98 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `hourlyColor` | source/render/render_environment.cpp:110 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `clockValueToHours` | source/render/render_environment.cpp:118 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderEnvironmentForValues` | source/render/render_environment.cpp:126 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderEnvironment` | source/render/render_environment.cpp:192 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shadowProjectionDirection` | source/render/render_environment.cpp:206 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `classifyTextureAlpha` | source/render/render_material.cpp:3 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `estimatedTextureStorage` | source/render/render_memory.h:9 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `texture` | source/render/render_memory.h:29 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `image` | source/render/render_memory.h:35 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `total` | source/render/render_memory.h:39 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `addCounters` | source/render/render_performance.cpp:19 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `percentileMilliseconds` | source/render/render_performance.cpp:69 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `RenderPerformanceContext::instance` | source/render/render_performance.cpp:80 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `RenderPerformanceContext::beginFrame` | source/render/render_performance.cpp:86 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `RenderPerformanceContext::recordGpuTime` | source/render/render_performance.cpp:102 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `RenderPerformanceContext::endFrame` | source/render/render_performance.cpp:111 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `RenderPerformanceContext::resetWindow` | source/render/render_performance.cpp:306 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setFrameMetadata` | source/render/render_performance.h:86 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `collecting` | source/render/render_performance.h:88 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `counters` | source/render/render_performance.h:89 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `currentFrameCounters` | source/render/render_performance.h:90 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `supportsFixedFunctionAlphaTest` | source/render/render_queue.cpp:16 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderTextureReady` | source/render/render_queue.h:50 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sortRenderItems` | source/render/render_queue.h:70 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shareInstanceBatch` | source/render/render_queue.h:127 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `instanceBatchLength` | source/render/render_queue.h:144 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `supportsInstancedRenderPath` | source/render/render_queue.h:154 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shadowItemWithinDistance` | source/render/render_queue.h:160 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shadowItemIntersectsLightSpace` | source/render/render_queue.h:166 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `collecting` | source/render/render_queue.h:206 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `collectingSelection` | source/render/render_queue.h:207 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setShadowOnlyCollection` | source/render/render_queue.h:208 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setStableResourceOrder` | source/render/render_queue.h:209 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setDiplomacyTint` | source/render/render_queue.h:210 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setLocalShadowOwners` | source/render/render_queue.h:211 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderTimerIntervalMs` | source/render/render_scheduling.h:12 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderSwapInterval` | source/render/render_scheduling.h:19 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderVsyncFromOptions` | source/render/render_scheduling.h:24 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CRenderStallTrace` | source/render/render_stall_trace.h:13 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `mark` | source/render/render_stall_trace.h:15 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `~CRenderStallTrace` | source/render/render_stall_trace.h:20 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `threadCpuNsecs` | source/render/render_stall_trace.h:39 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CScene::CScene` | source/render/scene.cpp:12 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CScene::changeMode` | source/render/scene.cpp:19 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `~CScene` | source/render/scene.h:18 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `getMode` | source/render/scene.h:20 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `switchTileEditMode` | source/render/scene.h:22 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setLandTileEditMode` | source/render/scene.h:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isLandTileEditMode` | source/render/scene.h:24 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `addVertex` | source/render/sector.h:18 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `STile` | source/render/sector.h:26 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setIndex` | source/render/sector.h:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `rebindMaterials` | source/render/sector.h:55 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setWorldOffset` | source/render/sector.h:59 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `index` | source/render/sector.h:61 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `arrTile` | source/render/sector.h:67 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `arrTileEdit` | source/render/sector.h:68 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `arrWater` | source/render/sector.h:69 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `arrWaterEdit` | source/render/sector.h:70 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `selectionReadRegion` | source/render/selection_region.h:8 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shaderLocationStore` | source/render/shader_locations.h:18 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidateShaderLocations` | source/render/shader_locations.h:25 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `linkShaderProgram` | source/render/shader_locations.h:31 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shaderLocation` | source/render/shader_locations.h:36 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `uniformLocation` | source/render/shader_locations.h:64 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `attributeLocation` | source/render/shader_locations.h:66 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shadowAtlasResolution` | source/render/shadow_cascade.h:13 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cameraEyeFromView` | source/render/shadow_cascade.h:21 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cameraFrustumSliceCorners` | source/render/shadow_cascade.h:38 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CSpatialGrid` | source/render/spatial_grid.h:30 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `clear` | source/render/spatial_grid.h:33 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `insert` | source/render/spatial_grid.h:40 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `query` | source/render/spatial_grid.h:88 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `querySwept` | source/render/spatial_grid.h:146 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `entryCount` | source/render/spatial_grid.h:194 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cellCount` | source/render/spatial_grid.h:195 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `overflowCount` | source/render/spatial_grid.h:196 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cellKey` | source/render/spatial_grid.h:214 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `materialLayer` | source/render/terrain_atlas_analyzer.cpp:67 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `isUniform` | source/render/terrain_atlas_analyzer.cpp:75 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasAnalyzer::analyze` | source/render/terrain_atlas_analyzer.cpp:83 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `catalog` | source/render/terrain_atlas_analyzer.h:44 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `overlayCatalogWithLandFamilies` | source/render/terrain_atlas_analyzer.h:49 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `archiveIndex` | source/render/terrain_atlas_archive_binding.cpp:32 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `memberPayload` | source/render/terrain_atlas_archive_binding.cpp:113 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `normalizedPath` | source/render/terrain_atlas_archive_binding.cpp:122 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `archiveEpoch` | source/render/terrain_atlas_archive_binding.cpp:127 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `exactAtlasDigestCache` | source/render/terrain_atlas_archive_binding.cpp:161 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `bindingEpochKey` | source/render/terrain_atlas_archive_binding.cpp:166 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasArchiveBindingResolver::matchesArchiveEpoch` | source/render/terrain_atlas_archive_binding.cpp:173 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasArchiveBindingResolver::readArchiveMembers` | source/render/terrain_atlas_archive_binding.cpp:184 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasArchiveBindingResult::hasCompleteArchive` | source/render/terrain_atlas_archive_binding.cpp:255 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasEmbeddedBindingResult::matchesLoadedAtlas` | source/render/terrain_atlas_archive_binding.cpp:265 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasEmbeddedBindingResult::canReuseDecodedAtlasTiles` | source/render/terrain_atlas_archive_binding.cpp:304 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasArchiveBindingResolver::findEmbeddedArchive` | source/render/terrain_atlas_archive_binding.cpp:343 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasArchiveBindingResolver::findExactArchive` | source/render/terrain_atlas_archive_binding.cpp:426 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `success` | source/render/terrain_atlas_archive_binding.h:23 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `success` | source/render/terrain_atlas_archive_binding.h:38 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `hash` | source/render/terrain_atlas_compact.cpp:29 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `read` | source/render/terrain_atlas_compact.cpp:30 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `u32` | source/render/terrain_atlas_compact.cpp:31 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `put` | source/render/terrain_atlas_compact.cpp:32 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `pageName` | source/render/terrain_atlas_compact.cpp:33 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `inventory` | source/render/terrain_atlas_compact.cpp:34 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainCompactProfileMatchesPublished` | source/render/terrain_atlas_compact.cpp:76 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `prepareTerrainAtlasCompact` | source/render/terrain_atlas_compact.cpp:84 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `buildTerrainArchiveWithout` | source/render/terrain_atlas_compact.cpp:281 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainCompactRetiredMembers` | source/render/terrain_atlas_compact.cpp:293 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainCompactRetiredMembers` | source/render/terrain_atlas_compact.cpp:307 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainCompactInventoryUnchanged` | source/render/terrain_atlas_compact.cpp:321 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `estimateTerrainCompactPublication` | source/render/terrain_atlas_compact.cpp:323 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `commitTerrainAtlasCompact` | source/render/terrain_atlas_compact.cpp:337 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainAtlasHasHash` | source/render/terrain_atlas_compact.cpp:407 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainAtlasPageLimit` | source/render/terrain_atlas_compact.cpp:408 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainAtlasMapDirectories` | source/render/terrain_atlas_compact.cpp:413 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainAtlasCopySourceIdentities` | source/render/terrain_atlas_compact.cpp:422 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `prepareTerrainPackCleanup` | source/render/terrain_atlas_compact.cpp:427 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `commitTerrainPackCleanup` | source/render/terrain_atlas_compact.cpp:469 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `retireTerrainAtlasAfterSave` | source/render/terrain_atlas_compact.cpp:477 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `compactTerrainPublication` | source/render/terrain_atlas_compact.cpp:490 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `publicationMeaning` | source/render/terrain_atlas_compact.cpp:503 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `reuseTerrainPublication` | source/render/terrain_atlas_compact.cpp:510 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `presetArray` | source/render/terrain_atlas_presets.cpp:14 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `parseUnderwaterRole` | source/render/terrain_atlas_presets.cpp:45 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `presetAtlasIdentity` | source/render/terrain_atlas_presets.cpp:54 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `matchingPreset` | source/render/terrain_atlas_presets.cpp:67 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::presetCount` | source/render/terrain_atlas_presets.cpp:86 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::materialTheme` | source/render/terrain_atlas_presets.cpp:91 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::globalMaterialId` | source/render/terrain_atlas_presets.cpp:98 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::globalUniformFamilies` | source/render/terrain_atlas_presets.cpp:119 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::loadGlobalUniformSource` | source/render/terrain_atlas_presets.cpp:292 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::loadGlobalTransitionSource` | source/render/terrain_atlas_presets.cpp:422 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::globalAuthoredTransitions` | source/render/terrain_atlas_presets.cpp:475 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::globalizedCatalog` | source/render/terrain_atlas_presets.cpp:571 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasPresetCatalog::exactProfile` | source/render/terrain_atlas_presets.cpp:649 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `layerName` | source/render/terrain_atlas_profile.cpp:27 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `parseLayer` | source/render/terrain_atlas_profile.cpp:33 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `signatureJson` | source/render/terrain_atlas_profile.cpp:44 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `parseSignature` | source/render/terrain_atlas_profile.cpp:54 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `SCornerColor` | source/render/terrain_atlas_profile.cpp:70 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `cornerColor` | source/render/terrain_atlas_profile.cpp:86 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `underwaterRoleName` | source/render/terrain_atlas_profile.cpp:145 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `parseUnderwaterRole` | source/render/terrain_atlas_profile.cpp:154 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainVertexJunctionThumbnail` | source/render/terrain_atlas_profile.cpp:164 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `colorDistance` | source/render/terrain_atlas_profile.cpp:220 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `uniform` | source/render/terrain_atlas_profile.cpp:252 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::toJson` | source/render/terrain_atlas_profile.cpp:260 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::toBytes` | source/render/terrain_atlas_profile.cpp:335 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::fromJson` | source/render/terrain_atlas_profile.cpp:341 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::fromBytes` | source/render/terrain_atlas_profile.cpp:560 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::save` | source/render/terrain_atlas_profile.cpp:578 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::load` | source/render/terrain_atlas_profile.cpp:592 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::normalizeMembership` | source/render/terrain_atlas_profile.cpp:605 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::mergeFamilies` | source/render/terrain_atlas_profile.cpp:655 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::splitFamily` | source/render/terrain_atlas_profile.cpp:713 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::assignSlotToFamily` | source/render/terrain_atlas_profile.cpp:763 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::replaceSlotFamily` | source/render/terrain_atlas_profile.cpp:793 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::setSlotCornerFamily` | source/render/terrain_atlas_profile.cpp:865 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::unassignSlot` | source/render/terrain_atlas_profile.cpp:921 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::setFamilyThumbnail` | source/render/terrain_atlas_profile.cpp:937 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::assignedSlotCount` | source/render/terrain_atlas_profile.cpp:964 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::embeddedMemberName` | source/render/terrain_atlas_profile.cpp:972 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::workingAtlasProvenance` | source/render/terrain_atlas_profile.cpp:988 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::isWorkingAtlasProfile` | source/render/terrain_atlas_profile.cpp:993 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::portableFinalizedProfile` | source/render/terrain_atlas_profile.cpp:999 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::workingAtlasProfile` | source/render/terrain_atlas_profile.cpp:1123 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::profilePath` | source/render/terrain_atlas_profile.cpp:1271 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::profilePathForMpr` | source/render/terrain_atlas_profile.cpp:1288 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::save` | source/render/terrain_atlas_profile.cpp:1305 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::saveForMpr` | source/render/terrain_atlas_profile.cpp:1320 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::load` | source/render/terrain_atlas_profile.cpp:1336 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::loadForMpr` | source/render/terrain_atlas_profile.cpp:1344 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `isExplicitTerrainAtlasOverride` | source/render/terrain_atlas_profile.cpp:1370 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `validateExplicitTerrainAtlasOverride` | source/render/terrain_atlas_profile.cpp:1380 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::loadExplicitOverride` | source/render/terrain_atlas_profile.cpp:1404 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::loadExplicitOverrideForMpr` | source/render/terrain_atlas_profile.cpp:1421 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileStore::removeLocalOverride` | source/render/terrain_atlas_profile.cpp:1437 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::validateBinding` | source/render/terrain_atlas_profile.cpp:1452 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::approvedAnalysis` | source/render/terrain_atlas_profile.cpp:1480 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `STerrainAtlasProfile::usableAnalysis` | source/render/terrain_atlas_profile.cpp:1515 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `atlasPixelDigest` | source/render/terrain_atlas_profile.cpp:1659 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileInference::pixelSha256` | source/render/terrain_atlas_profile.cpp:1688 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileInference::currentProvenance` | source/render/terrain_atlas_profile.cpp:1700 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileInference::isCompatibleInferenceProvenance` | source/render/terrain_atlas_profile.cpp:1706 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileInference::fromAuthoredAnalysis` | source/render/terrain_atlas_profile.cpp:1716 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainAtlasProfileInference::propose` | source/render/terrain_atlas_profile.cpp:1797 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `allows` | source/render/terrain_atlas_profile.h:106 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `hash` | source/render/terrain_base_hd.cpp:20 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `pixelHash` | source/render/terrain_base_hd.cpp:23 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `read` | source/render/terrain_base_hd.cpp:30 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `rotate` | source/render/terrain_base_hd.cpp:31 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `mse` | source/render/terrain_base_hd.cpp:34 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `randomAt` | source/render/terrain_base_hd.cpp:45 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `pure` | source/render/terrain_base_hd.cpp:51 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `prepareBaseTerrainHd` | source/render/terrain_base_hd.cpp:54 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CBaseHdSeams` | source/render/terrain_base_hd_seams.h:14 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `safe` | source/render/terrain_base_hd_seams.h:16 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `boundarySafe` | source/render/terrain_base_hd_seams.h:35 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `edge` | source/render/terrain_base_hd_seams.h:42 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `signatureJson` | source/render/terrain_paint_diagnostic.cpp:7 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `originName` | source/render/terrain_paint_diagnostic.cpp:17 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainWorkingPhysicalIdentityJson` | source/render/terrain_paint_diagnostic.cpp:33 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainAuthoredPhysicalIdentityJson` | source/render/terrain_paint_diagnostic.cpp:116 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTerrainPreviewContext` | source/render/terrain_preview_context.h:8 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `~CTerrainPreviewContext` | source/render/terrain_preview_context.h:12 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `layerOrder` | source/render/terrain_resolve_audit.cpp:18 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `deltaLess` | source/render/terrain_resolve_audit.cpp:23 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `canonicalDeltas` | source/render/terrain_resolve_audit.cpp:36 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `addInt` | source/render/terrain_resolve_audit.cpp:43 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `signatureJson` | source/render/terrain_resolve_audit.cpp:50 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `packedJson` | source/render/terrain_resolve_audit.cpp:60 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `deltaJson` | source/render/terrain_resolve_audit.cpp:70 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `fieldJson` | source/render/terrain_resolve_audit.cpp:86 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `pointJson` | source/render/terrain_resolve_audit.cpp:96 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainResolveAuditTriggerName` | source/render/terrain_resolve_audit.cpp:105 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainResolvePackedDeltaSha256` | source/render/terrain_resolve_audit.cpp:118 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainResolveChangedCellCount` | source/render/terrain_resolve_audit.cpp:143 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainResolveSessionDeltaSha256` | source/render/terrain_resolve_audit.cpp:150 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainResolveAuditSessionJson` | source/render/terrain_resolve_audit.cpp:162 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainResolveAuditJournalPath` | source/render/terrain_resolve_audit.cpp:293 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `appendTerrainResolveAuditSession` | source/render/terrain_resolve_audit.cpp:304 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `terrainShadowInputsChanged` | source/render/terrain_shadow_inputs.h:8 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `uploadRgbaAtlasPages` | source/render/texture_page_upload.h:12 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `contextsShareGpuResources` | source/render/tile.cpp:18 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `getNewIndex` | source/render/tile.cpp:84 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `tileRotation` | source/render/tile.h:62 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `preferredAtlasColumns` | source/render/tile.h:65 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `atlasPageRowOrigin` | source/render/tile.h:73 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `atlasTileOrigin` | source/render/tile.h:82 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setMaxZ` | source/render/tile.h:99 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `arrVertex` | source/render/tile.h:100 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setMaterialIndex` | source/render/tile.h:101 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `materialIndex` | source/render/tile.h:102 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `stagedGeometryGeneration` | source/render/tile.h:148 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `uploadedGeometryGeneration` | source/render/tile.h:149 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainTilePreviewElevation` | source/render/tile.h:173 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `topdownTransparent` | source/render/topdown_capture.h:10 | не проверялась отдельно | CPU raster сохранить; отдельный native offscreen/preview вход, GL FBO до переноса; 290 |
| `topdownMask` | source/render/topdown_capture.h:15 | не проверялась отдельно | CPU raster сохранить; отдельный native offscreen/preview вход, GL FBO до переноса; 290 |
| `clear` | source/render/unit_support_surface.h:15 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `add` | source/render/unit_support_surface.h:16 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `height` | source/render/unit_support_surface.h:39 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `intersect` | source/render/unit_support_surface.h:54 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `faceCount` | source/render/unit_support_surface.h:81 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `key` | source/render/unit_support_surface.h:84 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `inside` | source/render/unit_support_surface.h:85 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `localizedAiGraphLayerLabel` | source/render/view.cpp:144 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `localizedAiGraphPreviewSelectionLabel` | source/render/view.cpp:160 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCanvasResizePreview` | source/render/view.cpp:176 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setPlacementDrag` | source/render/view.cpp:188 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setImage` | source/render/view.cpp:197 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `wheelEvent` | source/render/view.cpp:234 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `resizeEvent` | source/render/view.cpp:241 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `mousePressEvent` | source/render/view.cpp:247 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `mouseMoveEvent` | source/render/view.cpp:264 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `mouseReleaseEvent` | source/render/view.cpp:278 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `fitImage` | source/render/view.cpp:291 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isMinimapStaticGeometry` | source/render/view.cpp:313 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isEditorObjectHelper` | source/render/view.cpp:330 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isDynamicViewportObject` | source/render/view.cpp:345 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `viewportVisibilityIcon` | source/render/view.cpp:352 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `updateViewportVisibilityButton` | source/render/view.cpp:378 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `paintIssueCategoryLabel` | source/render/view.cpp:389 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `globalMaterialCatalog` | source/render/view.cpp:398 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `globalMaterialBlendStyles` | source/render/view.cpp:409 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `materialRuntimeTypes` | source/render/view.cpp:419 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `saveTerrainAtlasRecoverySnapshot` | source/render/view.cpp:433 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `globalTransitionKey` | source/render/view.cpp:466 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `runtimeTypedTransitionKey` | source/render/view.cpp:477 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `selectWorkingPaintVariantWithRuntimeType` | source/render/view.cpp:492 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainTransitionTopology` | source/render/view.cpp:522 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainVertexBrushPoints` | source/render/view.cpp:536 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainAtlasBindingKey` | source/render/view.cpp:563 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `findTerrainPaintArchive` | source/render/view.cpp:580 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainNativeSourceNamespace` | source/render/view.cpp:605 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `preferNativeTerrainBank` | source/render/view.cpp:612 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainAtlasTileCacheKey` | source/render/view.cpp:625 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainAtlasBindingStillValid` | source/render/view.cpp:633 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `hydrateTerrainGlobalBinding` | source/render/view.cpp:697 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `loadBoundTerrainAtlasTile` | source/render/view.cpp:774 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `loadGlobalTransitionTile` | source/render/view.cpp:857 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `loadBoundGlobalUniformSource` | source/render/view.cpp:873 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `currentMapGlobalUniformSource` | source/render/view.cpp:925 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `cachedGlobalUniformSource` | source/render/view.cpp:952 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `aiGraphLayerColor` | source/render/view.cpp:1003 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainAtlasPixelSha256` | source/render/view.cpp:1014 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainGridSignatureJson` | source/render/view.cpp:1032 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `packedTerrainTileJson` | source/render/view.cpp:1042 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `m_view` | source/render/view.cpp:1062 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view.cpp:1073 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view.cpp:1077 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view.cpp:1088 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTerrainVerticalScalePreviewCommand` | source/render/view.cpp:1104 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view.cpp:1110 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view.cpp:1111 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view.cpp:1112 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTerrainTilePreviewCommand` | source/render/view.cpp:1125 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view.cpp:1131 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view.cpp:1132 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view.cpp:1141 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `COverlaySurfacePreviewCommand` | source/render/view.cpp:1155 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view.cpp:1161 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view.cpp:1162 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view.cpp:1171 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `COverlayTerrainPreviewCommand` | source/render/view.cpp:1185 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view.cpp:1193 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view.cpp:1198 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view.cpp:1208 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `m_view` | source/render/view.cpp:1229 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view.cpp:1237 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view.cpp:1247 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view.cpp:1257 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderMaterials` | source/render/view.cpp:1277 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `renderAnimatedTiles` | source/render/view.cpp:1300 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `protectTerrainAnimationRanges` | source/render/view.cpp:1314 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `openGlErrorName` | source/render/view.cpp:1331 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `SParticleVisualStyle` | source/render/view.cpp:1347 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `particleVisualStyle` | source/render/view.cpp:1360 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isPersistentFireParticle` | source/render/view.cpp:1405 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isPersistentSmokeParticle` | source/render/view.cpp:1410 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `SkyFigureReader` | source/render/view.cpp:1424 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `skip` | source/render/view.cpp:1426 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `u16` | source/render/view.cpp:1433 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `u32` | source/render/view.cpp:1442 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `f32` | source/render/view.cpp:1451 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `offset` | source/render/view.cpp:1460 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `size` | source/render/view.cpp:1461 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `jsonVector` | source/render/view.cpp:1468 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `jsonMatrix` | source/render/view.cpp:1473 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `projectedPoint` | source/render/view.cpp:1482 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `visualizedPackedDepth` | source/render/view.cpp:1500 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `decodeLegacySkyFigure` | source/render/view.cpp:1520 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `mergeTerrainHydrationCaches` | source/render/view.cpp:1656 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CView::attach` | source/render/view.cpp:2376 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `LazyParticleTexture` | source/render/view.cpp:7083 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `stringFromUnsignedChar` | source/render/view.cpp:11535 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainTilePixelSha256` | source/render/view.cpp:15093 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CView::getTerrainBrushPos` | source/render/view.cpp:16446 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `translateLogicSlotJson` | source/render/view.cpp:25749 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `patrolAuthoringIntegrationCheck` | source/render/view.cpp:27649 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainBaselineAnimationIntegrationCheck` | source/render/view.cpp:27844 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ai::realizeAgentPaintSignature` | source/render/view.cpp:27934 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `qHash` | source/render/view.h:33 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `suspendRendering` | source/render/view.h:240 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `resumeRendering` | source/render/view.h:241 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `activeTerrainSectorSize` | source/render/view.h:243 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `travelEditing` | source/render/view.h:253 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `activeTerrainArchivePath` | source/render/view.h:270 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `canvasNavigationStale` | source/render/view.h:279 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `diplomacyMapMode` | source/render/view.h:294 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `hasActiveMob` | source/render/view.h:317 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `hasDirtyWorldTerrain` | source/render/view.h:318 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `hasPendingTerrainPublication` | source/render/view.h:319 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setTileGridVisible` | source/render/view.h:332 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `navmeshPanel` | source/render/view.h:334 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `settings` | source/render/view.h:336 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `mobs` | source/render/view.h:339 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isLandscapeVisible` | source/render/view.h:345 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `terrainPaintPickUsesLandLayer` | source/render/view.h:360 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setLeverApproachOverlay` | source/render/view.h:375 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `leverApproachScreenPoint` | source/render/view.h:376 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isDatabasePlacementPending` | source/render/view.h:408 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isLogicPlacementPending` | source/render/view.h:410 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `entranceOverlaysVisible` | source/render/view.h:429 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `currentMob` | source/render/view.h:473 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `logicSlotIndex` | source/render/view.h:474 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `shaderObject` | source/render/view.h:477 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `objectTree` | source/render/view.h:516 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `drawWater` | source/render/view.h:554 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `drawLand` | source/render/view.h:555 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `isPreviewTile` | source/render/view.h:556 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `setPreviewTile` | source/render/view.h:557 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sessionWorkspaceKind` | source/render/view.h:601 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sessionWorkspaceIdentifier` | source/render/view.h:602 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sessionWorkspaceDisplayName` | source/render/view.h:603 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sessionWorkspaceMapZoneId` | source/render/view.h:604 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `apply` | source/render/view_assemblies.cpp:26 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `AssemblyTagCommand` | source/render/view_assemblies.cpp:32 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `undo` | source/render/view_assemblies.cpp:35 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `profileCatalogPath` | source/render/view_assemblies.cpp:37 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `gallery` | source/render/view_assemblies.cpp:210 | не проверялась отдельно | общие данные сохранить; native appearance/placement ingress, неперенесённое через GL; 289 |
| `CBaseHdCommand` | source/render/view_base_hd.cpp:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view_base_hd.cpp:24 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view_base_hd.cpp:25 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `fileBytes` | source/render/view_canvas_resize.cpp:19 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `fileKey` | source/render/view_canvas_resize.cpp:22 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `canvasNavigationMarkerPath` | source/render/view_canvas_resize.cpp:24 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `canvasNavigationIsStale` | source/render/view_canvas_resize.cpp:27 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `sourceMob` | source/render/view_diplomacy.cpp:19 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `completeTable` | source/render/view_diplomacy.cpp:25 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CViewFrustum` | source/render/view_frustum.h:14 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `intersectsSphere` | source/render/view_frustum.h:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `intersectsAabb` | source/render/view_frustum.h:36 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `intersectsSweptSphere` | source/render/view_frustum.h:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `plane` | source/render/view_frustum.h:75 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `acceptMobs` | source/render/view_map_save.cpp:79 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `saveMapCompanions` | source/render/view_map_save.cpp:109 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `rotationFields` | source/render/view_object_randomization.cpp:15 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `angleDifference` | source/render/view_object_randomization.cpp:20 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `apply` | source/render/view_object_randomization.cpp:36 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `RandomizeObjectsCommand` | source/render/view_object_randomization.cpp:67 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `undo` | source/render/view_object_randomization.cpp:75 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `redo` | source/render/view_object_randomization.cpp:76 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `invalidatesAiGraphPreview` | source/render/view_object_randomization.cpp:77 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CWorldTerrainHistoryCommand` | source/render/view_world_terrain.cpp:33 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `stroke` | source/render/view_world_terrain.cpp:35 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `undo` | source/render/view_world_terrain.cpp:36 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `redo` | source/render/view_world_terrain.cpp:37 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `invalidatesAiGraphPreview` | source/render/view_world_terrain.cpp:41 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `worldStroke` | source/render/view_world_terrain.cpp:43 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `~FailureReport` | source/render/view_world_terrain.cpp:979 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `worldRenderMaterials` | source/render/view_world_terrain.cpp:1347 | не проверялась отдельно | CPU документ/Undo сохранить; native terrain ingress и обновления секторов, GL до переноса; 288 |
| `CTextureList::texture` | source/resources/resourcemanager.cpp:1376 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTextureList::textureDefault` | source/resources/resourcemanager.cpp:1569 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTextureList::materialClass` | source/resources/resourcemanager.cpp:1575 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTextureList::releaseTerrainTexture` | source/resources/resourcemanager.cpp:2238 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTextureList::cacheTexture` | source/resources/resourcemanager.cpp:2335 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CTextureList::cachedTexture` | source/resources/resourcemanager.cpp:2344 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `textureSources` | source/resources/resourcemanager.h:182 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CCreateObjectForm::attach` | source/ui/createobjectform.cpp:201 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ItemSpellConstructorDialog::ItemSpellConstructorDialog` | source/ui/item_spell_constructor_dialog.cpp:120 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `LeverApproachDialog::LeverApproachDialog` | source/ui/lever_approach_dialog.cpp:26 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMalachiteBridge::CMalachiteBridge` | source/ui/malachite_bridge.cpp:20 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMobScriptWindow::CMobScriptWindow` | source/ui/mob_script_window.cpp:25 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CMobScriptWindow::openExternalEditor` | source/ui/mob_script_window.cpp:176 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CModelGallery::CModelGallery` | source/ui/model_gallery.cpp:152 | не проверялась отдельно | CPU raster сохранить; отдельный native offscreen/preview вход, GL FBO до переноса; 290 |
| `attachView` | source/ui/randomize_form.h:23 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `CScenarioPanel::CScenarioPanel` | source/ui/scenario_panel.cpp:37 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `attachParents` | source/ui/select_window.h:24 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `configureTravelCombo` | source/ui/travel_combo.h:8 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::Viewport` | source/render/gl_dispatch.h:37 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::Clear` | source/render/gl_dispatch.h:38 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::ClearColor` | source/render/gl_dispatch.h:39 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::ColorMask` | source/render/gl_dispatch.h:40 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::DepthFunc` | source/render/gl_dispatch.h:41 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::DepthMask` | source/render/gl_dispatch.h:42 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::BlendFunc` | source/render/gl_dispatch.h:43 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::LineWidth` | source/render/gl_dispatch.h:44 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::BindTexture` | source/render/gl_dispatch.h:45 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::DeleteTextures` | source/render/gl_dispatch.h:46 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::TexParameteri` | source/render/gl_dispatch.h:47 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::PixelStorei` | source/render/gl_dispatch.h:48 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::DrawArrays` | source/render/gl_dispatch.h:49 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::DrawElements` | source/render/gl_dispatch.h:50 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::ReadPixels` | source/render/gl_dispatch.h:51 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::GetBooleanv` | source/render/gl_dispatch.h:52 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::GetFloatv` | source/render/gl_dispatch.h:53 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |
| `ei::gl::GetIntegerv` | source/render/gl_dispatch.h:54 | не проверялась отдельно | GL/query/service реализация остаётся в запасном GL; native использует штатный renderer и хостовый слой; 291 |

Дополнительных определений/inline/wrappers: 1197; вместе с исходными861 — 2058 технических строк. Повторения существующих методов устранены по уточнённым anchors базы.


## Реализация и проверки287

Полнота code inventory не является PASS всех функций. Итоговые результаты ниже относятся только к явно названным сценариям. Промежуточные кандидаты отделены от итоговых бинарников.

Уточнение провайдера галереи: `CModelGallery::resourceThumbnails` (source/ui/model_gallery.cpp:506–577) и сборки используют CPU rasterization/QImage, не GL. Их нельзя включать в «убранную GL-работу»; в287 их доступность сохраняется. Отдельный CPreview остаётся GL и получает явный маршрут при показе.

### Промежуточные дефекты, до финальной матрицы

- В `case-routes-probe4` New MPR попытался выбрать исходный HDPack из configuredResourceArchives, хотя authoring reference был частным. Qt atomic replacement отказал; SHA исходного RES остался `53505ad8fab92968f091c3adaac0ad7e0f62a413a063689712cc65d59b1c742c`, новых файлов в каталоге не найдено. Этот прогон FAIL и не является проверкой создания. Изоляция исправлена на уровне первого configured HDPack; дополнительно Qt CreateFile/Move/Delete ограничены собственной work-папкой.
- На частном HDPack воспроизведён отказ atomic replacement при открытом Rebuild64. Resource-write lease хоста теперь останавливает/освобождает сессию перед RES-транзакцией, не переносит Save в DLL. В `case-create-release` создание и native кадр новой карты прошли, но процесс завершился 0xC0000374: весь прогон FAIL. Причина установлена стеком: поздний QLockFile вызвал диагностический IAT guard после разрушения его глобальных QString. Состояние изоляции теперь живёт до завершения процесса. Это дефект тестовой инфраструктуры, не доказательство ошибки DLL.
- Найдена реентрантность диагностического запуска: таймер мог войти в тест во время main.show/processEvents до настоящего app.exec. Теперь сценарий планируется после readyWindow и не входит в незавершённый startup document. Ранние EXE/цифры не финальные.
- Закрытие в новый пустой вид обнаружило повторную подачу empty document: previousNeighbours оставался от старого мира. Значение теперь принимается вместе с document, включая пустой. На итоговом кандидате закрытие возвращает настоящую пустую Qt-страницу; окончательная матрица ниже.

### Промежуточные подтверждения исправлений (c99f749c, не итоговая матрица)

- Полный профиль DejaVu требует schemaVersion2 и всех пяти jmv.v1 providers. Упрощённые первоначальные fixture schema1 их не включали: **все такие прогоны отозваны как приёмочное доказательство**, их цифры не используются. В финальных events есть положительные проверки database/characterAppearance/equipmentLayout/unitAnimation/semanticValidation, непустой базы и рас персонажей. Контрольный286 собирается заново из8668, с тем же полным профилем.
- Запись новой MPR использует первый configured HDPack, а не только terrainAuthoringArchive. Частная копия HDPack поставлена первой; источники игры читаются. Из-за штатного группирования resource directories fixture перечисляет84 RES вместо83, включая идентичный исходный HDPack после частного. Это одинаковое условие обеих сравниваемых сборок; игровая картинка не подгоняется.
- MCP read-worker сначала был остановлен тестовой защитой записи: QProcess создаёт собственные `qt-<hexPID>-…` pipes. Разрешены только pipes текущего изолированного PID и собственный MCP endpoint. Чужие мосты/буфер не используются. После исправления реальные list_profile_maps/read_profile_map прошли в GUI и headless.
- Реальное закрытие после GL→Native воспроизвело30-секундный teardown timeout. Стек собственного дочернего процесса: QGLWidget→QOpenGLContext::destroy→QOpenGLVertexArrayObject cleanup→QOffscreenSurface/create/icon. Старая CPart::releaseRetainedVertexArrays пропускала живой weak mesh cache. Теперь финальная очистка VAO охватывает и его, пока контекст существует. Промежуточный switch c99f749c: удаление CView423мс; восстановление:536мс, оба exit0. Кандидат070db3ba также промежуточный; приёмочные результаты относятся только к EXE, указанному в итоговой идентичности ниже. Это изменение освобождения ресурсов GL-хоста при завершении; изображение GL и рендера не изменено. Промежуточный отладочный GL-only прогон без этого исправления завершился0, поэтому исходный timeout нестабилен, но дорогой путь подтверждён стеком. Медленный debug-heap switch остановлен только для собственного PID и не засчитан.
- Фоновый native worker завершается до удаления CView. Отложенные phase/failure callbacks проверяют shutdown. Запросы MCP во время завершения отклоняются; запись RES сохраняет прежний commit/rollback и Undo.


### Запуск, подготовка и границы хоста

`ui/renderer` по умолчанию Rebuild64. Маркер `ui/rebuild64DefaultMigration287` записывается один раз с ограниченной межпроцессной блокировкой; сохранённый OpenGL переводится на Rebuild64. Последующий явный выбор OpenGL остаётся. Headless эту миграцию не выполняет. `manual-retry-required`/аварийные session markers имеют отдельный приоритет и миграцией не снимаются. Настройка neighborLandscapes и uiScalePercent не переписываются.

Стек viewport до показа MainWindow выбран на Qt-страницу. Без документа человек видит обычный тёмно-серый фон интерфейса, текст «Rebuild64 / Карта не открыта…», прежние панели и меню. При первом запуске поверх него остаётся штатный выбор игры/мода; отмена оставляет работающее пустое окно. Восстановление и стартовые документы идут через прежние MainWindow/CView. Старый мир скрывается страницей подготовки в начале замены документа; native HWND открывается только для готового generation. Qt-страница сообщает текущий этап и прошедшие секунды по-русски. Процент завершения не выдумывается: монолитный штатный shader-prewarm пока не даёт покомпонентного счётчика. DLL/device/shader work находятся в одном рабочем потоке, меню OpenGL не ждёт его завершения.

Общий begin/endWorkspaceLoad и startup-document batch ждут основной MOB, AddMob и world metadata. Штатная отсрочка соседей снимается обычным endWorkspaceLoad, отдельной тестовой deferNeighborLoading нет. Перед native Present нужны актуальная версия документа, полный native поток соседей, ready material epoch и готовая общая геометрия для служебного слоя. `first_frame` — подтверждение Qt-переключения страницы, отдельный `presented.jsonl` — настоящий DXGI Present. Снимок выбирается из второго потока после подтверждения Qt-страницы; итоговый вызов capture не заменяет это доказательство. Скрытый процесс не доказывает поведение видимого DWM/физического монитора: отсутствие видимой вспышки и художественную картинку окончательно проверяет только владелец.

| Работа скрытого GL | До287 | В287 при обычном native открытии | Причина остатка / первый спрос |
|---|---|---|---|
| Соседний рельеф | все11 фоновых MPR | только5 zone1 /4 zone7 | общая CPU ray query и глубина/альфа служебного слоя; дальний рельеф — по навигационному жесту либо OpenGL |
| Read-only объекты соседей | штатная GL-подготовка и загрузка | не загружаются | видимые объекты читает штатный renderer; GL-подготовка возвращается в OpenGL |
| Ресурсы неба, мировых теней, прочего GL world-pass | инициализировались при скрытом CView | ленивый ensureLegacyWorldResources | нужны только при настоящем GL-показе/GL-only инструменте |
| Автоматическое GL-запекание миникарты | выполнялось после открытия | не запускается; русское сообщение о GL | готовая embedded/cached CPU миникарта остаётся; явная регенерация включает GL |
| Основная GL-геометрия и альфа текстур | загружались | остаются | single-source pickObject и правдоподобная глубина служебного слоя; убирать до native ID/depth нельзя |
| GL service FBO/readback | применялся в286 | прежний путь | перенос самого служебного прохода отдельно, новый вид/шейдеры не менялись |
| Миниатюры CModelGallery | CPU raster/QImage | CPU raster/QImage | это не GL-работа, она не удалялась |

Промежуточный b88e2b19 создавал устройство для пустого состояния и ещё раз для map-local mounts/layout: замеры показали две подготовки shaders примерно по0,28с без выигрыша до кадра. В итоговом хосте без карты проверяются DLL/ABI и доступность Config64, но устройство/shaders не создаются. Они готовятся в worker при первом настоящем документе. После закрытия устройство/мир освобождаются; пустая MPR не подаётся в pending-frame цикл, поздняя статистика старого generation отбрасывается. Это также устраняет потенциальный120-секундный fallback пустого окна.

При смене реальных карт устройство всё ещё может пересоздаваться, если точный map-local порядок архивов/раскладка изменились. Штатный закреплённый вход не умеет переопределять mounts живой сессии; сохранять старый порядок ради скорости означало бы изменить картинку. Сеансовые Config/working copies раздельны по PID/UUID, исходный Config64 читается. Полная перезагрузка мира при подтверждённой правке объекта286 пока остаётся задачей288, не объявлена исправленной этим этапом.

Неперенесённые кисти/текстуры/overlay/лепка, water/river/road mutations и их MCP preview/apply явно требуют OpenGL. Ручной ввод кисти блокируется сообщением; GL-only dialogs/предпросмотры/каналы изображения переходят через штатный переключатель с сообщением. Персонажи/appearance/stst8 и их MCP-правки требуют GL. Перенос родных Qt-панелей CView сохраняет их экземпляры, сигналы, настройки видимости и положение. CPU операции, Undo, Save, автокопии, recovery journal и форматы остаются в документах/CView, не в DLL. Экспорт сверху в native MCP возвращает REQUIRES_OPENGL; window_snapshot получает native own readback или настоящую пустую Qt-страницу. Фоновый mapper оставляет прежний GL/query/offscreen путь, не создаёт native worker и не грузит DLL/D3D.

### Дополнения к повторной матрице

- Редкое падение начала worker локализовано по собственному dump: QString::operator== → Worker::phase, run+0x11a. Поле lastPhase стояло после std::thread и могло читаться до своего конструктора. Оно перенесено перед запуском потока; это исправление хоста287, оно не объявляется причиной исторического CView AV286. Полностью совпавший symbol twin имел одинаковые .text/.pdata с c99f749c EXE; сам twin не запускался.
- Галерея в первой финальной попытке была пустой, поскольку тестовая замена resourceRoots на файлы RES не оставляла каталогов для CMapCatalog::findNamedFile. Исправлена только изоляция: после приоритетных файлов оставлены исходные каталоги с texts.res; порядок/содержимое native mounts не меняются. Отказ того теста не засчитан как PASS галереи.
- Дополнительно исключён non-const доступ к общей диагностической QJsonObject из перехваченного QStandardPaths: hook читает неизменяемый bool. Это найденная гонка изоляции, не подтверждённая причина указанного AV.
- При native double-click общая CView ray query использует уже готовых соседей. Если нужная геометрия ещё отсутствует, хост подготавливает оставшийся GL-рельеф по требованию и повторяет исходное Qt-событие в тот же CView; readonly GL MOB при этом не подготавливаются. Изменение камеры/документа отменяет повтор с сообщением. Новая карта всё равно открывается в Rebuild64.
- Capability проверяется и перед фактическим queued MCP commit: смена GL→Native во время подготовки не позволяет применить terrain/unit/stst8 план молча. Отказ apply сохраняет законченный preview plan, документ и Undo. Отдельная проверка вводит такие планы в настоящую очередь сервера и посылает apply по собственному MCP transport; это целевая проверка границы commit, а не полный прогон каждого terrain алгоритма.


Дополнительно в287 устранена фоновая host-работа пустого viewport: подготовка ресурсов больше не перечисляет RES/не читает profiles на каждом16-мс тике. Ключ из значений настроек инвалидируется при изменении документа, RES-транзакции, настроек и повторном выборе Native. В итоговом чистом запуске `backendResourcePreparations=1`; первая настройка и закрытие/повторное открытие входят в матрицу. Это не кеш содержимого карты и не изменение mount order.


MOB-only уточнение287: штатный native ingress требует MPR. Отдельный MOB или коллекция без terrain переключается на GL с сообщением; документы, Save и предпочтение Native сохраняются. Синтетическая MPR/иная картинка не подставляется. Открытие MPR-карт и настоящая пустая страница различаются с этим состоянием.

### Найденный дефект первого MCP-чтения и исправление перед итоговой матрицей

Кандидат608d63b7 воспроизвёл настоящий AV в `CLandscape::projectPositions(QList<CNode*>&)+7` при первом `get_revision` после открытия zone1. Собственный dump и symbol twin с побайтно совпавшими `.text`/`.pdata` привязали сбой к обходу MOB в `CView::ensureTerrainAutoCatalogReady`. Непосредственное предыдущее сообщение — окончание canonical terrain preview и перепроецирование2012 объектов — само по себе причиной не считалось.

Внешний range-for шёл по изменяемому `QVector m_aMob`. Вложенная проекция персонажа вызывала `ensureUnitSupportSurface`; его non-const обход отделял Qt shared container, а присвоение кеша `m_unitSupportMobs` делало следующий detach способным освободить память старого внешнего итератора. Следующий MOB читался из освобождённого storage. Исправление на стороне редактора: внешний обход по const-снимку семейства MOB; построение support surface также читает const-снимок и const-список nodes. Высоты, collision proxies, порядок документов и алгоритм проекции не изменены; `landscape.cpp` владельца не затронут.

Целевой probe на той же Qt5.12.12 подтвердил `oldIteratorInvalidated=true`, `constSnapshotRetained=true`, одинаковые суммы6/6/6/6: `work/rebuild64-launch-287-20261107/logs/qt-cow-result.json`. Это проверка причины, не замена настоящему сценарию. Итоговые queued-capabilities, canonical-stress-2 и canonical-stress-3 вызывают первый MCP-запрос через настоящий сервер, предварительно делят контейнер с support cache и проверяют неизменность семейства MOB/числа nodes. Кандидат608d63b7 и его прогоны исключены из приёмочных чисел. Причина исторических AV286 этим автоматически не объявляется установленной.

### Метод итоговых проверок

Выполняется настоящий `main` и настоящий `MainWindow` Release EXE, без альтернативного тестового main. Диагностический аргумент `--rebuild64-launch-check <case.json>` включается только для schema287 и собственного рабочего каталога. Действия поступают в реальные QAction/диалоги/виджеты и собственный MCP transport. Положительно проверены все пять jmv.v1 provider, база и расы; fixture содержит частные копии двух MPR/MOB и требуемых AddMob. Данные игры читаются. Системный буфер обмена не используется.

Qt `WA_DontShowOnScreen` применяется до показа, штатные диалоги тоже скрыты. Проверяется отсутствие видимых HWND собственного PID. Настройки, backup, QStandardPaths, Windows Known Folders, temp и cache направлены в свою папку; локальная защита Qt Create/Move/Delete отклоняет запись вне неё. Это process-local защита, а не заявление о полном OS sandbox. Разрешены только собственный MCP endpoint и собственные QProcess pipes. Мосты/процессы владельца не подключались и не останавливались.

Native trace пишет настоящий DXGI Present, generation, map, `neighbours_accepted`, pending, SHA EXE/DLL. Для zone1 требуется5, zone7 —4, несовпадение означает FAIL. Отдельное событие Qt подтверждает выбор native-страницы. Изображения для просмотра выбираются из потока Present после такого события, а не из последнего snapshot. IAT-счётчик qwindows/SwapBuffers дал0 также в положительном GL-контроле: **его отрицательный результат отозван как доказательство отсутствия GL-показа**. Для логического выбора страницы остаются QStack/renderer events и поток DXGI; физическую вспышку скрытый запуск не доказывает. В GL-контроле проверяется полная штатная когорта11, в том числе касающиеся5/4 соседей. Если настоящий GL SwapBuffers не наблюдается, время Present не заменяется временем CPU/FBO.

Длительность Qt-паузы измеряет10-мс pulse, все интервалы свыше50мс пишутся в events. Секундомер — QueryPerformanceCounter, начало процесса берётся до CreateProcess/Popen. Измерения cold-private/warm-private используют отдельные пустые/повторно используемые частные каталоги, одинаковый профиль и карты у заново скомпилированного286 и287. Установленный mapper/старые EXE не запускаются. Физически холодный глобальный cache драйвера этим не доказывается.

## Итоговая идентичность и результаты287

**IMPLEMENTED / STAGED / PARTIAL; общий PASS287 не объявлен.** Перечень по выполненному code-аудиту полный:197 функциональных строк, все41 MCP метода,2058 технических определений/обёрток (861+1197). Состояния базы286:42 связаны по коду,122 не проверены,28 явно блокируются,5 молча не работали,0 скрытых. Это не runtime PASS всех197 функций. Пять найденных молчаливых путей закрыты в287: три GL-политики изображения требуют GL с сообщением, double-click идёт в общий CView, MOB-only получает явный GL fallback.

Итоговые Release SHA256: EXE `fc5c3aa550f8ffa8d3b2db67a3689332bbd439bd594f5599159c6f718c5e1b80`, DLL `464e4dedda7e7b7f82f00fcefce51139e54c6fc32c0d109df30ffba023b7875d`. Git archive tree `f68fb4a7d3a3be6468e228f1872a779b731765b2` поверх8668cfc49e017aec23be93641f1e75c7953bc124; полный scoped commit указан в BUILD_INFO staging. Только RTX4070SUPER, штатный DPR1/AA_DisableHighDpiScaling. Шейдеры и вид renderer не менялись.

### Поведение и проверенные пути

Обычный запуск выбирает Native до показа MainWindow. Однократный marker мигрирует сохранённый OpenGL; следующий явный GL-выбор сохраняется. Guard имеет отдельный приоритет. Настройка соседей и uiScalePercent не переписываются. Без карты — тёмно-серая Qt-страница с русской подсказкой и прежними панелями. DLL/ABI и доступность Config64 проверяются, устройство/shaders готовятся при первом мире. После закрытия подтверждены retired resources и ready_generation=0: пустая MPR не попадает в pending/120-second fallback. Поздние stats прошлого generation отклоняются.

На итоговом EXE проверены: чистые настройки; миграция/повторный запуск; первый setup с отменой и принятием; guard; стартовые MPR и MOB-only; предыдущая сессия; меню Open и Open MPR; недавние; реальный double-click в дереве галереи; neighbour/world double-click через тот же CView; смена/закрытие/переоткрытие; MCP open_map; создание MPR; ручной GL→Native→GL. Смена карты закрывает старый мир Qt-страницей до accepted generation. Перед world readback zone1 требует5 соседей, zone7 —4, несовпадение FAIL. У пустой страницы, MOB-only и новой карты вне layout=0. Содержательность первого кадра New MPR оценена отдельно ниже.

MCP runtime: list_profile_maps/read_profile_map через CPU read-worker, get_revision/get_context/read_terrain, clean Save на частных копиях, window_snapshot, close_map. Native snapshot — собственный GPU кадр/service или настоящая Qt-страница; GL/headless — прежний FBO. Terrain/unit/stst8 preview/apply/queued commit и native topdown честно требуют GL, сохраняют revision/Undo и законченный preview. Все41 схем сохранены; остальные инструменты проверены по коду, полный runtime-прогон41 не заявляется.

Headless прошёл open/read/clean Save/snapshot/close на копиях без DLL/D3D до и после. Renderer migration в нём не пишется. Как286, он создаёт скрытый MainWindow/GL context и служебные HWND: **полностью без HWND не реализован**, видимых окон0. Параллельность — только собственные GUI/headless на разных частных картах; процессы/мосты владельца не подключались и не останавливались.

Recovery: настоящая Qt-правка, Undo/Redo, production autosave, намеренный TerminateProcess собственного процесса86; новый запуск сохраняет guard, проверяет байты автокопии и точные NID/координаты, затем вручную включает Native. Восстановление в тесте — копия валидированной автокопии и штатное открытие; кнопки настоящего recovery-диалога не автоматизировались. Это **не новый настоящий DLL AV-тест и не полный UI recovery PASS**. На промежуточном b88e2b19 был один exit5 до autosave без dump/FAIL; обычный и диагностический повторы прошли, причина exit5 не установлена. Он не включён в результаты итогового EXE. Два старых session-прогона испорчены собственной одновременной арендой одной частной карты; правильный отказ lock и abort91 записаны. В итоговой матрице сессия выполнена последовательно.

| Прогон / events | Статус / exit | Рендеры | Соседи перед readback (GL terrain count) | Native Present |
|---|---|---|---|---|
| [cache-a](../../work/rebuild64-launch-287-20261107/case-accept287-cache-a/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 35 |
| [cache-b](../../work/rebuild64-launch-287-20261107/case-accept287-cache-b/events.jsonl) | PASS / 0 | Rebuild64 | zone7:4 (4) | 16 |
| [canonical-stress-2](../../work/rebuild64-launch-287-20261107/case-accept287-canonical-stress-2/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 60 |
| [canonical-stress-3](../../work/rebuild64-launch-287-20261107/case-accept287-canonical-stress-3/events.jsonl) | PASS / 0 | Rebuild64 | zone7:4 (4) | 143 |
| [clean](../../work/rebuild64-launch-287-20261107/case-accept287-clean/events.jsonl) | PASS / 0 | Rebuild64 | 0 / нет готового world readback | 0 |
| [fallback-config](../../work/rebuild64-launch-287-20261107/case-accept287-fallback-config/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1:5 (11); zone1-retry:5 (11) | 7 |
| [fallback-device-lost](../../work/rebuild64-launch-287-20261107/case-accept287-fallback-device-lost/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1:5 (11); zone1-retry:5 (11) | 7 |
| [fallback-init](../../work/rebuild64-launch-287-20261107/case-accept287-fallback-init/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1:5 (11); zone1-retry:5 (11) | 9 |
| [fallback-load](../../work/rebuild64-launch-287-20261107/case-accept287-fallback-load/events.jsonl) | PASS / 0 | OpenGL | zone1:5 (11) | 0 |
| [fallback-missing](../../work/rebuild64-launch-287-20261107/case-accept287-fallback-missing/events.jsonl) | PASS / 0 | OpenGL | zone1:5 (11) | 0 |
| [fallback-no-d3d11](../../work/rebuild64-launch-287-20261107/case-accept287-fallback-no-d3d11/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1:5 (11) | 0 |
| [first-accept](../../work/rebuild64-launch-287-20261107/case-accept287-first-accept/events.jsonl) | PASS / 0 | Rebuild64 | 0 / нет готового world readback | 0 |
| [first-cancel](../../work/rebuild64-launch-287-20261107/case-accept287-first-cancel/events.jsonl) | PASS / 0 | Rebuild64 | 0 / нет готового world readback | 0 |
| [gl-control](../../work/rebuild64-launch-287-20261107/case-accept287-gl-control/events.jsonl) | PASS / 0 | OpenGL | zone7:4 (11) | 0 |
| [guard](../../work/rebuild64-launch-287-20261107/case-accept287-guard/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1:5 (11); zone1-switch:5 (11); zone1-return:5 (11) | 6 |
| [guard/runs/run-001](../../work/rebuild64-launch-287-20261107/case-accept287-guard/runs/run-001/events.jsonl) | PASS / 0 | OpenGL | 0 / нет готового world readback | 0 |
| [guard/runs/run-002](../../work/rebuild64-launch-287-20261107/case-accept287-guard/runs/run-002/events.jsonl) | PASS / 0 | OpenGL | 0 / нет готового world readback | 0 |
| [headless](../../work/rebuild64-launch-287-20261107/case-accept287-headless/events.jsonl) | PASS / 0 | OpenGL | zone1:5 (11) | 0 |
| [headless-saved-native](../../work/rebuild64-launch-287-20261107/case-accept287-headless-saved-native/events.jsonl) | PASS / 0 | OpenGL | zone7:4 (11) | 0 |
| [mcp](../../work/rebuild64-launch-287-20261107/case-accept287-mcp/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 128 |
| [migration](../../work/rebuild64-launch-287-20261107/case-accept287-migration/events.jsonl) | PASS / 0 | OpenGL | 0 / нет готового world readback | 0 |
| [migration/runs/run-001](../../work/rebuild64-launch-287-20261107/case-accept287-migration/runs/run-001/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | 0 / нет готового world readback | 0 |
| [missing-startup-2](../../work/rebuild64-launch-287-20261107/case-accept287-missing-startup-2/events.jsonl) | PASS / 0 | OpenGL | 0 / нет готового world readback | 0 |
| [missing-startup-3](../../work/rebuild64-launch-287-20261107/case-accept287-missing-startup-3/events.jsonl) | PASS / 0 | OpenGL | 0 / нет готового world readback | 0 |
| [mob-only](../../work/rebuild64-launch-287-20261107/case-accept287-mob-only/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | mob-only:0 (0) | 0 |
| [new-map-initial-camera](../../work/rebuild64-launch-287-20261107/case-accept287-new-map-initial-camera/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5); launch287new:0 (0) | 494 |
| [parallel-gui](../../work/rebuild64-launch-287-20261107/case-accept287-parallel-gui/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 8 |
| [parallel-headless](../../work/rebuild64-launch-287-20261107/case-accept287-parallel-headless/events.jsonl) | PASS / 0 | OpenGL | zone7:4 (11) | 0 |
| [post-parallel](../../work/rebuild64-launch-287-20261107/case-accept287-post-parallel/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 10 |
| [queued-capabilities](../../work/rebuild64-launch-287-20261107/case-accept287-queued-capabilities/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 66 |
| [recovery](../../work/rebuild64-launch-287-20261107/case-accept287-recovery/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1-recovered:5 (11); zone1-recovered-native:5 (11) | 8 |
| [recovery/runs/run-001](../../work/rebuild64-launch-287-20261107/case-accept287-recovery/runs/run-001/events.jsonl) | INTENTIONAL_UNCLEAN / 86 | Rebuild64 | zone1-before-edit:5 (5); zone1-after-edit:5 (5) | 54 |
| [resource-refusal](../../work/rebuild64-launch-287-20261107/case-accept287-resource-refusal/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5); zone1-after-refusal:5 (5) | 13 |
| [routes](../../work/rebuild64-launch-287-20261107/case-accept287-routes/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5); zone7:4 (4); zone1:5 (5); zone7-change:4 (4); zone1:5 (5); zone1:5 (5); zone7-world:4 (4); zone1-mcp:5 (5); launch287new:0 (0) | 404 |
| [session](../../work/rebuild64-launch-287-20261107/case-accept287-session/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 7 |
| [session/runs/run-001](../../work/rebuild64-launch-287-20261107/case-accept287-session/runs/run-001/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 10 |
| [shader-switch](../../work/rebuild64-launch-287-20261107/case-accept287-shader-switch/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | 0 / нет готового world readback | 0 |
| [startup-mob-only](../../work/rebuild64-launch-287-20261107/case-accept287-startup-mob-only/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | mob-only:0 (0) | 0 |
| [startup-zone1](../../work/rebuild64-launch-287-20261107/case-accept287-startup-zone1/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 9 |
| [startup-zone7](../../work/rebuild64-launch-287-20261107/case-accept287-startup-zone7/events.jsonl) | PASS / 0 | Rebuild64 | zone7:4 (4) | 10 |
| [switch](../../work/rebuild64-launch-287-20261107/case-accept287-switch/events.jsonl) | PASS / 0 | OpenGL, Rebuild64 | zone1:5 (5); zone1-switch:5 (11); zone1-return:5 (11) | 19 |
| [case-perf-r3-287-rebuild64-zone1-cold-private](../../work/rebuild64-launch-287-20261107/case-perf-r3-287-rebuild64-zone1-cold-private/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 10 |
| [case-perf-r3-287-rebuild64-zone1-warm-private](../../work/rebuild64-launch-287-20261107/case-perf-r3-287-rebuild64-zone1-warm-private/events.jsonl) | PASS / 0 | Rebuild64 | zone1:5 (5) | 10 |
| [case-perf-r3-287-rebuild64-zone7-cold-private](../../work/rebuild64-launch-287-20261107/case-perf-r3-287-rebuild64-zone7-cold-private/events.jsonl) | PASS / 0 | Rebuild64 | zone7:4 (4) | 12 |
| [case-perf-r3-287-rebuild64-zone7-warm-private](../../work/rebuild64-launch-287-20261107/case-perf-r3-287-rebuild64-zone7-warm-private/events.jsonl) | PASS / 0 | Rebuild64 | zone7:4 (4) | 9 |

Сводка: [matrix-audit.json](../../work/rebuild64-launch-287-20261107/matrix-audit.json); статусы `{"PASS": 44, "INTENTIONAL_UNCLEAN": 1}`. Все записанные native Present проверены по neighbours/SHA/generation.

### Измерения до и после

286 — собственная свежая Release-компиляция8668 с диагностикой настоящего main/MainWindow; установленный mapper и старые EXE не запускались. Итоговый контрольный SHA `4ce4d73ca3cee0d95d1648cd8e7f62bab53820453154a144f711c2cb3955c14e`. Его успешные восемь измерений сохранены после последних изменений287: входы/бинарник286 не менялись. Первый невалидный цикл измерения отозван: неверный swap filter и ожидание скрытой GL object-upload очереди вместо готового native мира. Контроль286 заканчивается намеренно87 после измерений/readback/проверки скрытости: его teardown этим не принимается. Отдельный GL regression287 на итоговом EXE закрывается штатно0.

Cold-private/warm-private — пустая/готовая собственная cache-папка, **не доказанный холодный глобальный кеш драйвера**. В Native286 выбран ready generation окончательной MOB-когорты, ранний частичный world не подменяет готовый кадр. DXGI ready может ещё находиться под Qt-страницей подготовки; отдельная колонка — Present после Qt-показа. Замеры инструментированы собственным readback/PNG; первый DXGI timestamp снимается до PNG, задержка Qt-показа может включать диагностический readback. Настоящий GL SwapBuffers не удалось наблюдать: вместо отсутствующей цифры Present приведены отдельно GL11 CPU-ready и FBO, **они не считаются её заменой**.

| Версия / режим | Карта / private cache | Процесс→interactive, с | Open→DXGI ready, с | Open→Present после Qt-показа, с | GL11 ready / FBO, с | Max Qt-пауза всего прогона, с | Shader prepare, мс |
|---|---|---|---|---|---|---|---|
| 286 OpenGL | zone1 / cold-private | 3.862 | — | — | 24.052 / 25.987 | 2.590 |  |
| 286 OpenGL | zone1 / warm-private | 3.285 | — | — | 29.569 / 31.568 | 2.311 |  |
| 286 OpenGL | zone7 / cold-private | 3.398 | — | — | 29.731 / 33.550 | 2.484 |  |
| 286 OpenGL | zone7 / warm-private | 3.091 | — | — | 32.307 / 34.987 | 2.258 |  |
| 286 Rebuild64 | zone1 / cold-private | 3.791 | 10.689 | — | — | 2.708 | 304.9 |
| 286 Rebuild64 | zone1 / warm-private | 3.499 | 10.634 | — | — | 2.432 | 304.4 |
| 286 Rebuild64 | zone7 / cold-private | 3.743 | 13.390 | — | — | 2.732 | 308.53 |
| 286 Rebuild64 | zone7 / warm-private | 3.479 | 12.207 | — | — | 2.426 | 300.72 |
| 287 Rebuild64 | zone1 / cold-private | 2.992 | 14.044 | 14.258 | — | 2.610 | 300.79 |
| 287 Rebuild64 | zone1 / warm-private | 3.031 | 13.161 | 13.379 | — | 2.404 | 305.76 |
| 287 Rebuild64 | zone7 / cold-private | 2.847 | 13.684 | 13.925 | — | 2.396 | 296.66 |
| 287 Rebuild64 | zone7 / warm-private | 3.043 | 11.980 | 12.144 | — | 2.116 | 289.28 |

Полные phases (device/shaders/document), MPR visible-load timings и SHA: [performance-results.json](../../work/rebuild64-launch-287-20261107/performance-results.json). Основная цена остаётся в CView/MPR/CPU-каталогах и общей query/service геометрии, затем native document/material/neighbour preparation. CPU appearance/геометрия ещё нужны общему pick/service и GL fallback; их удаление сломало бы не перенесённые функции. Первый MCP canonical revision остаётся монолитным. Две бесполезные подготовки устройства/shaders пустое окно→карта устранены; на итоговом запуске шейдеры готовятся для настоящего мира.

**Ускорение native открытия не подтверждено:**286 Native10,634–13,390с,287 Native11,980–14,044с до ready DXGI; при этих одиночных запусках zone1 стал медленнее, zone7 — разнонаправленно. Окно287 интерактивно за2,847–3,043с против286 GL3,091–3,862с /286 Native3,479–3,791с. До начала создания D3D у287 прошло7,067–8,443с против5,637–5,764с у286: строгий барьер окончательной CView/MOB-когорты отложил подачу мира. В287 отдельно измерены MPR return-ready1,518–1,715с, device до shaders0,134–0,158с, shaders0,289–0,306с, document call0,319–0,630с; оставшийся интервал после document включает native material/neighbour readiness и общий editor-query/service barrier. Ранний частичный мир ради цифры не показывается. Полная GL11 когорта286 готова за24,052–32,307с, но это не измеренный GL Present. Процессы владельца не останавливались; гарантированный speedup/статистический benchmark не заявляется.

### Запасные ветки, прогресс и два экземпляра

Missing DLL, corrupt DLL/LoadLibrary, no-D3D11/init/device-loss hooks, отсутствие Config64 и guard дали GL с одним сообщением и сохранённым документом. После исправления init/device-loss/Config64 прошёл ручной retry Native. Физическое отсутствие видеокарты не проверялось: ошибка устройства вызвана штатным test hook. Тяжёлая проверка устройства отложена до первого мира; доступность Config64 проверяется уже без карты.

При shader-hold3с настоящий worker готовил устройство для открываемой карты; shader interval 3303.279мс, действие меню GL 57.626мс, завершилось до shader_end=True. Показаны русский этап/секунды. Это проверка host responsiveness при задержке, не реальных108с холодного драйвера и не процент shader programs. Два собственных Native процесса открывали zone1/zone7 с общей частной cache/guard папкой; последующий запуск с картой проверил пригодность кеша. Ещё одна пара — GUI Native+headless на разных копиях: [parallel-results.json](../../work/rebuild64-launch-287-20261107/parallel-results.json).

### Изображения и оставшиеся ограничения

Автор просмотрел собственные кадры zone1/zone7, world transition, New MPR и пустую Qt-страницу. Это поток Present после выбора native-страницы, не итоговый snapshot и не экран владельца. Реестр/QPC/generation/neighbours/SHA: [selected-flow.json](../../work/rebuild64-launch-287-20261107/selected-flow.json); вывод просмотра: [image-review.json](../../work/rebuild64-launch-287-20261107/image-review.json). OpenGL не эталон изображения. Вода, границы, fog и служебные надписи renderer оставлены штатными.

В routes первый New MPR кадр был сплошным фоном: world-neighbour переход ранее перенёс общую камеру в координаты zone7, а обычное открытие/MCP/New MPR её не сбрасывают (журнал rebase и код277). Отдельный итоговый new-map-initial-camera без такого перехода показал текстурированную поверхность нового маленького холста в первом Present после Qt-показа, generation12/0 соседей. Native ingress и материалы новой MPR этим подтверждены. Оба кадра сохранены; положение камеры и вид renderer ради снимка не менялись.

Открыты: реально холодный глобальный driver cache/~108с; физическое отсутствие подходящей GPU; настоящий GL Present в скрытом QGLWidget; кнопки UI recovery; видимое DWM/отсутствие физической вспышки/художественная приёмка (только владелец); полностью без HWND headless. Исторический exit5 промежуточного b88e2b19 не объяснён повторными PASS. IAT-счётчик0 также у положительного GL-контроля, поэтому его отрицательное значение отозвано как доказательство отсутствия GL-показа.

Вопросы Илье: безопасная изоляция реально холодного driver cache, progress/cancel callback shader prepare; смена mounts/layout живой owned-сессии без нового устройства; native ingress MOB-only. Native ID/depth и object/sector delta — прежние вопросы, самодельной замены внутри renderer нет. План288–292 выше, следующий этап запускаю я после приёмки, установка только по слову владельца.

### Сборка, состав, сохранность

Свежий `build-staging-287-rebuild64-launch-20261107`, финализатор/validator PASS:1364→1365 managed paths,0 потерь. Добавлен только `docs/validation/ATAMAN_REBUILD64_LAUNCH_287_20261107.md`. Изменены EXE/MCP/DLL, два runtime-компонента, VERSION/Readme/CHANGELOG/BUILD_INFO/QM: [package-receipt.json](../../work/rebuild64-launch-287-20261107/package-receipt.json). Fixtures/profiles/cache в staging отсутствуют.

Release mapper из git archive, qmake/make через8.3. Полный wrapper собрал287 MCP/.NET; x86 cmd long-path отказал, обе неизменённые recipes успешно выполнены через8.3. После GUI-исправлений повторно собран mapper; входы MCP/runtime/version не менялись: [component-receipt.json](../../work/rebuild64-launch-287-20261107/component-receipt.json). OwnedRenderer.lib и shader.bin переиспользованы283/285/286 без пересборки; NVIDIA SDK не возвращён. Vendor repositories не изменялись, private source в Git/пакет не копировался.

QM: lrelease архивного TS, lconvert readback3341→3360,19 новых сообщений: [qm-receipt.json](../../work/rebuild64-launch-287-20261107/qm-receipt.json). Рабочий QM владельца bf9cab… сохранён. Чужие landscape/render_stall_trace/mob_load_pipeline/QM/.mcp/report277/cleanup scripts исключены; в коммите view.cpp только свои LF-строки,13 строк владельца остаются рабочими, neighbor file аналогично6. ROADMAP сохраняет owner suffix. SHA/проверки: [protected-precommit.json](../../work/rebuild64-launch-287-20261107/protected-precommit.json). Git eol проверен: индекс view.cpp/neighbor LF, рабочий view.cpp CRLF; mixed не внесён.

При OpenGL сохранены штатная загрузка мира/все11 соседей/мини-карта/инструменты. Изменены общая безопасная const-итерация canonical/support и финальная очистка VAO живого weak cache; resource lease не переносит Save в DLL. Диагностика не включается без строгого собственного аргумента. Удалённых функций/файлов нет, uiScalePercent не менялся. Установленная286 не обновлялась, owner settings/AppData/ProgramData/процессы не изменялись, subst/junction не создавались. Очистка собственных payload и точные исключения: cleanup-plan.json/cleanup receipt. STOP BEFORE INSTALLATION, no push.

Очистка после относящихся PASS:3605 файлов/13 720 313 298байт,2255 точных целей только внутри собственной work-папки;0 заблокированных. Тестовые MPR/MOB/RES, atlas copies, временные backup/cache и неиспользуемые промежуточные изображения удалены. Связанные логи/выбранные кадры/сборочный материал/офлайн crash evidence сохранены. Исходные игровые fixtures повторно проверены SHA: [cleanup-receipt.json](../../work/rebuild64-launch-287-20261107/cleanup-receipt.json), [cleanup-plan.json](../../work/rebuild64-launch-287-20261107/cleanup-plan.json).

Специальные навыки работы с приватными источниками недоступны; правки ограничены Ataman/хостом и закреплённым ABI/рецептом. Приватные исходники движка в Git и staging не переносились.

## R2 — ускорение открытия и общая стартовая камера, 2026-11-07

**Поправка к прежнему результату 76580e0.** Старые замеры выше относятся к R1 и остаются историческими. Они не доказывают ускорение открытия. Сплошной голубой New MPR после перехода через соседа был дефектом общей камеры, а не приемлемым результатом. Он исправлен в R2. Формулировка продолжения: **следующий этап запускаю я после приёмки, установка только по слову владельца**. В этой доработке следующий этап не начинался; установка и push не выполнялись.

### Реализация и границы

- Rebuild64 получает независимый снимок исходных MPR и окончательной транзитивной когорты MOB/AddMob. Разрешение AddMob идёт через тот же `CMobFamilyValidator::addMobPaths`, без копии правил редактора. Worker устройства/шейдеров запускается после проверки MPR/привязки атласа и камеры, параллельно чтению когорты и подготовке CView. Передача байтов не ждёт проекцию объектов, GL-атласы, миниатюры и query/service geometry. Названия `cpu.cohort`, `native.create`, `native.document` и phase timestamps позволяют проверить это непосредственно.
- В первый мир передаются настройки той же карты: sky/cave из каталога, час и ветер из MOB, авторские point lights. Состав native neighbours известен из штатной раскладки; renderer начинает их подготовку при принятии независимого документа, параллельно подготовке CView. Барьер показа по-прежнему требует всю когорту, ожидаемые 5/4 native neighbours, pending=0, settled=true и готовые материалы. Снято только ожидание пикселей служебного слоя; его отсутствие не меняет мир renderer.
- Общая геометрия инструментов и служебный слой могут завершиться позднее первого мира. Настоящие Qt mouse/key/wheel/focus events сохраняются в очереди и воспроизводятся в исходный CView после готовности. Пока данных нет, есть русское сообщение ожидания. Закрытие/замена карты с невыполненным вводом явно ждёт его применения. Очередь сохраняется при штатном переходе на OpenGL. Undo, Save и владение документом остаются в хосте.
- Native primary MOB использует существующую CPU-подготовку/проверку фонового AddMob, затем каноническую проекцию в коротких порциях. MPR parse/binding и вычисление границ карты вынесены в QtConcurrent. Native-only pulse между завершёнными GL-страницами/секторами обслуживает события; после него восстанавливаются GL texture unit/binding/unpack. Обычный OpenGL hook пуст, его прежний порядок подготовки и 11 соседей сохранены.
- Каждый новый запрос открытия/закрытия меняет serial/generation. CPU-задачи проверяют его перед принятием; поздний старый MOB не публикуется. Новый запрос во время подготовки заменяет отложенный запрос, немедленно снимает право показа старого мира и затем загружает последнюю карту. Дополнительная проверка после progress event pump не даёт отменённому MPR продолжить загрузку как отдельному MOB.
- Обнаружено и исправлено искажение первого кадра при ещё скрытом CView другого размера: native lens/aspect устанавливается из фактического native viewport до первого Present и обновляется при resize. Это исправление передачи общей камеры, без подгонки вида renderer.
- Проверка стартовой камеры общая для GL и Native. Сохранённая камера, чей frustum пересекает реальные занятые высоты/прямоугольник карты, остаётся. Иначе применяется штатный resolver CAM из логики 277; при отсутствии корректного CAM — обычная ориентация редактора у центра новой карты. Учитываются Y/roll из CAM; формат сохранённой камеры не менялся. Перенос координат при переходе через соседа происходит до проверки новой карты. Поздняя перезапись восстановленной камеры удалена.

Рендер, его вид и шейдеры не менялись. В DLL изменены только встраивание: независимость барьера мира от служебных пикселей и диагностика стадий. Сохранение/форматы/автосохранение не перенесены в DLL. Ничего из существующих функций не удалено. Изменение обычного OpenGL: только требуемая общая проверка камеры и точность сохранения её roll; native ускорение/порционная подготовка ему не включаются.

### Артефакты и метод измерения

Рабочая папка: `work/rebuild64-launch-287-r2-20261107` (далее W). Итоговый Release EXE SHA256 `b4e327683fba4c46be9a8ad6465e546926a7f3c9a55fda237285117b41943d14`; DLL `f034057a95fbf275de085f1a91ec3e5c6f330ec52fc84cb13e99554c1f503c78`. Архив дерева исходников программы: `73a562892d58c78da5e4b474393099543dd7a444`. После компиляции менялись только документы; нормализованное содержимое всех затронутых программных файлов сверено с архивом, включая отдельные scoped blobs общих файлов.

Контроль 286 построен заново из `8668cfc49e017aec23be93641f1e75c7953bc124`, с изоляцией/скрытым настоящим main, наблюдателями и одинаковыми параметрами диагностического viewport; продуктовая логика 286 сохранена. EXE `c2872d48310cb3537a1f6fcbe082a7567edad2de14377431856cdc89cdad4f99`, DLL `cc5ed5ce336680f1ee2dc14c0ca2b3942058f8b12856e26e973077556c0adb69`. Установленная программа и старые EXE не запускались. Контрольные процессы 286 намеренно заканчиваются кодом 87 **после** complete.success=true и измерений; это metrics-only, штатное завершение/сохранность 286 по этим прогонам не заявляются. Итоговые процессы 287 завершаются штатно с 0.

Матрица: по три повтора zone1/zone7 × cold-private/warm-private × 286 OpenGL/286 Rebuild64/287 Rebuild64. Порядок чередуется, второй повтор развёрнут. Только RTX 4070 SUPER, фактический viewport 1920×1080, DPR=1. Для временного контроля обе версии получают одинаковую диагностическую камеру `(256,128,200)/(256,128,0)/rotation0`; routes/startup/camera tests используют обычные пути камеры. Не менялись uiScalePercent, шейдеры, renderer appearance. Cold-private означает пустой собственный Qt/native cache; глобальный driver cache и файловый cache ОС не очищались. Warm-private повторно использует собственный cache той же пары. Параллельная работа другого Codex не прекращалась; её папки/процессы не трогались. Накладные расходы подробного span/readback наблюдателя отдельно не вычтены; сравнение относится к этим диагностическим запускам.

Каждый ready/readback пишет фактический renderer, accepted neighbours, EXE/DLL SHA; несовпадение 5/4 — FAIL. Первый кадр берётся из настоящего DXGI Present потока после `qtPageShown=true`, а не из итогового snapshot. Qt gap — пересечение интервала между событиями с измеряемым окном; событие, записанное после кадра, также учитывается, если пауза пересекала кадр. Отдельно приводятся gap до первого кадра и до полной готовности инструментов.

При 15:30:13 изменился **живой** `Dejavu/Res/HDPack_Terrain.res`: исходный `53505ad8…`, новый `def4e975…`. Тест остановился на проверке источника; собственная копия осталась побайтно прежней (`53505ad8fab92968f091c3adaac0ad7e0f62a413a063689712cc65d59b1c742c`, 744700123 bytes). Продолжение заморожено на ней, без копирования нового файла и без исправления живого. Собственная копия остаётся первым источником, вторичные mod/Base источники читаются. Источник установлен владельцем при приёмке R3: штатное сохранение gipat4 через MCP установленной286, PID48164, `logs/headless-48164/workflow.log`, 15:30:13; namespace `gipat4_349a914d2fd9` заменён на `gipat4_fa74071fc87f`, создана резервная копия. Предыдущая попытка в15:29:24 завершилась «Atomic replacement failed», следующая прошла. Возможное удержание вторичного живого HDPack тестовыми процессами — гипотеза причины первого отказа, не доказанный факт. Код R2 действительно повторно включал живой RES через раскрытие исходного каталога, поэтому R3 исключает саму такую возможность полной заморозкой источников. Изменение вторичного архива может менять resource fingerprint/CPU cache, поэтому серия имеет эту дополнительную оговорку к шуму; журнал изменения сохранён. Одна GL-пара zone1/r2, внутри которой изменился вторичный архив, исключена и целиком повторена cold→warm на новом частном кеше при стабильном хеше источника; исходные две записи сохранены в performance-retired-external-epoch.json. Остальные пары не пересекали момент изменения. Проверка Qt writes во всех итоговых случаях не обнаружила записи вне W.


### Итоговая серия: медиана [минимум–максимум], миллисекунды

| Версия / вид | Карта / кеш | n | Первый показанный кадр, мс | D3D начало, мс | Qt до кадра, мс | Qt до инструментов, мс | Инструменты, мс |
|---|---|---:|---:|---:|---:|---:|---:|
| 286 / OpenGL | zone1 / cold-private | 3 | недоступно | недоступно | недоступно | 1988 [1932–5259] | 31548 [26181–32390] |
| 286 / OpenGL | zone1 / warm-private | 3 | недоступно | недоступно | недоступно | 2212 [2045–2707] | 32876 [31829–40983] |
| 286 / OpenGL | zone7 / cold-private | 3 | недоступно | недоступно | недоступно | 2671 [1863–2860] | 41590 [34313–42786] |
| 286 / OpenGL | zone7 / warm-private | 3 | недоступно | недоступно | недоступно | 1909 [1887–2439] | 34845 [34065–39720] |
| 286 / Rebuild64 | zone1 / cold-private | 3 | 14225 [12769–15579] | 5606 [5521–7298] | 2743 [2207–3396] | 2743 [2207–3396] | 15305 [13576–16586] |
| 286 / Rebuild64 | zone1 / warm-private | 3 | 14890 [13102–36716] | 6701 [5724–8039] | 2608 [2387–6259] | 2608 [2387–6259] | 15898 [13933–36600] |
| 286 / Rebuild64 | zone7 / cold-private | 3 | 21973 [17318–36846] | 6906 [6235–7331] | 3498 [2331–6385] | 3498 [2331–6385] | 22697 [17919–36794] |
| 286 / Rebuild64 | zone7 / warm-private | 3 | 18074 [16919–20270] | 6505 [5981–6924] | 2371 [2354–3153] | 2371 [2354–3153] | 18666 [17518–21073] |
| 287 / Rebuild64 | zone1 / cold-private | 3 | 4356 [4201–4420] | 823 [802–866] | 330 [317–353] | 1180 [1161–1479] | 19047 [17012–19921] |
| 287 / Rebuild64 | zone1 / warm-private | 3 | 4349 [4173–7472] | 865 [854–1076] | 362 [357–408] | 1385 [1135–2756] | 19155 [16811–23777] |
| 287 / Rebuild64 | zone7 / cold-private | 3 | 4999 [4431–8344] | 1030 [952–1137] | 563 [544–619] | 1422 [1360–1804] | 23475 [20241–24409] |
| 287 / Rebuild64 | zone7 / warm-private | 3 | 5036 [4504–5671] | 1169 [1042–1326] | 587 [551–700] | 1494 [1423–1612] | 22024 [20080–23001] |

«Первый показанный» — первый Present на уже выбранной native Qt-странице с собственным сохранённым readback. «Инструменты» — полный canonical cohort/query geometry, это не момент первого мира. Для OpenGL время настоящего Present недоступно, поэтому его ячейка не заменена готовностью FBO.

Полный машинный реестр: [performance-matrix-r2.json](../../work/rebuild64-launch-287-r2-20261107/performance-matrix-r2.json), [timing-analysis-r2.json](../../work/rebuild64-launch-287-r2-20261107/timing-analysis-r2.json), [timing-summary-r2.json](../../work/rebuild64-launch-287-r2-20261107/timing-summary-r2.json). Для каждого case сохранены собственные process/events/Present/span журналы с версиями и счётчиками. Все 36 проверок полного мира успешны; это не означает выполнения временных порогов.

| Версия/вид | Карта/кеш | Процесс→интерактивное окно, мс | Шейдеры, мс | GL FBO готов, мс (не Present) |
|---|---|---:|---:|---:|
| 286/OpenGL | zone1/cold-private | 3855 [3736–4488] | недоступно | 33755 [28210–34831] |
| 286/OpenGL | zone1/warm-private | 3601 [3367–3959] | недоступно | 35399 [34005–46232] |
| 286/OpenGL | zone7/cold-private | 4370 [3676–4733] | недоступно | 44929 [36928–45842] |
| 286/OpenGL | zone7/warm-private | 3512 [3307–4161] | недоступно | 37449 [36616–42797] |
| 286/Rebuild64 | zone1/cold-private | 4032 [3716–4204] | 316 [308–356] | недоступно |
| 286/Rebuild64 | zone1/warm-private | 3569 [3500–6381] | 364 [293–369] | недоступно |
| 286/Rebuild64 | zone7/cold-private | 4400 [3732–4437] | 306 [303–384] | недоступно |
| 286/Rebuild64 | zone7/warm-private | 3356 [3191–4207] | 315 [307–328] | недоступно |
| 287/Rebuild64 | zone1/cold-private | 3942 [3879–4447] | 306 [298–315] | недоступно |
| 287/Rebuild64 | zone1/warm-private | 3990 [3378–4427] | 314 [299–328] | недоступно |
| 287/Rebuild64 | zone7/cold-private | 4135 [3644–5129] | 308 [304–309] | недоступно |
| 287/Rebuild64 | zone7/warm-private | 4368 [3343–4482] | 312 [306–322] | недоступно |

### Разбивка интервалов, сходящаяся с общим временем

Ниже конкретные cold-private r1 на обеих картах. Они выбраны заранее, а не как самые быстрые. Таблица медиан выше содержит все три повтора. Нельзя складывать медианы параллельных задач и получать время открытия. В первой таблице каждый участок wall time учтён один раз: наиболее узкий активный именованный span, включая CPU-задачи; это покрытие наблюдением, не доказательство, что параллельный GL задерживал D3D. Остаток явно не разобран. Полные интервалы/threads и другие повторы доступны в JSON.

| Открытие→device_begin, участок | zone1, мс | zone7, мс |
|---|---:|---:|
| `UNCLASSIFIED_QUEUE_OR_NATIVE_WORK` | 114.986 | 129.157 |
| `camera.validate` | 12.921 | 12.328 |
| `cpu.camera.map_bounds` | 16.986 | 33.397 |
| `cpu.cohort` | 0.396 | 0.457 |
| `cpu.mob.read_parse` | 38.724 | 36.308 |
| `cpu.mpr.atlas_binding` | 301.686 | 312.579 |
| `cpu.mpr.parse` | 7.098 | 12.456 |
| `cpu.mpr.published_binding` | 0.213 | 0.173 |
| `cpu.mpr.read` | 0.313 | 0.690 |
| `gui.gl.query_context` | 0.017 | 0.020 |
| `gui.native_dispatch` | 18.283 | 16.240 |
| `gui.validate_editable_projection` | 5.229 | 14.182 |
| `gui.validate_mob` | 79.282 | 167.033 |
| `gui.validate_mpr` | 7.442 | 11.647 |
| `mob.deserialize` | 198.706 | 390.587 |
| **Сумма = открытие→D3D** | **802.282** | **1137.254** |

MOB deserialize здесь — проверка editable projection входной MOB, а не загрузка canonical объектов/GL. Каталоги texture/object уже были построены до команды открытия; они не стоят на этом участке. Appearance, общая геометрия и GL-атласы тоже не являются барьером раннего D3D. Native worker и сырой cohort реально перекрываются, что видно по begin/end разных threads.

Для интервала «готовы байты итоговой когорты→показанный кадр» использованы последовательные gate timestamps на native критическом пути, без сложения параллельного CView. Материалы и соседи готовятся внутри штатного renderer одновременно: доступный ABI даёт aggregate pending/settled, отдельного достоверного CPU/GPU material-only таймера здесь нет. Эта часть не придумана как независимый измеренный этап.

| Когорта→первый показанный кадр | zone1, мс | zone7, мс |
|---|---:|---:|
| Ожидание окончания устройства/шейдеров и очереди document | 317.095 | 229.606 |
| Native document: разбор/создание мира | 346.871 | 2015.971 |
| Подготовка native мира/всех соседей, до neighbour_client_settled | 2466.296 | 3754.719 |
| Оставшиеся aggregate resources/materials, до pending clear | 68.146 | 467.798 |
| Readiness cycle / Present / доставка native страницы Qt | 231.871 | 387.597 |
| **Сумма = cohort_ready→кадр** | **3430.279** | **6855.692** |

| Контрольные точки от команды | zone1, мс | zone7, мс |
|---|---:|---:|
| Полные raw MPR/MOB/AddMob байты | 989.894 | 1488.517 |
| Начало устройства | 802.282 | 1137.254 |
| Создание устройства (длительность) | 171.022 | 246.369 |
| Шейдеры (длительность) | 315.080 | 307.502 |
| Первый настоящий native Present | 4198.017 | 7979.848 |
| Первый Present после выбора Qt страницы | 4420.173 | 8344.209 |
| Canonical geometry / инструменты | 19921.072 | 24409.450 |

Дополнительные **перекрывающиеся длительности** задач ниже помогают локализовать цену CView. Это не слагаемые предыдущих сумм: GL neighbour_accept включает свои atlas/sector участки, gui.mob включает projection/finish/adopt, CPU appearance может планироваться позднее. `gui.geometry.support` учитывает тело, а не стоимость каждого диагностического append. Незамеренные scheduler/filesystem/observer затраты остаются в остатке.

| Именованный span (сумма тел в open→tools) | zone1, мс | zone7, мс |
|---|---:|---:|
| `cpu.mpr.parse` | 7.098 | 12.456 |
| `cpu.mpr.atlas_binding` | 301.686 | 312.579 |
| `cpu.mpr.published_binding` | 0.213 | 0.173 |
| `cpu.mpr.read` | 0.330 | 0.710 |
| `cpu.mob.read_parse` | 86.016 | 158.414 |
| `cpu.addmob.resolve` | 2.585 | 2.900 |
| `cpu.mob.environment` | 100.999 | 169.437 |
| `cpu.cohort` | 227.067 | 388.744 |
| `cpu.primary.prepare` | 117.810 | 158.114 |
| `cpu.primary.alignment` | 87.119 | 181.302 |
| `gui.mob.project_object` | 2719.198 | 3830.986 |
| `gui.primary.serialize` | 24.680 | 48.766 |
| `gui.mob.finish` | 6.744 | 8.184 |
| `gui.mob.adopt` | 1299.593 | 4241.691 |
| `gui.addmob_slice` | 990.207 | 3760.509 |
| `gui.character.appearance_resolve` | 40.076 | 9.966 |
| `gui.geometry.support` | 50.164 | 169.550 |
| `gui.geometry.mob_figures_textures` | 0.000 | 0.000 |
| `gui.gl.query_context` | 1.062 | 0.676 |
| `gui.gl.atlas_allocate` | 0.090 | 0.070 |
| `gui.gl.atlas_page_decode` | 3766.244 | 3553.018 |
| `gui.gl.atlas_mipmap` | 2.067 | 1.898 |
| `gui.gl.landscape_sectors` | 797.252 | 575.735 |
| `gui.gl.neighbour_accept` | 7834.338 | 5936.676 |
| `gui.end_workspace` | 264.137 | 304.798 |
| `native.create` | 497.067 | 560.020 |
| `native.document` | 347.058 | 3150.396 |

Каталоги до команды: суммы и timeline сохраняются в исходных spans; они входят в процесс→интерактивное окно, а не в open→device. Отдельного тела `gui.geometry.mob_figures_textures` у native primary R2 нет: прежний монолит заменён canonical `gui.mob.project_object` и CPU prepared pipeline; строка0 не означает отсутствия геометрии. Native materials-only и часть ожидания очереди до первого frame не разрешены глубже ABI; это честный неразобранный остаток, вопрос к измеряемости, а не повод изменять renderer.

### Пороги и остающиеся паузы

**Общего PASS по скорости нет.** Максимальный первый показанный кадр 8344.2мс (`matrix-287-rebuild64-zone7-cold-private-r1`), максимальный Qt gap до кадра 700.1мс (`matrix-287-rebuild64-zone7-warm-private-r2`), до полной готовности инструментов 2755.9мс (`matrix-287-rebuild64-zone1-warm-private-r1`). Цель ≤8с/≤500мс не выполнена для всех повторов.

Полный перечень оставшихся Qt gap >200мс **по каждому из12 итоговых измерительных запусков** записан в `timing-analysis-r2.json / gapsOver200Ms`: начало от команды, длительность, перекрывающиеся GUI spans. Пример r1 обеих карт:

| Карта | От команды, мс | Qt gap, мс | Именованные перекрывающиеся GUI spans (мс; вложенные не суммировать) |
|---|---:|---:|---|
| zone1 | 0.0 | 330.2 | gui.validate_editable_projection 203.9, gui.validate_mob 79.3, gui.validate_mpr 7.4 |
| zone1 | 729.7 | 295.7 | gui.gl.landscape_atlas 111.5, gui.gl.atlas_build 111.5, gui.native_dispatch 18.3 |
| zone1 | 1881.1 | 283.3 | gui.gl.landscape_sectors 11.3 |
| zone1 | 7462.1 | 1179.9 | gui.mob.adopt 362.4, gui.end_workspace 264.1, gui.geometry.support 13.6 |
| zone1 | 8824.3 | 452.9 | gui.addmob_slice 447.8, gui.mob.adopt 446.3, gui.geometry.support 12.2 |
| zone1 | 9415.0 | 498.8 | gui.addmob_slice 492.4, gui.mob.adopt 490.9, gui.geometry.support 11.9 |
| zone1 | 10115.4 | 688.3 | gui.gl.neighbour_accept 53.6, gui.gl.landscape_atlas 44.7, gui.gl.atlas_build 44.7 |
| zone1 | 12878.6 | 544.5 | gui.gl.query_context 0.3 |
| zone1 | 13423.1 | 555.5 | gui.gl.neighbour_accept 495.5, gui.gl.landscape_atlas 483.7, gui.gl.atlas_build 483.7 |
| zone1 | 14937.4 | 556.7 | gui.gl.atlas_build 556.7, gui.gl.landscape_atlas 556.7, gui.gl.neighbour_accept 556.7 |
| zone1 | 18093.5 | 200.3 | gui.gl.query_context 0.0 |
| zone1 | 18673.0 | 236.2 | gui.gl.atlas_build 236.2, gui.gl.landscape_atlas 236.2, gui.gl.neighbour_accept 236.2 |
| zone7 | 0.0 | 619.5 | gui.validate_editable_projection 404.8, gui.validate_mob 167.0, gui.validate_mpr 11.6 |
| zone7 | 1067.3 | 343.8 | gui.gl.landscape_atlas 132.8, gui.gl.atlas_build 132.8, gui.native_dispatch 16.2 |
| zone7 | 2331.5 | 262.6 | gui.gl.landscape_sectors 3.4 |
| zone7 | 10096.0 | 1803.8 | gui.mob.adopt 790.5, gui.end_workspace 304.8, gui.geometry.support 29.5 |
| zone7 | 12361.0 | 818.6 | gui.addmob_slice 806.7, gui.mob.adopt 804.5, gui.geometry.support 24.8 |
| zone7 | 13398.8 | 868.4 | gui.addmob_slice 852.2, gui.mob.adopt 850.4, gui.geometry.support 24.7 |
| zone7 | 14382.3 | 896.2 | gui.addmob_slice 872.0, gui.mob.adopt 870.2, gui.geometry.support 26.9 |
| zone7 | 15469.7 | 949.2 | gui.addmob_slice 928.0, gui.mob.adopt 926.1, gui.geometry.support 27.6 |
| zone7 | 16529.1 | 218.0 | неразобранная очередь/вызов; причина глубже не установлена |
| zone7 | 16747.0 | 535.0 | gui.gl.neighbour_accept 94.1, gui.gl.landscape_atlas 82.5, gui.gl.atlas_build 82.5 |
| zone7 | 19224.9 | 544.3 | gui.gl.query_context 0.0 |
| zone7 | 19769.2 | 524.9 | gui.gl.neighbour_accept 434.3, gui.gl.landscape_atlas 424.0, gui.gl.atlas_build 424.0 |
| zone7 | 23192.1 | 296.5 | gui.gl.query_context 0.2 |

Что удерживает порог: перед ранним запуском ещё синхронны входные validation/deserialize editable projection (на zone7 это само пересекает500мс при нагрузке). После первого мира остаются canonical adopt/end_workspace, принятие AddMob и GL query/service atlas/sector preparation. Часть пауз почти не покрыта именованными телами — это не объявляется доказанной «ценой renderer»: остаются очереди/GL driver/readback и накладные расходы наблюдателя, без точной причинной атрибуции. Native document и neighbours/material readiness также дают длинный хвост первого кадра zone7 в шумном повторе; реальные gate timings приведены выше.

Для закрытия500мс нужна дальнейшая порционность **adopt/finish/AddMob и query/service подготовки**, не только primary object creation; входную editable validation надо получать CPU value с проверкой serial. Для уверенного8с zone7 нужно отдельно разложить native document→settled/material tail штатными измерителями или повторить на свободной машине. Ничего из этого не маскируется уменьшением viewport/соседей, отключением материалов или более слабым барьером. Эти критерии остаются открыты в287; этап288 здесь не начинался.

### Функциональные проверки итоговых b4e32768 / f034057a

| Прогон в W (`case-…`) | Рендер / соседи перед кадрами | Результат |
|---|---|---|
| `debug-r2-early-input-count` | Rebuild64, native 5 / GL query 5 | PASS: настоящие click + G X 1 Enter сразу после первого мира, до query readiness. Выбран nid56, +1м, ровно два канонических Undo шага (выбор и перемещение), Undo/Redo, очередь пуста. Ожидание применения 13835мс, без молчаливой потери. |
| `debug-r2-races` | Rebuild64, закрытая карта 0, итоговая zone7 native4 / query4 | PASS: close во время подготовки, затем открытие zone1 и запрос zone7 во время её подготовки. Старый MOB не принят; final cohort только последней карты; в Present trace нет старого мира после отмены. |
| `verify-r2-routes` | Rebuild64: zone1 5, zone7 4, New MPR 0 | PASS: меню, recent, gallery, смена карты, close/reopen, настоящий double click по соседу, собственный MCP open_map, настоящий диалог New MPR. До каждого readback точное число, generation/SHA. |
| `verify-r2-startup-zone7` | Rebuild64 native4 / query4 | PASS: настоящие main CLI MPR+MOB и первый кадр target карты. |
| `verify-r2-saved-camera` / create и restore | Rebuild64 native5 / query5 | PASS: настоящий session journal и старт main с ним, корректная сохранённая матрица сохранена точно, maximumMatrixError=0. Первый запуск сохранён в `runs/run-001`. |
| `verify-r2-gl-control` | OpenGL 11 / touching5 zone1, 11 / touching4 zone7, New MPR0; D3D DLL не загружена | PASS: обычные инструменты/числовой move/Undo/Redo, neighbour double click, New MPR и общая камера. GL FBO readback. |
| `verify-r2-headless` | headless OpenGL, 11 / touching5; D3D DLL отсутствует до и после | PASS: настоящий main `--agent-headless`, MCP read/open/read/Save копии/snapshot/close, источники всех MOB частные. |
| `verify-r2-shader-switch` | сначала Native shader worker, затем OpenGL 11 / touching5 | PASS: настоящее действие меню OpenGL во время 3с инъекции shader hold. Это проверка доступности меню, не измерение глобально холодных шейдеров. |
| `matrix-*` | 286/287 Rebuild64 — native5/4; 286 GL — 11 / touching5/4 | Три повтора каждой пары/кеша, полный журнал измерений отдельно; контроль286 код87 metrics-only, 287 exit0. |

Во всех перечисленных итоговых процессах проверены отсутствие видимых окон, неожиданных модальных диалогов и Qt-записей вне собственной папки. Нельзя переносить на итог прежние экспериментальные PASS/FAIL других EXE: например, ранний тест очереди ошибочно требовал один Undo шаг, хотя исходный click сам создаёт selection Undo; критерий исправлен на точные два шага и реальное положение объекта. Закрытие/гонки и вся таблица выше выполнены на b4/f034.

Сохранены `[events,opening-spans,presented].jsonl`, process/exit receipts и stdout каждого case. Реестр: [functional-rest-r2.json](../../work/rebuild64-launch-287-r2-20261107/functional-rest-r2.json); ранний ввод и гонки — отдельные каталоги выше. Тайминги потоков и поля readiness проверяются тестами, а не только итоговой картинкой.

### Камера и просмотр кадров

При открытии/создании после переноса через соседа прежний frustum теперь проверяется относительно новой карты. Исправление находится в общем CView, поэтому распространяется и на OpenGL. До исправления сохранение старых координат было общим путём по коду; отдельный старый установленный GL EXE не запускался. Итоговый GL neighbour→New MPR действительно пройден. Корректная сохранённая камера подтверждена фактическим session restore с ошибкой матрицы0. Ветка выбора авторского CAM повторно использует resolver277 и проверяется кодом; отдельного нового runtime доказательства загрузки конкретного authored CAM в этой изолированной fixture нет. Это косвенная часть проверки, не выдаётся за отдельный PASS.

Автор лично просмотрел первые показанные кадры итогового EXE, а не только метрики: zone1/zone7 cold и warm, early-input и New MPR после соседа. На zone1 видны поселение/река/лес, на zone7 острова/реки/мосты; материалов-заглушек и старой карты нет. У New MPR видна текстурированная поверхность с краями, а не uniform cyan. Штатный голубой фон вокруг поверхности и её форма оставлены как есть. OpenGL не является эталоном картинки.

Файлы собственных readback: [zone1 cold](../../work/rebuild64-launch-287-r2-20261107/pairs/zone1-first-cold-r1.png), [zone7 cold](../../work/rebuild64-launch-287-r2-20261107/pairs/zone7-first-cold-r1.png), [zone1 warm](../../work/rebuild64-launch-287-r2-20261107/pairs/zone1-first-warm-r1.png), [zone7 warm](../../work/rebuild64-launch-287-r2-20261107/pairs/zone7-first-warm-r1.png), [ранний ввод](../../work/rebuild64-launch-287-r2-20261107/pairs/early-input-first.png), [новая карта](../../work/rebuild64-launch-287-r2-20261107/pairs/new-map-after-neighbour-first.png). [provenance.json](../../work/rebuild64-launch-287-r2-20261107/pairs/provenance.json) содержит исходный case/seq/generation/QPC/neighbours/SHA. Новый кадр — `case-verify-r2-routes/present-4-gen-42.png`, native0 как положено отдельному новому холсту. Это не снимки экрана владельца. Дополнительно просмотрен собственный GL FBO `case-verify-r2-gl-control/launch287new-gl.png`: поверхность новой карты видна и в GL.

Видимое DWM-окно, физическое отсутствие вспышки и художественная оценка проверяются только владельцем. Настоящий GL SwapBuffers в скрытом QGLWidget по-прежнему не наблюдался, несмотря на observer Qt platform/OpenGL/Gui/Widgets. Поэтому время GL Present **недоступно**, GL FBO и готовность11 — отдельные косвенные показатели и не подменяют Present. Глобально холодный driver cache/~108с также не подтверждён этой серией.

### Сборка, состав и сохранность чужих строк

Release из git archive через 8.3-путь, qmake/mingw Release и собственный MSVC-host DLL. Шейдеры не пересобирались: сохранены OwnedRenderer.lib `e21003c22efd3999c598a9bb7e99743fc95ca1dbc19320179185f0af6ee4c2d9` и shader table `38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64`, проверенные по receipt286. Fresh MCP/runtime собраны в собственной папке; Release wrapper дошёл до x86 runtime и вернул1 из-за старого длинного пути `C:\GOG`; оба runtime компонента затем построены тем же рецептом через разрешённый 8.3-путь с exit0. Финальные изменения камеры/aspect/test observer перелинкованы обычным Release из конечного archive; неизменные свежие runtime/MCP использованы повторно. Старый wrapper receipt не выдаётся за SHA конечного EXE.

Свежий staging `build-staging-287-rebuild64-launch-r2-20261107`; финализатор запускается только на нём. Состав сравнивается с `build-staging-286-rebuild64-tools-r2-20261107`, потери0; добавлен только отчёт287. QM сгенерирован lrelease из архивного TS: **3365 finished, 0 unfinished** против3341 в286, в том числе пять новых сообщений R2. Рабочий QM владельца не менялся. Изолированный тестовый resource profile в staging не включается. Полный receipt: [package-receipt.json](../../work/rebuild64-launch-287-r2-20261107/package-receipt.json).

Общие view.cpp/view_neighbor_load.cpp/landscape.cpp собраны из специальных LF scoped blobs, исключающих соответственно13/6/5 строк владельца. Эти строки остаются в рабочей копии; включены только свои новые строки. Остальные защищённые файлы (render_stall_trace.h, mob_load_pipeline_check.cpp, рабочий QM, .mcp.json, отчёт277, cleanup scripts) сверены SHA256 и не включены. ROADMAP меняется только своими строками, чужие пункты сохранены. [protection-audit-r2.json](../../work/rebuild64-launch-287-r2-20261107/protection-audit-r2.json) фиксирует побайтную сохранность неизменяемых файлов и точное сохранение чужих patch в общих. Индекс C++ проверяется на LF, без новых CR. Версия остаётся287, новый scoped-коммит поверх76580e0; не переписывается история.

### Состояние передачи R2

Функциональные сценарии раннего полного мира, очереди ввода, отмены/последней карты, камеры, routes, GL и headless прошли на итоговых бинарниках. Временные пороги целиком не выполнены; итог **PARTIAL**, не принятие владельцем. Полный перечень функций197/план288+ выше не сокращён и здесь не объявлен завершённым. Новых требований менять renderer/шейдеры не возникло. Дополнительный вопрос Илье: штатные точные отметки native document/geometry/material readiness для разбора хвоста без самодельного вмешательства в renderer. Остальные старые вопросы сохранены, никому сообщения не отправлялись.

Копии и cleanup: поскольку общего PASS по порогам нет, необходимые частные fixtures сохранены для воспроизводимости; точный собственный остаток и план удаления после PASS — `W/cleanup-plan.json`. Чужие/288 каталоги и процессы не затронуты. На установке/видимой картинке проверяет только владелец. STOP BEFORE INSTALLATION, no push. Следующий этап запускаю я после приёмки, установка только по слову владельца; следующий этап в R2 не начинался.

## R3 — живая камера, query-only рельеф и изоляция чтения, 2026-11-07

Результаты R2 выше остаются историческими. Владелец принял ранний полный мир, параллельный старт, камеру новой карты, гонки, GL и headless, но вернул очередь навигации, поздние инструменты и Qt pauses. R3 — последний круг открытия; следующий этап здесь не начат, его запускает владелец. Установка только по его слову, push не выполняется. Номер остаётся287.

### Что изменено

1. Ввод навигации идёт сразу в исходные CView/COperation/CCamera, а не в очередь. Native tick передаёт общую камеру и во время подготовки, включая native-only projection pulse. W/A/S/D/Q/E и стрелки используют только CCamera/key manager/settings; в этом коде нет ограничения высотой или запроса GL-рельефа. Колесо использует тот же enlarge. Средняя кнопка использует обычный COperation, но pivot на поверхности берётся CPU-ray/height запросом CView::terrainPositionAt; если попадания нет, сохраняется прежний корректный pivot. Вычисление движения не зависит от готовности редактируемого документа или служебного слоя.
2. Очередь геометрических действий хранит исходное Qt-событие, view/projection, viewport и модификаторы на момент события. При воспроизведении временно используется этот снимок общей камеры; после него возвращается живая камера. Отложенные Undo/Redo вызывают те же QAction/MainWindow handlers в порядке очереди. Изменившийся размер viewport отменяет оставшийся пакет с русским сообщением, сохраняя документ. Close/смена карты сохраняют прежнюю защиту очереди. В тесте клик перед полётом выбирает исходный nid, а не объект под новой камерой.
3. CLandscape/CSector при native открытии читают CPU tiles/высоты и сохраняют исходные MPR. Не создаются мировой GL-атлас, его mipmaps, цветовые/material/water потоки и обычные GL-секторы. Для query depth/grid по требованию создаются отдельные position-only buffers из тех же девяти вершин плитки и тех же индексов треугольников. Единственный белый texel нужен старому GL depth/grid shader для alpha test, это служебный ресурс; он не передаётся в Rebuild64 и не заменяет материалы мира. Сетка запроса сбрасывается при изменении геометрии. Меши и alpha-текстуры объектов сохранены: они нужны прежнему выбору и служебному слою.
4. Убрана ещё одна ненужная зависимость: native terrain cache не читает/хеширует содержимое мирового GL-атласа. Его ключ — содержимое MPR; object resource generation имеет собственный прежний контракт. Полный OpenGL сохраняет прежние ключи, атласы, воду, материалы и11 соседей. При явном GL/запасном переходе отложенные ресурсы строятся штатным путём с русским сообщением ожидания.
5. MPR/MOB preflight и полная editable projection validation/deserialize выполняются как CPU value с проверкой request/serial перед принятием. Каноническая проекция остаётся в CView. При adopt события обрабатываются между завершёнными unit projections и стадиями; AddMob проверяет generation после возврата из adopt. Стадии end_workspace разложены на resume_tools/actions/minimap; между ними обслуживаются события и проверяется serial. До регистрации отменённый MOB удаляется явно, после неё сохраняется владение CView. Undo/Save/форматы не перенесены в DLL.
6. Найден и исправлен потерянный readback: worker раньше забирал capture request до готового кадра и мог не ответить при pending. Теперь запрос сохраняется до ответа/отмены; другое поколение даёт явную ошибку. Проверяется отдельный запрос во время подготовки последней карты. Observer повторно проверяет текущий готовый generation перед обычным snapshot.

Вид renderer и shader table не изменены. Новые отметки в embedding host: terrain page bytes, MOB resource snapshots, visual seed, material texture ingress, material-table ready epoch. Это измерители фактических вызовов/условий готовности. Texture ingress wall time измеряет вызов загрузки, а не GPU timestamp. Финальная material-table ready отметка требует pending0 и совпавший завершённый renderer epoch, как барьер показа. Отсутствие отдельного замера больше не объявляется блокером Ильи.

### Потребители GL-атласов и запасной путь

| Потребитель | Решение R3 |
|---|---|
| CView selection terrain depth / service depth (`view.cpp`, `view_editor_service.cpp`) | Untextured query geometry, исходные формы/диагонали; без world atlas. |
| Сетка служебного слоя | Те же query vertices и border indices; прежние visibility/uniform settings. |
| Height/pivot/placement query | CPU terrainPositionAt/projectPt; не строит GL-атлас. |
| Мировые land/water passes | CLandscape::ensureRenderResources перед обычным draw; прежние full streams/атлас. |
| Переход GL, включая device loss | Общий selectRebuild(false) создаёт отложенные ресурсы и затем использует прежний GL;11 соседей. |
| Terrain palette/preview (`view.cpp`), `glTexture()` | Getter материализует ресурсы по требованию. Неперенесённые действия сохраняют прежний явный GL путь. |
| `topdown_export.cpp::captureSourceAtlas`, export/миниатюры/GL captures | Getter обеспечивает настоящий atlas; обычный GL capture path сохранён. Автоматическая GL-миниатюра при native открытии по-прежнему не запускается (R1). |
| Native window_snapshot / показанный поток | Собственный D3D readback, без GL мирового atlas; запрос pending больше не теряется. |
| Headless | Прежний OpenGL путь; deferred native flags не включаются, D3D DLL не загружается. |

### Чтение ресурсов: поправка к R2 и новая изоляция

Факт установлен владельцем: живой HDPack в15:30:13 изменило штатное сохранение gipat4 через MCP установленной286, PID48164, `logs/headless-48164/workflow.log`. Namespace `gipat4_349a914d2fd9` заменён на `gipat4_fa74071fc87f`, с резервной копией. Попытка15:29:24 получила «Atomic replacement failed», следующая прошла. Соответствующая фраза R2 исправлена по прямому поручению. Связь первого отказа с удержанием файла тестом остаётся недоказанной гипотезой.

По коду R2 риск был реальным: orderedResourceSources повторно раскрывал живой каталог RES после явного private archive и включал живой HDPack вторичным mount. Исторический список handle того процесса не сохранён, поэтому конкретный отказ не приписывается тесту как доказанный.

R3 замораживает292 файла (все используемые mod/Base RES, карты и конфигурацию), сохраняет manifest SHA и не перечитывает оригиналы при каждом запуске. Состав раскладки и map roots перенаправлен в собственные копии, включая соседей/AddMob. Приоритетный terrain pack — тот же байтовый снимок, что в R2; вторичные архивы также частные. Профиль не содержит живых resource/map roots. Qt CreateFile guard запрещает попытки открытия живых mod RES/MPR/MOB, mount inventory проверяется до загрузки мира, NtQuerySystemInformation/GetFinalPathNameByHandle читает **только handles своего PID** и сохраняет список. Чужие handles не дублируются и процессы не меняются. Сам аудит выполняется как CPU value после отметки готовности, до snapshot; его стоимость не выдается за стоимость открытия.

Условия чтения теперь строже R2. Контроль286 повторно не запускался по поручению; его цифры ниже используются как историческая граница R2, не как новый одновременный контроль с иной изоляцией.

### Итоговые бинарники и проверки R3

Все итоговые проверки ниже: EXE `13cb39156ff8c8b2afc5b0a8e0dae1a33097d6cf02762c8fc5a6aef5d4ef1cfa`, DLL `5319e9599b4a3d2ffdd06c85c31ceaa6755885bd2ecd2b07d4f77523a9417801`, archive tree `51bba8b26ab83ff80090b79075006a7e974a66f5`. Release собран из git archive без строк владельца; x86 runtime/MCP собраны штатным wrapper с известным обходом длинного пути через8.3. Таблица шейдеров прежняя `38fb2f9c8931f79080ca036d75f89388170852531627c31415b3ed3d99872f64`; renderer library `e21003c22efd3999c598a9bb7e99743fc95ca1dbc19320179185f0af6ee4c2d9` не пересобиралась. Только RTX4070SUPER, DPR1, настоящий main/MainWindow, скрытые реальные Qt-виджеты и события; видимое окно и ощущение работы проверяет только владелец.

| Сценарий | Итог | Рендер / соседи |
|---|---|---|
| `seal-r3-final-live-camera` | PASS | Rebuild64/5 (GL loaded 5) |
| `seal-r3-final-prepare-races` | PASS | Rebuild64/4 (GL loaded 4) |
| `seal-r3-final-switch-gl` | PASS | OpenGL/5 (GL loaded 11); Rebuild64/5 (GL loaded 11); Rebuild64/5 (GL loaded 5) |
| `seal-r3-final-device-late` | PASS | OpenGL/4 (GL loaded 11); Rebuild64/4 (GL loaded 4) |
| `seal-r3-final-headless` | PASS | OpenGL/5 (GL loaded 11) |
| `seal-r3-final-routes` | PASS | Rebuild64/0 (GL loaded 0); Rebuild64/4 (GL loaded 4); Rebuild64/5 (GL loaded 5) |

Все case представлены в [functional-r3.json](../../work/rebuild64-launch-287-r3-20261107/functional-r3.json); события, opening-spans, process/exit и shown Present trace находятся рядом в соответствующем case. Перед снимком проверены текущие renderer/generation, 5/4 соседей (либо0 для нового холста), pending0/settled и SHA. GL-контроли явно отмечены OpenGL: касающихся соседей5/4, всего загруженных GL-ландшафтов11. Headless: open/read/Save/snapshot копии, DLL не загружается. Нет видимых окон, clipboard и обращений к живым заменяемым файлам.

### Камера и порядок ввода

| Настоящее Qt-событие после первого показанного кадра | Ввод → изменившийся Present, мс | Геометрия готова |
|---|---:|---|
| W | 297.3 | False |
| wheel | 487.4 | False |
| middle-drag | 458.2 | False |

`syntheticQueryHold=False`: в итоговом прогоне использовано естественное ожидание геометрии, искусственная задержка не понадобилась. Клик по исходной точке перед полётом после подготовки выбрал исходный nid56; Qt G/X/1/Enter и Ctrl+Z сохранили порядок, Redo подтвердил отменённое перемещение. Камера не воспроизводится рывком из очереди. Проверены отмена открытия, close, новая последняя карта и запрос capture при pending: ответ текущего поколения либо явная отмена, без потери запроса.

### Три повтора cold-private/warm-private

Числа — медиана [минимум–максимум], мс. Это собственный пустой/готовый кеш, не доказанное охлаждение глобального shader cache драйвера. После добавления детализации service и порционного end_workspace использованы только новые case `seal-matrix-r3-*`; промежуточные `matrix-r3-*`, `accept-matrix-r3-*`, `verify/prove-r3-*` не входят в итоговую матрицу.

| Карта / кеш | n | Первый настоящий Present | Первый показанный Present | Инструменты готовы | Максимальная пауза Qt до инструментов |
|---|---:|---:|---:|---:|---:|
| zone1 / cold-private | 3 | 6024 [5701–6854] | 6264 [5969–7129] | 13595 [12740–14005] | 1751 [1574–2310] |
| zone1 / warm-private | 3 | 5723 [4702–7136] | 5941 [4916–7430] | 12785 [10057–13556] | 1224 [426–2278] |
| zone7 / cold-private | 3 | 8683 [4448–9087] | 8959 [4659–9622] | 21672 [14482–23964] | 1498 [488–2166] |
| zone7 / warm-private | 3 | 5918 [5212–6173] | 6160 [5442–6419] | 17220 [15538–17312] | 537 [448–639] |

Полные строки12 прогонов, разбивка и все паузы>200мс: [timing-analysis-r3.json](../../work/rebuild64-launch-287-r3-20261107/timing-analysis-r3.json). [performance-matrix-r3.json](../../work/rebuild64-launch-287-r3-20261107/performance-matrix-r3.json) связывает условия с case; в каждом действительно приняты5/4 соседей, исключений/SKIP нет.

| Исторический контроль из R2 | Карта / кеш | Инструменты, мс | Qt до инструментов, мс |
|---|---|---:|---:|
| 286  | zone1 / cold-private | 15305 [13576–16586] | 2743 [2207–3396] |
| 286  | zone1 / warm-private | 15898 [13933–36600] | 2608 [2387–6259] |
| 286  | zone7 / cold-private | 22697 [17919–36794] | 3498 [2331–6385] |
| 286  | zone7 / warm-private | 18666 [17518–21073] | 2371 [2354–3153] |
| 287 R2 | zone1 / cold-private | 19047 [17012–19921] | 1180 [1161–1479] |
| 287 R2 | zone1 / warm-private | 19155 [16811–23777] | 1385 [1135–2756] |
| 287 R2 | zone7 / cold-private | 23475 [20241–24409] | 1422 [1360–1804] |
| 287 R2 | zone7 / warm-private | 22024 [20080–23001] | 1494 [1423–1612] |

Повтор286 не запускался. Изоляция чтения теперь строже: все вторичные RES/карты заморожены, тогда как R2 повторно раскрывал живой RES-каталог. Приоритетный pack сохранён побайтно как в R2. Это историческое сравнение, а не новый попарный контроль при строго тождественных условиях; параллельная работа другого Codex не останавливалась.

### Именованная разбивка и оставшиеся паузы

Суммы вложенных/параллельных spans не складываются в CPU стоимость. Ниже общая стоимость стадии по каждому открытию: медиана по12 прогонам и максимальная одиночная порция. Полный файл хранит QPC/thread/detail; service.draw отделён от glFinish и readback.

| Стадия | Медиана суммы на открытие, мс | Максимальная одиночная порция, мс |
|---|---:|---:|
| `cpu.validate_editable_projection` | 300.2 | 450.2 |
| `mob.deserialize` | 351.7 | 435.4 |
| `cpu.primary.prepare` | 157.5 | 212.0 |
| `gui.mob.project_object` | 3290.4 | 268.5 |
| `gui.mob.adopt` | 1820.5 | 748.9 |
| `gui.addmob_slice` | 1677.7 | 750.7 |
| `gui.end.resume_tools` | 212.9 | 256.7 |
| `gui.end.actions` | 23.2 | 28.0 |
| `gui.end.minimap` | 16.9 | 20.3 |
| `gui.gl.neighbour_accept` | 354.1 | 240.0 |
| `gui.gl.landscape_sectors` | 418.8 | 236.4 |
| `gui.gl.atlas_page_decode` | 0.0 | 0.0 |
| `gui.native.document_serialize` | 39.0 | 58.8 |
| `gui.native.resource_snapshot` | 16.5 | 19.4 |
| `gui.service.context` | 16.2 | 1.9 |
| `gui.service.fbo` | 33.7 | 37.8 |
| `gui.service.draw` | 1535.4 | 2254.3 |
| `gui.service.finish` | 156.5 | 411.7 |
| `gui.service.readback` | 114.1 | 59.6 |

`gui.gl.landscape_sectors` в native теперь измеряет CPU constructors/tiles/bounds, а не создание полного GL world mesh; query-depth buffers выделяются отдельно. Atlas/mips/цветовые/водные streams отсутствуют до явного GL. `gui.mob.project_object` и draw service остаются на GUI: исходные CNode/GL owners и общий выбор требуют их; вынесен CPU deserialize/preflight, а не эти владельцы. `gui.end.minimap` — refreshViewportMinimapSources (обновление источников панели, не автоматическое GL-запекание), resume_tools — возобновление CView и его сигналов.

Паузы service.draw относятся к drawEditorServiceScene: depth terrain, объектный ID/depth, выделение/метки и helpers. Это измеренная граница вызова CView; затраты CPU/драйвера внутри отдельных draw не разделены и не объявляются временем GPU. Для устранения такой паузы нужен порционный прогрев/подготовка query resources либо асинхронный service pass с безопасным снимком владельцев CView; этого в R3 нет. glFinish/readback измерены отдельно.

#### zone1: cold-private-r1, точная шкала

| Открытие → начало D3D: разбиение wall interval | мс |
|---|---:|
| `cpu.mpr.atlas_binding` | 406.627 |
| `mob.deserialize` | 262.186 |
| `outside_instrumented_scopes` | 145.375 |
| `cpu.validate_mob` | 90.516 |
| `cpu.mob.read_parse` | 29.273 |
| `cpu.camera.map_bounds` | 22.510 |
| `gui.native_dispatch` | 19.104 |
| `gui.gl.landscape_sectors` | 17.618 |
| `cpu.mpr.parse` | 8.123 |
| `camera.validate` | 7.392 |
| `cpu.validate_editable_projection` | 7.370 |
| `cpu.validate_mpr` | 7.087 |
| `gui.native.tick` | 5.577 |
| `gui.gl.landscape_archive` | 1.100 |
| `cpu.mpr.read` | 0.445 |
| `cpu.cohort` | 0.404 |
| `cpu.mpr.published_binding` | 0.169 |
| `gui.fingerprint.terrain` | 0.141 |
| `gui.fingerprint.resources` | 0.124 |
| `gui.gl.query_context` | 0.019 |
| `gui.gl.landscape_atlas` | 0.008 |
| **Сумма = начало D3D** | **1031.169** |

Разбиение отдано самому короткому покрывающему scope; это разметка интервала, не сумма параллельных CPU задач. Остаток outside_instrumented_scopes назван явно.

| Отметка хоста | От открытия, мс |
|---|---:|
| `device_begin` | 1031.2 |
| `shader_prepare_begin` | 1295.7 |
| `shader_prepare_end` | 2360.6 |
| `document_begin` | 2392.5 |
| `terrain_page_bytes_begin` | 2434.3 |
| `terrain_page_bytes_end` | 2509.0 |
| `mob_resource_snapshots_begin` | 2509.0 |
| `mob_resource_snapshots_end` | 2823.1 |
| `scene_visual_seed_begin` | 2823.1 |
| `scene_visual_seed_end` | 2952.4 |
| `document_end` | 2952.5 |
| `native_frame_work_begin` | 2953.0 |
| `material_texture_ingress_begin` | 2958.5 |
| `material_texture_ingress_end` | 2990.5 |
| `neighbour_client_settled` | 6527.1 |
| `native_material_table_ready_epoch` | 6843.0 |
| `native_resources_pending_clear` | 6843.0 |
| **Qt показанный Present** | **7128.9** |

CPU raw cohort готова на 1343.6мс; от неё до показанного кадра 5785.3мс. Непересекающееся разбиение этого интервала также сохранено в timing-analysis-r3.json; исходные MOB/AddMob/appearance/геометрия/материалы имеют собственные spans и host phases.

#### zone7: cold-private-r1, точная шкала

| Открытие → начало D3D: разбиение wall interval | мс |
|---|---:|
| `mob.deserialize` | 431.239 |
| `cpu.mpr.atlas_binding` | 398.579 |
| `cpu.validate_mob` | 158.463 |
| `outside_instrumented_scopes` | 117.938 |
| `cpu.camera.map_bounds` | 34.647 |
| `cpu.mob.read_parse` | 28.489 |
| `gui.native_dispatch` | 18.885 |
| `cpu.validate_editable_projection` | 14.659 |
| `cpu.validate_mpr` | 12.951 |
| `cpu.mpr.parse` | 12.334 |
| `camera.validate` | 11.386 |
| `gui.gl.landscape_sectors` | 10.919 |
| `gui.native.tick` | 7.402 |
| `gui.gl.landscape_archive` | 1.642 |
| `cpu.mpr.read` | 0.681 |
| `cpu.cohort` | 0.382 |
| `gui.fingerprint.terrain` | 0.209 |
| `gui.fingerprint.resources` | 0.177 |
| `cpu.mpr.published_binding` | 0.135 |
| `gui.gl.query_context` | 0.031 |
| `gui.gl.landscape_atlas` | 0.008 |
| **Сумма = начало D3D** | **1261.157** |

Разбиение отдано самому короткому покрывающему scope; это разметка интервала, не сумма параллельных CPU задач. Остаток outside_instrumented_scopes назван явно.

| Отметка хоста | От открытия, мс |
|---|---:|
| `device_begin` | 1261.2 |
| `shader_prepare_begin` | 1503.7 |
| `shader_prepare_end` | 1852.2 |
| `document_begin` | 1882.2 |
| `terrain_page_bytes_begin` | 1922.8 |
| `terrain_page_bytes_end` | 1980.7 |
| `mob_resource_snapshots_begin` | 1980.8 |
| `mob_resource_snapshots_end` | 2357.4 |
| `scene_visual_seed_begin` | 2357.5 |
| `scene_visual_seed_end` | 2604.5 |
| `document_end` | 2604.6 |
| `native_frame_work_begin` | 2604.9 |
| `material_texture_ingress_begin` | 2610.8 |
| `material_texture_ingress_end` | 2622.7 |
| `neighbour_client_settled` | 8318.1 |
| `native_material_table_ready_epoch` | 8668.2 |
| `native_resources_pending_clear` | 8668.3 |
| **Qt показанный Present** | **8958.5** |

CPU raw cohort готова на 1708.3мс; от неё до показанного кадра 7250.2мс. Непересекающееся разбиение этого интервала также сохранено в timing-analysis-r3.json; исходные MOB/AddMob/appearance/геометрия/материалы имеют собственные spans и host phases.

### Границы результата R3

**Функциональные проверки PASS; временные цели целиком не достигнуты, общий результат PARTIAL.** Камера живая с первого показанного кадра и очередь геометрических действий корректна. Убрана ненужная подготовка GL terrain atlas/mips/full sectors; CPU preflight и deserialize вынесены, adopt/end_workspace обслуживают события с serial guards. Однако инструменты≤10с и Qt≤500мс на всём интервале открытия не подтверждены. Остались исходные object projection/GL draw service, отдельные синхронные обновления панелей и шум параллельной нагрузки. Полная детализация внутри service.draw не выполнена; она не подменяется «неразобранной очередью» и не относится к проблеме вида Rebuild64.

Смена/close/GL fallback/headless проверены на итоговом коде. В маршрутах создание новой MPR включает отдельную синхронную публикацию собственного RES и может давать паузу дольше контрольного обычного открытия; это не заявлено исправленным. Новая карта после соседней видна с первого кадра. Новых блокирующих вопросов Илье нет; измерительные точки добавлены в embedding host без изменения картинки. Следующий этап здесь не начат, его запускает владелец. Установка и push не выполнялись.

### Снимки, владение MOB и ограничения проверки

Просмотрены все12 первых показанных кадров итоговой матрицы, четыре кадра навигации, первый New MPR, два собственных GL FBO и native capture с выделением/служебным слоем. Реестр20 файлов: [pairs/provenance.json](../../work/rebuild64-launch-287-r3-20261107/pairs/provenance.json), [image-review-r3.json](../../work/rebuild64-launch-287-r3-20261107/image-review-r3.json). Это собственные readback/Present, не экран владельца. [W](../../work/rebuild64-launch-287-r3-20261107/pairs/camera-W.png), [колесо](../../work/rebuild64-launch-287-r3-20261107/pairs/camera-wheel.png), [MMB](../../work/rebuild64-launch-287-r3-20261107/pairs/camera-middle-drag.png), [новая карта](../../work/rebuild64-launch-287-r3-20261107/pairs/new-map-first.png).

W проверен с небольшим наклоном eye–target=(0,-1,100). У строго отвесного взгляда старая CCamera::shift даёт нулевую горизонтальную проекцию W в обоих видах; этот старый общий крайний случай не исправлялся. Проверка очереди камеры относится к указанному реальному сценарию, а не доказывает исправление этой старой математики.

Финальный просмотр обнаружил утечку до регистрации MOB при новой отмене adopt. В итоговом коде до m_aMob.append отменённый pMob удаляется явно; после регистрации сохраняется владение CView. Новый guard проверен по исходному владению; отдельная принудительная runtime-проверка попадания именно в этот короткий guard не инструментирована. Общие cancellation/last-map/close и связанный регресс повторены на итоговом EXE. Предыдущие6+12 прогоны EXE6086445e и их цифры — промежуточные, не итоговые.

После первой очистки для этого исправления восстановлены292 файла с теми же SHA256, включая старый priority pack из сохранённой readonly backup. [frozen-recovery-receipt.json](../../work/rebuild64-launch-287-r3-20261107/frozen-recovery-receipt.json). Backup/исходники только читались при подготовке копий, закрыты до запуска; тесты опять открывали лишь собственные копии. Необходимость повторения не подменена проверками старого EXE.

Все82 пауз>200мс перечислены в [qt-gaps-over200-r3.csv](../../work/rebuild64-launch-287-r3-20261107/qt-gaps-over200-r3.csv). Наибольшая —2309.7мс (`seal-matrix-r3-zone1-cold-private-r1`), пересекающиеся измеренные scopes: `{"gui.native.tick": 2276.4566, "gui.service.total": 2275.6928, "gui.service.capture": 2274.196, "gui.service.draw": 2254.2707, "gui.character.appearance_resolve": 38.2309}`. Вложенные интервалы не складываются повторно.

Маршрутный New MPR получает отдельный case-local publication pack. В одном промежуточном routes harness ошибочно требовал неизменности общего частного pack после штатной публикации; driver исправлен, baseline восстановлен, итоговый routes использует отдельный pack. route-private-pack-mutation.json сохраняет историю. Живой HDPack этим сценарием не открывался. Критерии10с/500мс и прежний порог первого показанного кадра8с оцениваются по таблице, а не по exit0 функционального сценария; невыполненное не объявляется PASS.

### Сохранность и очистка R3

Удалены собственные fixtures/authoring, private caches и промежуточные payloads case-* трёх launch-папок R1/R2/R3: 470 целей, 8512090441 байт (8,51GB; предыдущий проход14,27GB зафиксирован отдельно в before-ownership-fix-cleanup-receipt-r3.json). Карты/RES для следующего шага не оставлены; блокировок нет. Сохранены cited logs, process/exit/events/opening-spans/Present traces, выбранные исходные PNG и pairs. [cleanup-receipt-r3.json](../../work/rebuild64-launch-287-r3-20261107/cleanup-receipt-r3.json), [cleanup-plan.json](../../work/rebuild64-launch-287-r3-20261107/cleanup-plan.json). Папки/процессы288, чужие checkout и установка не изменялись.

[protection-audit-r3.json](../../work/rebuild64-launch-287-r3-20261107/protection-audit-r3.json): owner13/6/5 строк view.cpp/view_neighbor_load.cpp/landscape.cpp исключены из archive/индекса и остаются в рабочей копии, прочие защищённые файлы сохранены побайтно. Параллельные добавления ROADMAP также сохранены вне коммита. Scoped C++ blobs только LF; действующий AGENTS.md SHA256 `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7` проверен. Новые правила/полномочия не вводились.

### Сборка и состав R3

Свежий `build-staging-287-rebuild64-launch-r3-20261107`, финализатор exit0. Против `build-staging-286-rebuild64-tools-r2-20261107`:1364→1365 управляемых файлов, **0 потерь**; добавлен только `docs/validation/ATAMAN_REBUILD64_LAUNCH_287_20261107.md`. Новые DLL/SDK/шейдеры не добавлены. Изменены EXE, embedding DLL, свежесобранные MCP/runtime, VERSION/Readme/CHANGELOG/BUILD_INFO и QM. QM: **3367 finished,0 unfinished** (R2:3365,286:3341), SHA256 `ca8f7665e32457cf77ad693869695e304a92891dd47c3f34a07627cbf167ebdd`. Рабочий QM владельца не менялся. Изолированный resource profile исключён из пакета. [package-receipt.json](../../work/rebuild64-launch-287-r3-20261107/package-receipt.json) и stage SHA256SUMS проверяют каждый файл; BUILD_INFO после scoped-коммита указывает его полный SHA. Удалённых функций/настроек нет. GL обычный путь и headless сохранены; новый отложенный путь включается при Rebuild64. Установка, push и следующий этап не выполнялись.

### Установка 287 — 2026-11-07

По явному поручению владельца «ставь 287» установлен неизменённый проверенный `build-staging-287-rebuild64-launch-r3-20261107` в `artifacts/EI-Mapper-Ataman-2-first-test`. До установки VERSION=286; HEAD main точно `682c495bca6f5c290f3f78fb1155ad585087492c`, diff принятого коммита с рабочим packaging/* пуст. Использован только репозиторный `packaging/install_program.ps1`; пересборка, финализация и изменение staging не выполнялись.

- Установщик: `INSTALLED verified=1365 changed=10 added=1 removed=0`. Все 1365 установленных управляемых SHA256 и SHA манифеста совпали со staging, включая EXE, ataman_rebuild64.dll, runtime/EIMapperEnduranceJmvBridge.plg, runtime/EIMapperEnduranceRuntimeHost.exe и translations/ei_mapper_ru.qm.
- Контрольные EXE/DLL: `13cb39156ff8c8b2afc5b0a8e0dae1a33097d6cf02762c8fc5a6aef5d4ef1cfa` / `5319e9599b4a3d2ffdd06c85c31ceaa6755885bd2ecd2b07d4f77523a9417801`. `EIMapperMcp.exe --version`: `EIMapperMcp 287`, exit0.
- Откат `artifacts/rollback286-to287-rebuild64-launch-20261107`: штатный `rollback.ps1 -VerifyOnly` → `ROLLBACK_VERIFIED`. Прежние поколения отката сохранены.
- SHA256 всех 15410 пользовательских файлов вне managed manifest совпали с предустановочным снимком. Настройки вида/соседей не менялись; миграция Rebuild64 остаётся первому запуску маппера. Регистрация мостов не изменялась.
- До замены зафиксированы PID мостов 17412, 49824, 40536, 36036, 2608, 55780, 16456; после установки все живы с тем же временем создания (при сравнении учтена потеря долей миллисекунды в JSON). Ни один процесс не завершался.
- Занятый `EIMapperMcp.exe` отодвинут в `EIMapperMcp.exe.old-ataman-7afec64e64ae4104bb1ce9512c6815cb`. Также остался прежний `EIMapperMcp.exe.old-ataman-0faf7fe49f064623b59d728f72a63a89`. Установщик штатно удалил один освобождённый зарегистрированный retired-файл `EIMapperMcp.exe.old-ataman-e4ab1eab98e14e43ab799e091bfd2d2d`; остальные файлы и прежние откаты не очищались.
- Маппер/host из установки и игра при проверках отсутствовали; не запускались. DLL не загружалась, computer-use и видимых окон не было. `subst` пуст. Чужие изменения, включая ROADMAP, код, VERSION/CHANGELOG/Readme, QM и work288, не включены в этот коммит.

Квитанции: `work/install-287-20261107/before.json`, `install.txt`, `result.json`. Этап EI-REBUILD64-LAUNCH-287 остаётся **PARTIAL по времени открытия**; установка не закрывает временные цели. Видимая приёмка за владельцем, push не выполнялся.
