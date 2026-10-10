# EI-TEST-CLIPBOARD-ISOLATION-134 — 2026-10-10

## Поручение и состояние

Поручение владельца через сопровождающего: «сделай тесты без системного буфера».
Исходная база main: `50eba3aaa02ce50037fa427de879960c41a6c5d6`, VERSION 133.
Проверен актуальный AGENTS.md: SHA256
`F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`.
Рабочий каталог при входе был чистым. Git использовался только для чтения.

Параллельный участник переместил HEAD в
`f182b72def81bc0dd7bfddd792901adac0790cf9` (документальный отчёт о передаче 133),
а также изменил `res_archive_index.{cpp,h}`, `res_read_lease.h`,
`model_gallery.cpp`, `tests/res_reader_drain_tests.cpp` для 133-PERF.
Эти чужие изменения сохранены; они не входят в scoped-набор этой задачи.
Проверенная сборка из рабочего каталога включает их: соответствующие объекты
созданы после последних изменений исходников. Это совместная рабочая сборка,
а не чистый git-снимок исходного SHA.

ROADMAP/CHANGELOG/VERSION/Readme и постоянная установка этим исполнителем
не изменялись. Номер задачи 134 ещё не внесён в VERSION: проверенный EXE
выводит `EI Mapper Endurance Edition 133`. Коммит и назначение версии оставлены
сопровождающему. Сеть, установки зависимостей, игровые/модовые данные,
интерактивный запуск редактора и подагенты не использовались.

## Решение и аудит путей

`verified_clipboard` содержит явную `TestClipboardSession`. До QApplication
в тестовом процессе устанавливаются нативные сторожи и снимаются метаданные.
После QApplication, до создания окон, заменяется QPA integration адаптером,
который возвращает собственный `MemoryClipboard` и передаёт остальные
виртуальные вызовы исходной Windows integration.

Хранилище владеет QMimeData, поддерживает передачу ownership, замену/очистку,
текст, произвольные MIME-байты, HTML, URLs, изображения, `ownsClipboard` и
`dataChanged`. Как Windows, оно не поддерживает Selection/FindBuffer.
`writeClipboard` сохраняет общую проверку GUI-thread/вложенной записи;
в тестах выполняет одну запись в память и прежнее snapshot/readback сравнение.
Обычная ветка с нативным preflight, retries и Qt Windows clipboard сохранена.

Аудит source/tests/scripts охватил QClipboard, QApplication/QGuiApplication
clipboard, QMimeData, Copy/Cut/Paste, стандартные меню и Win32/OLE функции:

| Путь | Реальный маршрут |
| --- | --- |
| main_window → view: копирование ID | `copyTextToClipboard` |
| view/tree_view: logic slot Copy/Paste | внутренний объектный буфер; системного API нет |
| tablemanager: свойства, компоненты, частичная текстовая выборка | verified-запись; чтение через `clipboardText`/`clipboardMimeData` |
| database_editor_dialog: ID строки | `copyTextToClipboard` |
| item_spell_constructor_dialog: код | `copyTextToClipboard` |
| message_log_panel: несколько сообщений | `copyTextToClipboard` |
| resource_browser: имя ресурса | `copyTextToClipboard` |
| main.cpp: синтетическая запись и проверка UI | verified-запись/чтение; RestoreGuard удалён |
| QLineEdit, QTextEdit и другие стандартные Qt editors/меню | их QClipboard использует тот же in-memory QPA backend |
| Win32/OLE сохранение и восстановление | только отдельно выбранный необязательный native-набор |

Замена только обработчиков Copy/Paste была бы недостаточна: стандартное
меню QLineEdit читает доступность Paste уже при создании. QPA адаптер
перехватывает и это обращение. Offscreen/minimal не выбраны, чтобы регресс
продолжал проверять реальные Windows widgets и OpenGL. Использован уже
подключённый `gui-private` и QPA договор удерживаемого Qt 5.12.12; при смене
версии Qt адаптер требуется пересмотреть.

`testClipboardRequested` распознаёт все аргументы `--…-check`, включая
`--…-check=значение`; в main.cpp найдено 22 таких опции. Без тестового аргумента
сессия не снимает метаданные, не устанавливает hooks и не заменяет integration.
Это подтверждено отдельным child-тестом `--backend-default-check`, не читающим
и не записывающим системный буфер, и сохранённой обычной веткой writeClipboard.

Нативный ClipboardRestoreGuard сохранён для отдельно разрешённых native
проверок: они продолжают нуждаться в сохранении внешних данных. По умолчанию
`verified_clipboard_tests` запускает только изолированный набор. Старые проверки
доступны через `--native-clipboard-tests` и явно названные native helpers;
в этот регресс они не включались. Попытка создать RestoreGuard в изолированном
процессе сама считается нарушением.

## Сторож

В тестовом процессе IAT-слоты приложения/Qt/загруженных сторонних модулей
заменяются запрещающими функциями для Open/Close/EmptyClipboard,
Get/SetClipboardData, Enum/CountClipboardFormats, IsClipboardFormatAvailable,
GetPriorityClipboardFormat, GetClipboardOwner/GetOpenClipboardWindow,
OleGet/Set/Flush/IsCurrentClipboard. Системные DLL не переписываются.
qwindows.dll предварительно загружается до QApplication; наблюдаемые модули
удерживаются до конца процесса. Установлены 14 слотов в приложении/reader-drain,
15 в clipboard unit executable. В проверенных путях нет динамического
разрешения clipboard API. Такой обход или новый поздно загружаемый модуль
потребует расширения сторожа при добавлении соответствующего кода.

Каждая запрещённая попытка блокируется до чтения/записи и увеличивает счётчик.
После уничтожения QApplication сравниваются счётчик, sequence number и полный
отсортированный набор format IDs; нарушение переопределяет успешный результат
прогона кодом 2. Это охватывает также уничтожение окон и Qt teardown.
Метаданные читаются только через GetClipboardSequenceNumber и
GetUpdatedClipboardFormats: OpenClipboard и чтения payload в программе или
regression harness нет. Изменение буфера внешней программой также даст FAIL.

Три отрицательных child-пробы проверяют реальный импорт OpenClipboard,
OleGetClipboard и обход адаптера через исходный Windows QPA MIME `formats()`.
Все три возвращают код 2, ненулевой nativeAttempts и `unchanged=1`.
Qt получает OLE данные отложенно: одного получения MIME-указателя для этой
отрицательной проверки недостаточно.

## Проверки и точные результаты

Release: `build-release-134-clipiso/release/EIMapperEndurance.exe`.
SHA256: `BF39FBCF3D4F4E7CC637F31473A169F415FC9AB3AD35A29DFE6E657BA46B8C33`.
Qt 5.12.12 / MinGW 7.3 x64; приложение собрано штатным build_release.cmd.
Тесты находятся в `build-tests-134-clipiso`. Оба каталога — технические
выходы, не пользовательская поставка.

Все UI прогоны выполнены только после атомарного mkdir
`build-locks/ui-tests.lock`, с owner.txt и удалением собственной блокировки.
Чужие блокировки 133-PERF/133-finalize не удалялись; исполнение ждало их
освобождения. Harness использует ограниченные по времени скрытые процессы
и проверяет exit code, isolation receipt и QtTest totals.

| Проверка | Результат |
| --- | --- |
| verified_clipboard_tests, default | 11 PASS / 0 FAIL / 0 SKIP, 271 ms; 9 сценариев + init/cleanup |
| --ui-widget-check | 169 функциональных строк PASS / 0 FAIL; ещё 1 PASS сторожа, exit 0 |
| --scenario-layer-check | 50 PASS / 0 FAIL; ещё 1 PASS сторожа, exit 0 |
| --dark-theme-check | 70 PASS / 0 FAIL; ещё 1 PASS сторожа, exit 0 |
| --model-gallery-check | функциональный PASS и PASS сторожа, exit 0 |
| reader-drain | 23 PASS / 0 FAIL / 0 SKIP, 12070 ms; 21 сценарий + init/cleanup, exit 0 |
| отрицательные пробы | 3/3 ожидаемых FAIL с exit 2; системный буфер не изменён |
| обычный режим backend | отдельный child PASS: backend=system без тестовой активации |
| --version | 133, exit 0; версия намеренно оставлена сопровождающему |

Copy/Paste/Cut/Undo регресс 120 остаётся исполняемым: partialCopyExact,
partialPasteExact/Commit, fieldCopyAvailable, selectedComponentCopy,
componentPlatePaste, componentContextPaste, pasteSingleAction,
pasteUndoAtomic/pasteRedoAtomic, compatible/incompatible/mismatched Paste,
bodyPartPropertyPasteIsExact и messageLogMultiCopyWorks равны 1 в полном UI.
Отдельный набор проверил встроенные QLineEdit copy/cut/paste/undo, обычное меню,
Ctrl+X/Z/V, verified-menu Cut/Undo, MIME readback, replacement/empty и запрет
вложенной записи из dataChanged.

Gallery: 497 моделей / 7165 текстур, open 6 ms, selection 3 ms;
Shift/keyboard/Ctrl/reverse, cache/bounded parallel/stale/all-N close PASS.

Глобальные метаданные за ВСЮ серию:

```text
GetClipboardSequenceNumber: 33461 -> 33461
formats before: 1,7,13,16,49383,49770,49794,49795
formats after:  1,7,13,16,49383,49770,49794,49795
nativeAttempts: 0 во всех шести изолированных процессах
```

Проверено текущее содержимое владельца; payload не читался, тип содержимого
не угадывался. Системные текст/OLE-скриншот/empty fixtures не устанавливались.
Независимость от содержимого следует из маршрута всех операций в память;
проверка не требует возможности сериализовать исходный системный буфер.

Логи: `build-tests-134-clipiso/regression.log`, `harness-receipt.txt`,
`*.stdout.log`/`*.stderr.log`,
`clipboard/release/clipboard-results.txt`,
`reader-drain/fixtures-resdrain/reader-drain-results.txt`.
`source-receipt.txt` фиксирует SHA256 затронутых собственных и параллельных
исходников и проверенного EXE. Сгенерированная копия reader-drain проверена на
полное совпадение с текущим оригиналом за исключением include и трёх строк
активации TestClipboardSession в entry point. Оригинал чужого набора не изменён.

Первый инженерный запуск отдельного набора до deploy/preload исправления
остановился до тестов; он завершён принудительно как собственный процесс и
не считается PASS. Исправлено разрешение qwindows относительно загруженной
Qt5Core.dll. Первый отрицательный QPA probe с ленивым MIME-указателем также
не был PASS; после запроса formats() обязательный sentinel успешно проверен.
После последней правки исходников приложения выполнен весь успешный регресс.
Изменения только harness-парсинга receipts затем проверены на полученных логах
и PowerShell AST, без повторного запуска неизменных бинарных файлов.

## Повторение

Собрать приложение в назначенный AppBuild, default verified_clipboard_tests
в `build-tests-134-clipiso/clipboard` с qmake/Makefile.Release, выполнить
windeployqt --release --compiler-runtime для обоих EXE. Затем:

```powershell
tests/clipboard_isolation_regression.ps1 -PrepareReader
```

PrepareReader создаёт изолированный entry point reader-drain только в
техническом каталоге и линкует точные объекты AppBuild, сохраняя исходный набор.
Для уже подготовленного набора этот флаг не нужен. Нативный guard-набор
отдельный, необязательный, по умолчанию НЕ запускается.

## Scoped-файлы для коммита

1. source/core/verified_clipboard.h
2. source/core/verified_clipboard.cpp
3. source/app/main.cpp
4. source/layout_components/tablemanager.cpp
5. tests/verified_clipboard_tests.cpp
6. tests/verified_clipboard_tests.pro
7. tests/clipboard_isolation_regression.ps1
8. docs/validation/ATAMAN_TEST_CLIPBOARD_ISOLATION_134_20261010.md

Чужие файлы 133-PERF, сборки, настройки, журналы и fixture-данные не включать.

## Текст для ROADMAP (вносит сопровождающий)

EI-TEST-CLIPBOARD-ISOLATION-134: тестовые --*-check используют in-memory QPA
clipboard до создания окон; стандартные Qt Copy/Paste/Cut/Undo и меню также
изолированы. Системный backend обычного запуска сохранён. Native guard-набор
отдельный/необязательный. Проверено: UI 169/0 + sentinel, scenario 50/0,
dark 70/0, gallery PASS, clipboard 11/0/0, reader-drain 23/0/0; 3 отрицательных
пробы отвергнуты, nativeAttempts=0 в положительных прогонах, sequence
33461→33461, 8 format IDs неизменны. Evidence: этот отчёт. Реализация и
регресс завершены; следующий шаг — scoped-коммит/назначение версии и
согласование совместного результата с 133-PERF. Передача/установка не выполнены.

## Текст для CHANGELOG (вносит сопровождающий)

EI-TEST-CLIPBOARD-ISOLATION-134: автоматические проверки больше не открывают,
не читают и не перезаписывают системный буфер владельца. Добавлен in-memory
backend QClipboard для тестовых режимов, включая стандартные меню и
Copy/Paste/Cut/Undo Qt. Убран RestoreGuard из обязательного UI регресса;
native guard tests запускаются отдельно. Сторож блокирует нативные обращения
и проверяет неизменность sequence/formats. UI 169/0, scenario 50/0, dark 70/0,
gallery PASS, clipboard 11/0/0, reader-drain 23/0/0; буфер неизменён.
