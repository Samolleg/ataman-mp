# EI-PALETTE-HOTFIX-152: проверка восстановления

Дата: 2026-10-12. Ветка main. Программное исправление: `8f82ce3d341dbfe755ef77d7de34429a6a5a2102`.
Проверяемый source SHA после добавления harness: `e3ac06061fc844df844dc67a16d096e9f5ec0d96`, VERSION152.

## Исходный сценарий

`--terrain-random-paint-check` с `paletteRecovery=true` выполняет скрытый desktop OpenGL прогон, без интерактивного редактора. До проверки готовности harness не вызывает ensureTerrainAutoCatalogReady: карту загружает production loadLandscape, каталог заполняют кадр и event loop. Затем production beginTileBrushGroup/setTile/endTileBrushGroup меняет землю. Undo и Redo точны. Сохранение современного черновика выполняет production saveChangedLandscapeDocument (allowModernDraft=true), с публикацией только в скопированный HDPack и повторным чтением MPR; все painted sector bytes совпали.

Использованы актуальные приватные копии zone1 и HDPack DejaVu, вне живой установки и Git. Исходные SHA256: MPR `1333A596DB880C9D73670F24F115BECB38BAB45D3D2B04BD9662CCD3DB23DA87`; RES `79AE99A3BD47CDE0B36E73A774B20E2CFBABCDD9491AAD49DC1E1CF8D3376909`. Хеши копий перед запуском совпали. Живые файлы не записывались.

- Итог: 10 PASS, 0 FAIL; selectable land82, overlay7.
- openReturn1356 ms; firstFrame1362 ms; paletteReady4522 ms. Это разные фазы. Миниатюры догружались ещё25554 ms; каталоги были доступны раньше. Замер одного открытия не является статистическим сравнением с148.
- UI-lock: build-locks/ui-tests.lock; CWD/APPDATA/LOCALAPPDATA/QSettings/backup в owned test directories; clipboard backend in-memory, nativeAttempts0, sequence4790→4790, formats8→8, unchanged1.
- Снимки земли и покрытия после hydration просмотрены. Функциональная палитра заполнена; компоновка152 остаётся прежней, её доведение относится к153.
- Локальные журналы/PNG: build-tests-152-recovery/palette-check-final.log, output/palette-land.png, output/palette-overlay.png. Это приватные evidence artifacts, в Git и пакет они не включены.

Промежуточные отказы harness не скрыты: сначала resource root был задан архивом вместо каталога и save ожидал native file dialog; затем harness отклонил штатное подтверждение замены entries в копии HDPack. Исправлены только тестовые условия, production hotfix не менялся. Повторный итог выше.

## Сборка и границы

Release из clean committed archive, Qt5.12.12/MinGW7.3 x64; runtime bridge/host пересобраны. EXE SHA256 `C80AD5A91AD0D908599B901114AF670ADB6FFFD2A7C9BC9B734BD433554325A3`.
Вложенные snapshot builds отвергнуты: qmake ошибочно сокращал пути к одноимённым файлам родительского checkout. Принятая сборка находится в owned temp archive вне checkout; Makefile указывает только его source. Uncommitted HD/стыки/панель в152 не включены.
`--version` сообщает EI Mapper Endurance Edition152. Первый startup fail-fast был связан с отсутствующим plugin path; с явным Qt plugins запуск успешен. Staging проверяется с собственными deployed plugins.

Встроенная `--ui-widget-check`: exit0, widget interaction/lifecycle PASS, nativeAttempts0 и unchanged1.
Staging `build-staging-152-recovery-final`: EXE хеш совпал с tested executable; staged --version152 прошёл с собственными deployed plugins. Finalizer/validator PASS:132 prior/133 current,0 removals. Manifest SHA256 `130E529D05B072D5D01DAB1972CF2055B5BF131ECA0D6B0DA942557A16311218`.
Guarded установка151→152 в `artifacts/EI-Mapper-Ataman-2-first-test` завершена:133 managed files,7 замен,1 добавление,0 удалений. Все23149 unlisted owner files сохранились; before/after inventory SHA256 `36715F6E18101B39CE802A904A18F0B0BA6FF96643DE40B8E34BC62D2784D478`. Rollback151: `artifacts/rollback151-EI-PALETTE-HOTFIX-152-20261012`. Локальный receipt: build-tests-152-recovery/install-receipt.txt. Первая попытка адаптированного installer остановилась на старом version guard148 до backup/записи; исправлены guards151→152, итог выше.

Ручная приёмка владельцем и игра остаются отдельными; ordinary Save MPR publication и реальные mouse/IME этой проверкой не заявляются. Пакетная копия отчёта создана до установки; этот канонический отчёт содержит итоговый receipt.
