# EI-TEST-TAILS-20261011 — политика save и изоляция тестовых backup

Дата: 2026-10-11. Поручение владельца через сопровождающего Claude.
Исходная база и проверяемый снимок: main
`b7f7bd8d67bde3075ac350688f1edf5049272e48`, VERSION снимка 138.
Номер следующей версии и коммит назначает сопровождающий; Git использован
только для чтения. ROADMAP/CHANGELOG/VERSION/Readme/translations не изменялись.
Постоянная установка, живые карты/моды/игра, Base и Endurance не изменялись.
Сеть, установка зависимостей, подагенты, интерактивный редактор не использовались.
Приватные engine-reference skills отсутствуют; исследован только обычный код
проекта и распространяемая синтетическая fixture.

## История FAIL799 и принятое поведение

- `git blame -L 780,815 tests/res_archive_document_tests.cpp` относит прежние
  assertions 799/800 к `4b7c9233` (Ataman51, 2026-09-22): оба save маршрута
  ожидали `PublishModernAtlas` для persistedWorkingDraft.
- `git log/blame source/core/terrain_save_flow_policy.h` и `git show 915fa959`
  показывают намеренное изменение в
  `915fa959aba278ef291f2dd5b062085390c90d81` (Ataman122, 2026-10-07):
  `persistedWorkingDraft` заменён на `(persistedWorkingDraft && saveAs)`.
- [Отчёт122](ATAMAN_TERRAIN_HEIGHT_RES_SAVE_FIX_122_20261007.md), раздел
  «Исправление», прямо задаёт обычный save без перепубликации RES и сохранение
  публикации для Save As. Там же зафиксированы production height-only/mixed/
  reopen/Undo/rollback проверки 29/0 и передача принятой122.
- [Отчёт reader-drain](ATAMAN_RES_READER_DRAIN_20261010.md) и существующий
  пункт `EI-PUBLICATION-RES-READER-DRAIN-133` ROADMAP фиксируют старое ожидание
  799 как отдельный хвост, воспроизводящийся на принятой базе.

Причина подтверждена: тест51 не обновлён при принятии политики122.
Исправлена только проверка in-place save: теперь явно переданы
`persistedWorkingDraft=true, saveAs=false`, ожидается `PlainMpr`.
Соседний assertion Save As продолжает ожидать `PublishModernAtlas`.
Политика и production save flow не менялись.

## Изоляция

Минимальная точка изменения production — `CBackupManager::defaultRootPath()`:

1. Явный `EI_MAPPER_TEST_BACKUP_ROOT` задаёт абсолютный корень. Относительное
   значение вызывает явную ошибку (`qFatal`), без fallback в каталог владельца.
2. Без override Qt test mode и `--*-check[=value]` используют один уникальный
   `QTemporaryDir` процесса (`ei-mapper-test-backups-XXXXXX/backups`). Это
   защищает встроенный height/save check и прочие save checks без правки main.cpp.
3. Обычный запуск без тестовых признаков сохраняет прежнюю формулу
   `GenericDataLocation/EI Mapper Endurance/backups` побуквенно.
   Явный аргумент конструктора `rootOverride` по-прежнему имеет приоритет.

`tests/save_test_backup_root.pri` подключает общий initializer. Он до main
создаёт отдельный `ei-mapper-native-save-test-XXXXXX/backups` и устанавливает
override для всего процесса, включая QTEST_APPLESS_MAIN и вложенные coordinators.
Унаследованный env override заменяется этим собственным каталогом.
Он подключён ко всем пяти standalone `.pro`, компилирующим backup_manager.cpp,
и к reader-drain, связанному с production objects.

Три новых backup-manager теста проверяют реальные default open/pre-save/autosave
snapshots и explicit constructor precedence, прежний production path без записей,
и частный временный root Qt test mode вместо общего qttest root.
Его entry point заменён на QTEST_GUILESS_MAIN: набор не использует widgets и
не требует создания QApplication/platform windows.

В profile_database_tests.pro устранены отсутствовавшие link dependencies
verified_clipboard/window_geometry. Этот набор содержит UI copy action: его main
использует существующий TestClipboardSession до QApplication и скрывает widgets
на Polish. Это необходимо для полного прогона без системного буфера и видимых
тестовых окон; production clipboard/geometry implementation не изменялась.

## Проверки и доказательства

Статус: **DONE — исправления и согласованные проверки завершены**.
Коммит и назначение версии остаются сопровождающему по текущему поручению.

| Уже проверено | PASS / FAIL / SKIP | Evidence |
|---|---:|---|
| Backup manager, полный набор | 12 / 0 / 0 | [tests.txt](../../build-work-testtails/backup_manager_tests-final/tests.txt) |
| Endurance documents, полный набор | 192 / 0 / 18 | [tests.txt](../../build-work-testtails/endurance_document_tests-final/tests.txt) |
| Minimap export, полный набор | 18 / 0 / 0 | [tests.txt](../../build-work-testtails/minimap_export_tests-final/tests.txt) |
| Исходный FAIL799 + Save As/rollback в том же файловом case | 3 / 0 / 0 | [tests.txt](../../build-work-testtails/res-policy799/tests.txt) |
| RES archive documents, полный набор (включает case799) | 28 / 0 / 0 | [tests.txt](../../build-work-testtails/res_archive_document_tests/tests.txt) |
| Profile/database documents, полный набор | 80 / 0 / 14 | [tests.txt](../../build-work-testtails/profile_database_tests/tests.txt) |
| Production save122, synthetic local/world height/save/reopen/rollback | 29 / 0 / 0 | [stdout](../../build-work-testtails/save122/stdout.txt) |
| Production reader-drain, полный набор | 23 / 0 / 0 | [tests.txt](../../build-res_reader_drain_tests-testtails/fixtures-resdrain/tests.txt) |

QtTest totals включают initTestCase/cleanupTestCase. Всего пять native targets:
**330 PASS / 0 FAIL / 32 SKIP**, без повторного счёта отдельного case799.
32 SKIP (18 document corpus +14 database) — внешние/private fixtures;
переменные для них не задавались, это не PASS. Save122 отдельно:29 условий
и один clipboard sentinel; не считать sentinel тридцатым save assertion.
Файловый case799 и три набора без окон проверены независимо от UI-lock;
полные RES/database UI suites и save122/reader-drain проверены под собственным
lock с owner.txt, после освобождения активной задачей138.
Database/save122: clipboard nativeAttempts=0, sequence36501→36501,7→7 форматов;
reader-drain: nativeAttempts=0, sequence36512→36512,8→8 форматов. Системный буфер
не открывался и не блокировался; clipboard checks использовали существующий
in-memory backend. RES widget cases использовали offscreen QPA.

Первый backup-manager запуск с QApplication не дал результатов и завершился
`0xC0000602`; не PASS. Набор переведён на подходящий ему QCoreApplication.
Первые document/minimap запуски завершились exit0, но QtTest не сохранил stdout;
для доказательства они один раз повторены с явным `-o tests.txt,txt`.
Пути Qt plugins/QPA теперь явно заданы в собственном runner. Эти ошибки подготовки
не скрыты и не заменены ослаблением assertions. Уже успешные неизменные
наборы повторно не запускаются при ожидании lock.
Сборки — только собственные `build-*-testtails/`.
Снимок Git HEAD распакован в `build-source-testtails/`, поверх него только
12 собственных файлов. Receipt с полным SHA и SHA256 каждого overlay:
[source-receipt.json](../../build-work-testtails/source-receipt.json).
Чужие рабочие изменения source/app/main.cpp, terrain/render/world и документы
не включены в проверяемый EXE. Raw qmake/make не перезаписывают translations.
Qt5.12.12/MinGW7.3 из сохранённого Endurance toolchain использован только для чтения.

Отдельный read-only probe связан с точными backup_manager.o/log.o/moc_log.o
этого production build. **12 assertions PASS**: прежний путь обычного режима,
частный root checks и Qt test mode, две разные process generations, вариант
`--terrain-mixed-edge-check=synthetic`, приоритет explicit env. Пользовательские
backup snapshots до/после probe совпали. Доказательство:
[root-probe-results.json](../../build-work-testtails/root-probe-results.json).
Ни редактор, ни игровой runtime для проверки обычного пути не запускались.

Для каждого прогона сохраняются отдельные `backup-before.json`,
`backup-after.json` и `receipt.json` с EXE hash, exit и временем. Промежуточные
снимки учитывают наличие root, полный список каталогов, relative paths, размеры,
mtime_ns. Полные снимки до/после всей серии дополнительно включают SHA256 каждого
файла в двух пользовательских backup roots LOCALAPPDATA (обычном и qttest).
Такой объём достаточен без многократного хеширования 42,5ГБ на каждом наборе.
UI/save checks выполняются последовательно под mkdir-lock
`build-locks/ui-tests.lock` с owner.txt; чужой lock не изменяется.

Итоговые полные снимки **совпали**: обычный root — **3688 файлов,
329 вложенных каталогов, 42 543 256 139 bytes**; qttest root — **0 файлов,
0 вложенных каталогов**, существование обоих roots сохранено. Нет новых,
удалённых или изменённых файлов/каталогов; размеры, mtime_ns и SHA256 каждого
файла совпали. Промежуточные снимки всех семи полных прогонов тоже совпали.

- [Полный before SHA256 snapshot](../../build-work-testtails/backup-before.json)
- [Полный after SHA256 snapshot](../../build-work-testtails/backup-after.json)
- [Итоговые receipts](../../build-work-testtails/verification-receipts.json)

Свежий Release `--version`: **EI Mapper Endurance Edition 138**, exit0;
EXE SHA256 `7541287cce8e80b7493e1ce5de9920f585a45b8b64bb1b4b391628cf301c3e55`.
[Версия](../../build-work-testtails/version/stdout.txt),
[receipt](../../build-work-testtails/version/receipt.json).
Собственный UI-lock освобождён в finally. Установка/упаковка не выполнялись;
это проверяемый raw build, не комплект для пользователя. Ручная/игровая приёмка
не заявляется. Обязательные проверки этой задачи доступны и выполнены;
32 внешних SKIP остаются явно отделёнными от PASS.

## Тексты для ROADMAP и CHANGELOG

Номер версии назначает сопровождающий; VERSION/CHANGELOG/ROADMAP текущего checkout
не редактировались этой задачей.

ROADMAP:

> EI-TEST-TAILS-20261011 — DONE. Устаревший FAIL799 приведён к сознательно принятой
> политике122: persisted draft save in place=PlainMpr, Save As=PublishModernAtlas;
> production policy сохранена. Native save/document targets получают отдельный
> временный backup root до main; встроенные --*-check и Qt test mode также
> изолированы, обычный запуск сохраняет прежний путь. Проверки:5 полных native
> наборов330/0/32, save12229/0, reader-drain23/0/0, root probe12 assertions PASS;
> 3688 пользовательских backup-файлов (42 543 256 139 bytes) и пустой qttest root
> неизменны по SHA256/size/mtime/составу. База b7f7bd8d67bde3075ac350688f1edf5049272e48;
> отчёт docs/validation/ATAMAN_TEST_TAILS_20261011.md. Следующий шаг — scoped-коммит
> и назначение версии сопровождающим; установка/push не разрешены этим поручением.

CHANGELOG:

> EI-TEST-TAILS-20261011: исправлено устаревшее ожидание сохранения рабочего
> черновика без перепубликации RES. Тестовые сохранения изолированы от
> пользовательских backup roots отдельными временными каталогами; обычный путь
> резервных копий сохранён. Полные native наборы330 PASS/0 FAIL/32 внешних SKIP,
> save12229/0, reader-drain23/0/0;3688 пользовательских backup-файлов неизменны.

## Файлы для scoped-коммита сопровождающего

- source/documents/backup_manager.cpp
- tests/save_test_backup_root.cpp
- tests/save_test_backup_root.pri
- tests/backup_manager_tests.cpp
- tests/backup_manager_tests.pro
- tests/endurance_document_tests.pro
- tests/minimap_export_tests.pro
- tests/profile_database_tests.cpp
- tests/profile_database_tests.pro
- tests/res_archive_document_tests.cpp
- tests/res_archive_document_tests.pro
- tests/res_reader_drain_tests.pro
- docs/validation/ATAMAN_TEST_TAILS_20261011.md

Build outputs, snapshots, logs, синтетические backup не коммитить.
