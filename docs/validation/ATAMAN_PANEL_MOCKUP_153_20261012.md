# EI-PANEL-MOCKUP-153: проверка панели

Дата:2026-10-12. База main `a342604e041fca7d4ced93f2fe099ad7c51b51db`, установленная152. Макет: build-tests-panel-mockup/approved-mockup.html. VERSION153, без интеграции чужих HD/стыков.

Приняты относящиеся незавершённые landscape_tools.cpp правки: крупные плитки/плашки, общий ряд формы/цели, selected-count строка. Дополнены компактная сетка команд карты, minimum-size palette contents и refresh выбранного материала по текущему слою. Исходные варианты пока доступны одной thumbnail плюс assignedSlots count; полная исходная лента требует согласованного изменения данных view.cpp. Это открытое требование, не PASS.

Адресные widget-проверки: sharedBrushSizePersistsAcrossModes, paintPaletteUsesVisibleWidthOnFirstActivation(ground/overlay), usedMaterialFilterTracksMapAndSelection, landscapeModeSegmentsKeepEverySurfaceActionAvailable, manualPaletteIsRequestedOnlyWhenItsTabIsOpened, terrainPaletteBatchDefersIntermediateWidgetRebuilds, deferredTerrainThumbnailRefreshesWithoutInitialsFallback, overlaySurfaceModeLocksTileTargetAndOverlayLayer и landscapePanelPolishControlsAndOrder прошли,0 skipped. Три старых ожидания (family heading, map fold, variant title) обновлены по утверждённому макету; они сначала отказали и после адресной правки прошли.
Тесты используют offscreen QPA/QSettings temporary directory; нет desktop input или системного clipboard. Снимки на реальной карте, полная Release/UI-check, staging/установка153 ещё не выполнены. Родительская цель остаётся открытой.
