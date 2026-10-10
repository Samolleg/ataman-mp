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

## Передача 134

**EI-FINALIZE-134, 2026-10-10: передача завершена, постоянная установка
обновлена 132→134.** Версия 133 (RES reader drain 50eba3aa и исправление
скорости 209af800) установлена в составе 134, отдельно не устанавливалась.
Ручная визуальная/игровая приёмка остаётся владельцу.

### База и свежая сборка

- Поручение владельца через сопровождающего Claude от 2026-10-10; main,
  исходный HEAD `6497562675cde4289f4c96b21e4a4b23e4cd59cf`, VERSION=134,
  чистый рабочий каталог. AGENTS.md проверен: SHA256
  `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3`;
  применён текущий docs/TEST_BUILD_HANDOFF.md.
- Свежий git archive распакован в `build-release-134-final/src`.
  Архивные окончания строк приведены к точным байтам Git blobs; все 1811
  файлов совпали до сборки и после lrelease/build, лишних/отсутствующих 0.
  Штатный build_release.cmd/build_release.ps1, Qt 5.12.12/MinGW 7.3 x64,
  shadow build `build-release-134-final/release`, EXE
  `release/release/EIMapperEndurance.exe`; обе runtime-компоненты собраны x86.
  Toolchain только читался; Endurance не изменялся.
- Первый внешний PowerShell-вызов с ErrorActionPreference=Stop прервался
  на диагностике Qt private headers в stderr и не дал надёжного receipt:
  это не PASS оболочечного вызова. Дочерняя сборка создала свежие EXE/objects.
  Для подтверждения завершения повторён штатный wrapper с независимым
  захватом stdout/stderr/exit через subprocess: exit 0, 18,125 с;
  использованы свежие objects первого вызова, runtime-компоненты пересобраны.
  [Receipt](../../build-tests-134-final/release-receipt.json),
  [stdout](../../build-tests-134-final/release-receipt.out),
  [stderr](../../build-tests-134-final/release-receipt.err).
- Свежий и staged EXE совпали; оба `--version` вернули
  `EI Mapper Endurance Edition 134`, exit 0. EXE SHA256:
  `EA881BD6647F5FD792F7E41197A4EC49332321F5047C9E665FE8BB897A149B56`.

### Обязательный регресс и системный буфер

Весь регресс выполнен штатным `tests/clipboard_isolation_regression.ps1`
с AppBuild=`build-release-134-final/release`, TestBuild=`build-tests-134-final`.
PrepareReader создал изолированный entry point исходного reader-набора;
он связан с точными production objects134 без main.o. Clipboard-набор собран
из git-снимка134. Прогоны и benchmark выполнялись под атомарным mkdir
`build-locks/ui-tests.lock`, owner.txt, finally-release; lock освобождён.
Редактор интерактивно не открывался. Необязательный native guard-набор
не запускался; три запрещающие child-пробы входят в обязательный набор.

| Проверка | Результат |
|---|---|
| Полный --ui-widget-check | 169 функциональных PASS + PASS isolation; 0 FAIL, exit 0 |
| --scenario-layer-check | 50 PASS / 0 FAIL + PASS isolation, exit 0 |
| --dark-theme-check | 70 PASS / 0 FAIL + PASS isolation, exit 0 |
| --model-gallery-check | PASS + PASS isolation, exit 0; 497 моделей/7165 текстур, open 6 мс/select 2 мс/close 0 мс |
| verified_clipboard_tests, default | 11 PASS / 0 FAIL / 0 SKIP, 266 мс, exit 0 |
| tests/res_reader_drain_tests | 23 PASS / 0 FAIL / 0 SKIP, 12031 мс, exit 0 |
| Отрицательные clipboard child-пробы | 3/3 ожидаемых exit 2; обычный backend child PASS |
| Внешний harness, весь прогон | sequence 33600→33600; format IDs 1,7,13,16,49383,49770,49794,49795 неизменны |

В положительных процессах nativeAttempts=0, hookedImports=14 (app/reader)
и 15 (clipboard). Метаданные снимаются без OpenClipboard и чтения payload.
Reader: собственный reader 185 мс, 1 commit/1 checkpoint; 4/4 thumbnails +
1/1 hydration, drain 180 мс; новые 5/5 consumers дождались публикации и
прочитали новые bytes. Admission timeout 10008 мс — ожидаемое исключение
проверяемого сценария, повтор provider успешен; это не таймаут harness.

Первый регресс **не прошёл**: clipboard 10 PASS / 1 FAIL / 0 SKIP,
проба `--probe-system-qpa` exit 0 вместо 2. Test driver задавал внешние
QT_PLUGIN_PATH/QT_QPA_PLATFORM_PLUGIN_PATH на toolchain, хотя windeployqt
развернул собственный qwindows.dll, который preloader выбирает рядом с
Qt5Core. После удаления обоих внешних переопределений из окружения child
тот же неизменённый EXE дал ожидаемые три exit 2 и полный PASS выше.
Наблюдение указывает на несовпадение выбранной QPA-копии и preloaded plugin;
поддержка сторонних переопределений QPA этим прогоном не доказана.
Исходный FAIL сохранён, не переименован в PASS; его sequence тоже 33600→33600.
[Первый FAIL](../../build-tests-134-final/clipboard-first-failed.txt),
[полный успешный регресс](../../build-tests-134-final/regression-packaged-qpa.out),
[harness receipt](../../build-tests-134-final/harness-receipt.txt).
Все stdout/stderr и QtTest results находятся в `build-tests-134-final`.

### Скорость 132 против 134

Повторены средства `build-tests-133-final`/`build-tests-133-perf`:
тот же real-benchmark.cpp, связанный отдельно с точными Release objects132
и134 без main.o, профиль DejaVu/Base, private fixture129, unmo36/stbu71,
восемь видимых плиток. EXE исходной сборки132 побайтно совпадает с
установленной132: `DF44E7E6BD840FAD308525A0C1E08B367805846A8F049086CC9E9CEC6A1FEA06`.
Версии чередовались между пятью повторами. Cold — новая app-папка без
производного disk/memory thumbnail cache; disk-warm — второй процесс той
же папки; repeat — повторный показ в памяти процесса. OS file cache и
пользовательские кэши не очищались. Все 20 процессов exit 0, все 80 samples
complete=1/visible=8/ready=8/unavailable=0; cold calls=8, остальные calls=0.

| Fill, мс; n=5 | 132 min / median / max | 134 min / median / max | Δ median мс / % |
|---|---:|---:|---:|
| unmo cold | 72,124 / 74,448 / 76,669 | 80,666 / 84,710 / 88,728 | +10,262 / +13,78% |
| unmo disk-warm | 27,950 / 28,611 / 29,213 | 27,561 / 28,154 / 28,869 | -0,457 / -1,60% |
| unmo repeat | 4,877 / 4,980 / 5,611 | 4,927 / 4,973 / 5,868 | -0,007 / -0,14% |
| unmo repeat-after-disk | 4,829 / 4,961 / 5,347 | 4,850 / 5,094 / 5,321 | +0,133 / +2,68% |
| stbu cold | 28,720 / 29,354 / 33,866 | 28,536 / 29,383 / 30,821 | +0,029 / +0,10% |
| stbu disk-warm | 9,693 / 10,144 / 10,443 | 9,379 / 9,912 / 9,950 | -0,232 / -2,29% |
| stbu repeat | 5,450 / 5,813 / 7,417 | 5,403 / 5,608 / 7,304 | -0,205 / -3,53% |
| stbu repeat-after-disk | 5,471 / 5,600 / 6,243 | 5,537 / 5,711 / 6,044 | +0,111 / +1,98% |

Порог холодного заполнения по медианам «≤+10% **или** ≤+15 мс» выполнен:
unmo проходит абсолютную ветвь +10,262≤15 мс, stbu — обе ветви.
[80 samples](../../build-tests-134-final/benchmark-raw.json),
[min/median/max и порог](../../build-tests-134-final/benchmark-summary.json).

### Состав и установка

- Создан ранее несуществовавший `build-staging-134-final`; базовые assets/Qt
  взяты из проверенных 127/127 manifest-файлов staging131, EXE и два runtime
  заменены свежими, документы — из снимка134; windeployqt --compiler-runtime.
- Исправлены `packaging/finalize_package.ps1` (автоматически добавляет
  docs/TRAVEL_EDITOR.md и нормализует тексты копии пакета в UTF-8 без BOM,
  CRLF) и `packaging/validate_distribution.ps1` (руководство обязательно).
  В новом staging руководство намеренно отсутствовало до finalizer — он
  успешно восстановил его из исходника. Finalizer запущен только на этом
  новом staging: exit 0, внутренняя проверка PASS, удалено 0 файлов.
  После finalizer пакет не запускался и не менялся.
- Manifest: 127 payloads, отсутствующих/лишних 0. В сравнении с
  `build-staging-131-stream-handoff` отличаются ровно 6 payloads: EXE,
  два заново собранных runtime (включая build identity), VERSION134,
  Readme134 и единственный текущий CHANGELOG с ручными шагами.
  SHA256SUMS.txt закономерно новый. Остальные 121 payload побайтно совпали.
  LICENSE, NOTICE, heightmaps/README и TRAVEL_EDITOR совпали с CRLF130/131;
  SHA256 руководства `945D3E7B5905944D8735D07E518063DBC04BCDA15887CFD5B119F463EF1ACC83`.
  Manifest SHA256 `1FD79984B7A79447A308FD947193094D6D3426AB402A667FEC2E9BE380B90945`.
  [Package receipt](../../build-tests-134-final/package-receipt.json).
- Постоянная установка `artifacts/EI-Mapper-Ataman-2-first-test`:
  исходные 126/126 managed hashes132 совпали, коллизий с user-файлами 0;
  host-level проверка процессов перед backup и непосредственно перед записью
  не обнаружила запущенных компонентов установки. Backup10 файлов
  (9 заменяемых + старый manifest) в `artifacts/rollback-132-to-134-final`
  проверен по SHA256 до первой замены. Файлы заменены атомарно, manifest
  последним; добавлен TRAVEL_EDITOR, удалено 0. Установленные 127/127 hashes
  и manifest совпали со staging, VERSION134. Все 23062 пользовательских
  файлов/кэшей вне manifest побайтно сохранены; все 5 прежних rollback
  сохранены. Finalizer на установке не запускался.
  [Install receipt](../../build-tests-134-final/install-receipt.json),
  [backup receipt](../../build-tests-134-final/backup-receipt.json).

Scoped-набор коммита: этот отчёт, три относящихся статуса ROADMAP.md и два
скрипта упаковки. VERSION остаётся134; исходный код программы не менялся.
Технические scripts/logs/binaries/private fixtures не включаются в Git.
Коммит в main с process-local GIT_AUTHOR_NAME/GIT_COMMITTER_NAME=Codex,
GIT_AUTHOR_EMAIL/GIT_COMMITTER_EMAIL=codex@localhost, trailer
`Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
Сеть, push, установка зависимостей, глобальный git config, игра, мод,
живые карты и Endurance не изменялись. ZIP не требовался: это обычная
передача в постоянную локальную установку. Ручная приёмка не заявляется.
