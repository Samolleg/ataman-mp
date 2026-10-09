# EI-PUBLICATION-RES-COMMIT-77-AUDIT — 2026-10-10

## Результат

Подтверждён **механизм отказа**: открытый `QFile(ReadOnly)` другого потока мешает атомарной замене RES используемым Qt 5.12.12 на Windows. На синтетическом RES production `commitMapSaveBatch()` отказывает с `Qt file error 10` (`RenameError`), сохраняет исходные байты и backup; после закрытия читателя повтор проходит. Это воспроизведено с одним и четырьмя одновременно удерживаемыми QFile. Аналогичный отказ и успешный повтор получены в `commitStagedResPublishedMembers()`.

**Причина конкретного отказа zone18 от 2026-09-28 не доказана.** Версия77 объединила ошибку чтения/сравнения исходника и ошибку commit в одном сообщении. Ни системного кода, ни владельца дескриптора в историческом журнале нет. Наиболее сильная гипотеза — два собственных фоновых потока deferred reviewed-HD hydration, ещё работающие при публикации. Найден конкретный участок, который держит RES открытым дольше чтения: `CTerrainAtlasArchiveBindingResolver::findExactArchive()` сохраняет QFile при CPU-декодировании всех слотов и подсчёте pixel SHA-256.

Provider128 увеличил возможное число перекрывающихся чтений на холодном кэше до четырёх, но не создал постоянного общего дескриптора. Он появился позже отказа77 и не может объяснить тот исторический случай. Рост частоты отказов после128 не измерен; это риск по коду и воспроизведённой семантике Windows, а не доказанная статистическая регрессия.

Код программы, VERSION, общие документы и установка не изменялись. Исправление предложено ниже, без применения.

## База, правила и границы

- Checkout: `C:/GOG Games/maped/ei-mapper-ataman`, ветка `main`.
- HEAD на старте (база аудита): `33d5c29f6cbca25483dcc589a0efa43ad3db42a7`; VERSION128. Перед передачей другой участник продвинул main до `ad60be231e6fc38ca7db4324de2cc615f363c5bd`; это не коммит данной задачи. Проверенные production исходники harness после этого остались неизменны (`completion-receipt.json`).
- AGENTS.md прочитан с UTF-8; SHA-256 `F4B7FDD5EA82FCDF43D002D23D64EC36BE853650F054EDF70376527CD6432EC3` совпал при заключительной проверке. Прочитаны CONTRIBUTING.md, PROJECT_CONTRACT.md, два назначенных пункта ROADMAP и связанные отчёты78/128. Свежие ограничения поручения приоритетны: только аудит, без Git-записей, сети, установки и UI.
- Собственные записи: этот новый отчёт и `build-tests-res77-audit/`. Ветки/worktree/подагенты не создавались; Git использовался только для чтения. ROADMAP/CHANGELOG/VERSION/Readme/translations не редактировались.
- Навыки evil-islands-modding/private-engine-reference отсутствуют. Использован обычный код Ataman, синтетические данные и разрешённое чтение логов/атрибутов; приватный движок не исследовался.
- Параллельные изменения не включены в результат. В ходе аудита рабочий `model_gallery.cpp` изменился другим участником. Оценка provider128 опирается на **git show указанного стартового HEAD** и родителя коммита128; снимки сохранены отдельно. Остальные проверенные исходники, включая все пять production cpp, скомпилированных в harness, соответствовали стартовому HEAD с учётом CRLF/LF. 25 файлов снабжены отпечатками в `source-receipt.json`.
- Harness использует существующий Qt/MinGW из `.tooling` соседнего Endurance только как toolchain; туда ничего не записывалось. Все выходы qmake/make, файлы RES, backup и recovery journal находятся в собственном каталоге аудита.

## Исторические доказательства77/78

Источник: `artifacts/EI-Mapper-Ataman-2-first-test/logs/workflow-20260928-133923.log`. Нужные21 строки с исходными номерами сохранены в [historical-workflow-excerpt.txt](../../build-tests-res77-audit/historical-workflow-excerpt.txt). Время журнала местное, UTC+5; имена backup используют UTC.

| Время / строка | Наблюдение | Значение |
|---|---|---|
|12:09:34 /18820|Успешная публикация zone18|Путь вообще работал; это не постоянная невозможность записи|
|13:02:11–15 /22199–22202|Backup zone18, zone19, HDPack и texts.res в первом batch|Отдельный успешный этап мирового сохранения предшествует следующей публикации|
|13:02:16–18 /22208,22260|Обновление resource lists; hydration73 bindings,2 threads|После первого этапа запущены собственные новые читатели|
|13:02:18 /22264,22266|Чтение semantics zone19; вход в публикацию zone18|Активность RES и публикация перекрываются|
|13:02:25 /22321,22331,22347|Публикация authorized; чтение соседней zone19 и принятие сцены|Не только queued: работа со связанными ресурсами продолжается|
|13:02:31 /22428|Backup HDPack368418137 байт, hash e60cbfdf…|Второй batch дошёл до backup RES; постоянная невозможность открыть/стейджить файл до этой точки маловероятна|
|13:02:34 /22429|Backup zone18.mpr4251868 байт, hash8226aa92…|Подготовлены оба участника; ошибка относится к более позднему сравнению/commit|
|13:02:35–37 /22442,22444|Hydration complete|Читатели не были заранее осушены к началу операции; точного момента закрытия конкретного handle журнал не содержит|

Строки успеха второй публикации нет. Старый ROADMAP фиксирует снимок пользователя: `Publication did not complete`, phase `map-cohort`, `Save failed or source changed` для HDPack. `git show af5343fab5e7b7f2ccd2e31e1827c04d338c90c7:source/documents/map_save_batch.cpp` подтверждает объединённую ветку отказа строк84–88. Снимок этого кода сохранён в `historical-map-save-batch-77.cpp.txt`.

По историческому аудиту в ROADMAP предыдущая пара совпала с backup: HDPack SHA-256 `E60CBFDF11374DC98B1510328CA6152206A76231D2BB5051CE29EB868A4EB7D3`, MPR `8226AA923D1D675AFCC413EE93A9D3BB3C3169AE0B7A520B0D00F5E307AAA0FA`. Это **историческое свидетельство**, а не заявление о сегодняшних хешах живой установки; повторное хеширование нынешних игровых файлов не восстанавливает причину старого отказа и не выполнялось.

[Отчёт78](ATAMAN_SAVE_CHECKPOINT_78_20260928.md) и `build-tests-78-save/release/baseline.txt` подтверждают отдельный дефект checkpoint: первоначальный повтор `travelRetryAfterPublicationFailure` вернул `map.txt changed outside this editor` (2 PASS/1 FAIL/0 SKIP в исходном адресном запуске). В `release/results.txt` после исправления78:28 PASS/0 FAIL/0 SKIP, включая `batchReportsLockedDestination`. Эти числа прочитаны из сохранённых логов, **не являются новым прогоном**. Исправление78 синхронизирует точный baseline после успешного первого batch и разделяет compare/commit, но не устанавливает виновника первоначальной блокировки.

## Путь публикации и время жизни собственных файлов

Ссылки на исходники относятся к стартовому HEAD, кроме отдельно указанной исторической версии77.

1. `CView` строит atlas/package, выбирает configured HDPack и читает исходный RES (`view.cpp:15153–15171`). `sourceTextures.close()` выполнен до публикации. `CResArchiveDocument` хранит QByteArray, а не QFile. Строительство кандидата в QtConcurrent оперирует снимками памяти; worker не держит исходный файловый handle.
2. Семантика MPR/embedded profile проверяется до commit. После recovery snapshot вызывается `terrainPublicationPreflightRequested` (`view.cpp:15335`). `MainWindow` предлагает остановить только принадлежащий редактору Test Current, ждёт до8с и при незавершении отказывает (`main_window.cpp:1625–1682`). Внешние игры этот guard не закрывает.
3. Preflight вызывает `CResourceBrowser::beginInternalPublication()` (`resource_browser.cpp:820–830`): снимает QFileSystemWatcher, помечает internal publication. Он **не ждёт** уже начатый `beginLoad`/`makePreview`, не блокирует gallery/provider и hydration. `ExcludeUserInputEvents` у progress/event loops не останавливает фоновые потоки и queued callbacks.
4. Для `m_mapCohortActive` кандидат RES становится companion через `stageMapCompanion`; текущий RES читается локальным QFile внутри блока, handle закрывается на выходе. Затем `commitMapCohortMpr()` добавляет MPR и вызывает `commitMapSaveBatch()` (`view_map_save.cpp:14–29`, `view.cpp:15431–15434,15498–15514`). `phase=no-op` здесь означает отложенную запись в cohort, а не отсутствие изменений.
5. `commitMapSaveBatch()` сначала проверяет точные before bytes, берёт `.mapper-save.lock` для каждого изменяемого пути, открывает `QSaveFile` рядом с целью с direct-write fallback=false, записывает/flush. Lock согласует cooperating writers, но фоновые readers его не берут. Файлы всех участников стейджатся до backup и commit.
6. Перед backup проводится новое сравнение; `CBackupManager::createSnapshot()` (`backup_manager.cpp:416–530`) хеширует исходник, переоткрывает его, копирует кусками4MiB, атомарно пишет backup и metadata. Его input живёт до возврата функции, затем закрывается RAII. QFile проверки backup также локален и закрывается до цикла commit. Эти дескрипторы не утекли через границу commit.
7. Создаётся recovery JSON. Перед **каждым** commit again читаются before bytes; затем `QSaveFile::commit()`, чтение/сравнение after. `matches()` возвращает один bool для mismatch, отказа open и ошибки read, поэтому два ранних сообщения всё ещё могут выдавать недоступный исходник за изменение. На фазе commit текущая версия78+ разделяет отказ чтения и `Atomic replacement failed / Qt file error / errorString`, но native Win32 code не хранит.
8. При отказе откатываются только уже committed участники и только если disk==after. После rollback требуется точный before. Неудачный rollback оставляет recovery journal, `Restore required`, backup paths. Успешный batch вызывает checkpoint до renderer/следующей публикации; отказ следующего этапа не отменяет уже принятую первую запись.
9. Вне cohort используется `commitStagedResPublishedMembers()` (`res_save_coordinator.cpp:214–427`): source/destination check → serialize → временный RES в destination directory → flush/close → fresh reopen и payload checks → pre-backup check → backup+hash → pre-commit check → QSaveFile → точная post-commit проверка и reopen. Все его исходные QFile локальны; stage закрыт до reopen. Последующий MPR отказ вызывает compare-and-restore RES/MPR в `view.cpp:15457–15515`. Простая замена одного member использует аналогичный coordinator; complete-bytes save и Resource Browser transaction имеют собственные входы и также QSaveFile.

## Инвентаризация читателей RES

| Участок / функции | Что хранится постоянно | Когда открыт RES / риск |
|---|---|---|
|`res_archive_index.{h,cpp}`: `open`, `sourceIsCurrent`, `readRange`|Path, size,mtime, первые4096 bytes fingerprint, таблица/имена; **QFile-члена нет**|Локальный QFile на index/header/member read. `readRange` сначала закрывает QFile проверки current, потом открывает другой для payload; между проверкой и чтением существует окно замены. Retained index сам не блокирует commit|
|`resource_catalog.cpp::build`, provider chains|Копии `CResArchiveIndex`, metadata|Индексация последовательно открывает/закрывает архивы; вызов может идти в фоне|
|`resourcemanager.cpp::archiveIndex/readIndexedResource`, `CObjectList::loadFigures/animationArchive/resourceData`, `CTextureList`|Таблицы entry, QByteArray, декодированные QImage/GL texture, negative/cache записи|QFile только при index/read; обычное resource decode уже после возврата read. `catalogTerrainMmpTile` (`1725–1760`) держит QFile во время bounded DXT tile decode. Общего постоянного RES handle не обнаружено|
|`terrain_atlas_archive_binding.cpp::archiveIndex/readArchiveMembers`|Описание member/байты; bindings и tile caches в CView содержат значения|Index/file-local чтение; `readArchiveMembers` держит handle на весь список requested pages|
|`findExactArchive` (`401–454`) и `findEmbeddedProfile` (`313–350`)|Результат содержит пути/metadata/profile/tiles|**Длинное окно:** QFile в `findExactArchive` открыт при чтении всех pages, decode всех slots, convert и pixel hash. В `findEmbeddedProfile` внешний QFile с profile не закрыт перед вложенным `findExactArchive`; возможны два handles на тот же RES. В историческом77 эта структура также присутствует|
|`CView::hydrateTerrainGlobalBinding/startDeferredGlobalTerrainPaletteHydration`, family prewarm|Копии task, binding и tiles; отдельный pool2|Вызывает resolver в background. Generation подавляет устаревший delivery, но не прекращает выполняющийся file read. `clear()/waitForDone()` есть в destructor, не в publication preflight|
|`model_gallery.cpp::resourceThumbnails`128|QSharedPointer PreviewSources, immutable catalog, QCache scenes/textures под mutex|После index/cache lookup mutex освобождается **перед** readMember. До4 workers могут открыть отдельные QFile. Handle не живёт при CPU raster/декодировании после readMember; закрытие диалога disconnect delivery, захваченный provider продолжает работу до окончания задания|
|`resource_browser.cpp::beginLoad/showPreview/makePreview`|Metadata каталога, value copies, watchers|QtConcurrent index/preview; index/readMember локальны. beginInternalPublication снимает watcher, но не выполняющийся future. Model preview parsers сами принимают QByteArray/scene, не держат RES. Audio играет извлечённый временный member, не архив|
|`assembly_inventory.cpp::archiveIndex/readIndexedResource`, `profile_database.cpp`, `game_text_catalog.cpp`, `map_weapon_catalog.cpp`, `resource_profile.cpp`, database provider support|Entry/data/снимки либо catalogs|Index/member/full-file/hash через локальные QFile. Длительность полного RES/hash зависит от размера; постоянного RES QFile-члена не найдено. Map catalog сканирует каталог карт, не удерживает HDPack постоянно|
|`res_file.cpp::CResFile(QString)`, landscape MPR load|Полные byte buffers и parsed maps|Legacy CResFile **явно закрывает file до parse** (`282`). Landscape читает MPR, далее ресурсы через CTextureList; это другой путь, не постоянный HDPack handle|
|`CResArchiveTransaction::writeCandidate`|Индекс+changes|Локальный source закрыт строк376 до QSaveFile публикации. Сохраняет собственное правило не заменять открытый исходник Windows|

Поиск QFile/CreateFile/fopen/ifstream по обычному коду выявил постоянные QFile для логов/settings/clipboard/recent files, но не постоянный RES handle. Это аудит исходников, а не полная live-инвентаризация всех native handles процесса. Отсутствие утечки не исключает нормальный временный читатель во время commit.

## Ошибки Windows и сообщения пользователю

Коды ниже — **возможные**, кроме явно измеренных32/33/5. Имена/числа сверены с локальным `mingw730_64/x86_64-w64-mingw32/include/winerror.h`. Нельзя преобразовывать `Qt file error 10` в `Win32 10`: это другая enum.

| Этап | Возможные причины / Win32 | Что возвращает код / видит пользователь |
|---|---|---|
|Чтение исходного RES, source/destination comparison|2 FILE_NOT_FOUND,3 PATH_NOT_FOUND,5 ACCESS_DENIED (ACL/filter),32 SHARING_VIOLATION (exclusive holder),33 LOCK_VIOLATION (byte-range),23 CRC,1117 IO_DEVICE|Обычный coordinator errorString/source-check. В batch `File changed outside the editor`, `File changed while staging`, `Source changed or could not be read before commit`; первые два не различают unreadable и mismatch. Исходное чтение CView может показать безопасное чтение refused с пустым publishErrors, поскольку QFile errorString там не добавляется|
|Serialize/loadBytes/semantic reopen|Повреждённая структура, непредставимая операция, memory/resource failure; Windows-код может отсутствовать|`Atlas publication refused` + validation report; до записи целевого RES. Это не доказательство файловой блокировки|
|Временный кандидат / QSaveFile open/write/flush|5 ACL/read-only/filter,19 WRITE_PROTECT,39 HANDLE_DISK_FULL,112 DISK_FULL,3 исчезнувший каталог,206 длинное имя;32 при блокировке temp,23/1117 I/O|`Cannot stage: path + errorString` у batch; `Could not write the staged RES file` у coordinator; read-only target отказал ещё на staging в синтетике|
|Повторное чтение staged file|2/3 (temp исчез),5/32/33,23/1117; truncate/подмена bytes|Fresh reopen validation/refusal; target ещё не заменён|
|`.mapper-save.lock`|Другой writer, недоступный lock file/каталог|`Another save is using: path`; QLockFile причины отдельно не раскрыты. Это не lock любого RES reader|
|Backup/metadata/recovery journal|Ошибки исходного чтения;5/19/39/112/3 при записи backup;32/5 при его публикации|`Cannot read/create/write/publish backup`, backup verification failed либо `Cannot create save recovery journal`. Batch не начинает commit без готового journal|
|Атомарная замена|Открытый reader, memory mapping/1224 USER_MAPPED_FILE,5 ACL/read-only/filter или rename incompatibility,32 sharing;2/3 после удаления/rename путей;1117 I/O|В77 `Save failed or source changed: path`; при cohort CView оборачивает в `Publication did not complete`, `Phase: map-cohort`, статусы rollback. В78+ `Atomic replacement failed: path / Qt file error 10: errorString / Backups`. Вне cohort `Phase: atomic-replacement`, errorString. **Win32 5 не доказывает ACL:** MoveFileEx на открытом файле здесь дал5|
|Post-commit verification / reopen|Новая внешняя запись, удаление, read denial32/5/33,23/1117|Batch запускает сравнение/rollback, затем возможно `Restore required` и recovery path. Published-members coordinator возвращает `post-commit-validation`, но RES **уже мог быть заменён**. Текст CView «The selected archive was not replaced» для всех ошибок этого coordinator слишком категоричен; улучшить диагностику отдельно|
|Откат|Те же rename/read errors; disk не равен нашим after, поэтому compare-and-restore отказан|`manual recovery required`, backup paths, при batch recovery journal. Пользовательские внешние изменения не должны стираться ради успешного rollback|

17 NOT_SAME_DEVICE для самой замены здесь не ожидается: QSaveFile создаёт temp рядом с destination. Backup на другом томе копируется отдельно. Таблица не утверждает, что все коды были получены в zone18; disk full/ACL/filter/mapping здесь намеренно не создавались в живой установке.

## Синтетические проверки — свежий запуск

Harness: [res77_audit.cpp](../../build-tests-res77-audit/res77_audit.cpp), [res77_audit.pro](../../build-tests-res77-audit/res77_audit.pro). Обычный console `QCoreApplication`, без QWidget, окна, clipboard, qttest/LOCALAPPDATA и общего gallery cache. UI lock не требовался. RES создаётся самим harness (один member); игровые файлы для опыта не копировались. Production реализации map batch/coordinator/archive document/index/backup скомпилированы без изменений. Только `ei::log` заменён no-op, чтобы не создавать общий журнал.

Toolchain: Qt5.12.12 x64 MinGW7.3.0, существующий поддерживаемый kit. Сборка успешна; локальные предупреждения style не считаются тестовыми PASS. Повторная сборка выполнена только после добавления адресных проверок.

| Проверка | Результат |
|---|---|
|Batch без reader|Commit, backup1, точный after, callback1|
|QFile reader в другом std::thread,1 и4 handles, готовность через promise|Оба commit отказали; Qt error10; точный before сохранён; rollbackComplete; после join оба повтора успешны|
|Native reader share READ+WRITE (3)|Batch отказал; прямой ReplaceFileW отказал с32|
|Native reader share READ+WRITE+DELETE (7)|**Исходное ожидание успешного QSaveFile не подтвердилось: FAIL.** Batch отказал сQt10, before сохранён. ReplaceFileW на отдельном файле успешно. Не переписано в PASS|
|Native exclusive reader share0|Batch отказал до staging как `File changed outside the editor`; показывает потерю причины open|
|Read-only attribute на target|Отказ staging `Existing file … is not writable`, before сохранён; attribute возвращён на собственной копии|
|Реальная внешняя смена bytes|Отказ сравнения; внешние bytes сохранены|
|Backup root упирается в обычный файл|Отказ backup до commit; before сохранён|
|Второй участник удерживается QFile|Первый committed участник точно восстановлен, второй unchanged, callback0|
|Retained CResArchiveIndex при commit|Индекс жив, commit успешен; старый index отказал `sourceIsCurrent`. Значит index object не владеет постоянным handle|
|4 потока ×32 readMember|128/128 точных payload, без записи и гонки замены в этом подопыте|
|Published-members coordinator с QFile holder|Refusal phase atomic-replacement; before сохранён; после close повтор phase complete|
|Адресное исследование share7: QSaveFile отдельно; MoveFileExW share3 и7; DELETE access при QFile|QSaveFile снова отказал Qt10; **MoveFileExW оба раза Win32 5**, before сохранён; открытие DELETE access при QFile отказало32. Это объясняет, почему share-delete/ReplaceFile нельзя переносить на QSaveFile без проверки backend|
|Byte-range exclusive LockFileEx|Прямой ReadFile отказал33; production batch выдал `File changed outside the editor`; before сохранён после unlock|

Числа: основной запуск **15 PASS /1 FAIL /0 SKIP** (`results.json`, `run.log`, процесс вернул1); исследование неожиданного отказа **4 PASS /0 FAIL /0 SKIP** (`followup-results.json`, `followup-run.log`, exit0); byte-range **1 PASS /0 FAIL /0 SKIP** (`byte-lock-results.json`, `byte-lock-run.log`, exit0). Всего **20 PASS /1 FAIL /0 SKIP,21 утверждение**, дополнительно128 точных payload reads. Общий безусловный PASS не заявляется: один FAIL — опровергнутое предположение о Windows API, сохранённое и разрешённое дополнительным исследованием. Коды Win32 записаны сразу после **прямого native вызова**, не извлечены из Qt cleanup через поздний GetLastError.

Нативное errorString имеет проблемы кодировки в этом console окружении; сырой текст сохранён в JSON/log, выводы опираются на Qt enum и отдельно снятые Win32 numbers. Исторический workflow имеет смешанные участки кодировки: первая попытка strict UTF-8 extraction завершилась UnicodeDecodeError; извлечение ASCII-маркеров исправлено на errors=replace и выполнено успешно. Эта служебная ошибка не засчитана как тестовый PASS.

## Ранжированные гипотезы и проверка каждой

1. **Собственные deferred hydration readers — главный кандидат.** Исторический журнал подтверждает2 потока в критическом интервале; код77 и текущий resolver держат handle при CPU decode/hash; синтетика подтверждает необходимую Windows-семантику. Не хватает совпадения конкретного открытого handle с точным commit. Проверка: лог begin/end каждой RES read lease (path,thread,task,generation,duration) и каждой commit attempt; детерминированный barrier в тесте production resolver после open, затем commit на копии. Ожидаемая проверка исправления — writer ждёт закрытия readers и затем проходит, а новые readers не стартуют до завершения.
2. **Внешний transient holder/filter: антивирус, индексатор, сторонний editor/game.** Механизм совместим с отказом, в старом журнале владельца handle нет. Сегодня WMI перечисляет Kaspersky/Defender, работают avp и SearchIndexer/SearchProtocolHost/SearchFilterHost; это наличие кандидатов, **не атрибуция события 2026-09-28**. Protected filter может вернуть5 даже без видимого пользовательского handle. Проверка при новом отказе: точный OS code на границе native I/O плюс PID/image/start-time holders через доступный Restart Manager/системную трассу; при необходимости согласованный ProcMon capture с path filter. AV не выключать, исключения автоматически не добавлять.
3. **Галерея128/Resource Browser/scanners — текущий дополнительный собственный источник.** Per-call QFile не исключает collision. Provider128: до4 jobs, release mutex до чтения, jobs переживают закрытие; browser preflight их не осушает. Нужна реальная выбранная модель/texture, чей winning provider — **именно заменяемый RES**. При обычных model archives и cache hit HDPack не открывается. Проверка: лог path/read overlap + холодная галерея/закрытие во время публикации копии, отдельный warm-cache сценарий. Это гипотеза текущего риска, не объяснение77.
4. **Source mismatch либо временный read denial перед commit.** Оба попадали в старую общую ветку; нынешние ранние matches также conflated. Точная старая пара после попытки и успешный backup не исключают краткую запись/недоступность между сравнением и commit. Проверка: разделить open/read/hash mismatch с actual/expected fingerprints, временем и native status, не отключая guard. Статическое отличие bytes подтверждено синтетически; историческая внешняя запись не наблюдалась.
5. **Права/attributes/disk/путь/OneDrive — слабее.** Успешные stage и оба backup перед историческим отказом понижают вероятность постоянного read-only/нехватки места на раннем этапе; изменения после этих шагов не исключены. Нынешний target и цепочка родителей `C:/GOG Games/PZ/MODS/DejaVu/res` без ReparsePoint, файл Archive; OneDrive в текущем process snapshot отсутствует. Поэтому обычный sync OneDrive этого пути — низкий приоритет, но состояние прошлого не восстановлено. Проверка: атрибуты/ACL/volume-free/фактический resolved path и status в момент отказа; mapping/filter диагностировать отдельно. Ничего из этого не ремонтировать автоматически.

## Оценка provider128

Сравнены `d3a0267a53c34118bedab2935802903d1acbf6af` (изменение128) и его родитель, а результат128 — со стартовым HEAD. В127 один thumbnail future на диалог, provider держит sources mutex через read/parse/raster; в128 separate pool qBound(2,idealThreadCount,4), несколько jobs на диалог, sources mutex только для index/cache. Защита cache/thread-safety не равна защите внешней атомарной публикации.

Изменение увеличивает **максимальное перекрытие** локальных RES reads и позволяет оставшимся jobs продолжить I/O после закрытия галереи. При этом не продлевает QFile через raster: `readRange` возвращает QByteArray и закрывает file до parse/decode; warm disk cache делает0 provider calls по отчёту128. Поэтому частота collisions зависит от hit-rate, реальных archive providers и момента операции. Утечка handle и количественный рост вероятности не доказаны. Модель независимых событий `1-(1-p)^N` здесь не измерение: cold tasks/caches связаны, длительность до4 parallel tasks меняется.

## Предложение исправления — не применено

1. **Сократить существующее длинное удержание.** В `source/render/terrain_atlas_archive_binding.cpp::findExactArchive` ограничить scope QFile только чтением pages и закрыть до декодирования/pixel hash; в `findEmbeddedProfile` закрыть внешний QFile после profileBytes, до вложенного resolver. Сохранить size/mtime/binding validation; закрытие само по себе не устранит короткое I/O collision.
2. **Согласовать чтение и публикацию одного архива.** Общая per-canonical-path read/write lease для реального file I/O: все читатели в `res_archive_index.cpp`, `resourcemanager.cpp`, `terrain_atlas_archive_binding.cpp`, `assembly_inventory.cpp` и прочих scanners должны брать read lease, writers — exclusive lease на проверку/замену/verification/rollback. Admission новых background reads приостанавливается до bounded drain уже начатых. Подключить `model_gallery.cpp`, `resource_browser.cpp`, `view.cpp` hydration/prewarm и `main_window.cpp` publication preflight. Не держать cache mutex во время ожидания, не ждать GUI delivery для закрытия handle, не вызывать собственный read-lock повторно из writer без явного решения reentrancy. Writer может выполняться в фоне с существующим progress; GUI не должен зависать на небounded wait. Generation cancellation без drain недостаточна. После commit/rollback обновить namespace/индексы и не принимать late result старого поколения.
3. **Диагностика конкретного шага.** В `source/documents/map_save_batch.cpp::matches/commitMapSaveBatch` вернуть отдельные статусы missing/open/read/mismatch и fingerprints, `error()`/`errorString()`. Аналогично `res_save_coordinator.cpp::readFile/destinationMatches` и source чтение `view.cpp`. Для настоящего native code нужен захват непосредственно в native boundary или проверенный adapter/трасса; поздний GetLastError после Qt не является доказательством. Логировать phase/path/run-id/timing и активные собственные leases; не просто предлагать пользователю «файл занят» при любом5.
4. **Bounded retry transient refusal** после осушения собственных readers: до повторной попытки вновь сверить before и актуальность snapshot, сохранить backup/recovery и rollback semantics78. Не повторять на реальном byte mismatch; ограничить ожидание и сообщить точно неразрешённую фазу. Новая staging после failed QSaveFile commit обязательна: не предполагать, что failed instance остаётся пригодным для reuse.
5. **Не подменять безопасную запись.** Не включать direct-write fallback, не переходить на delete+rename и не отключать антивирус ради PASS. Native ReplaceFile + FILE_SHARE_DELETE может быть отдельным вариантом после полного исследования metadata/ACL/backup/error/rollback контрактов, но **простое добавление FILE_SHARE_DELETE недостаточно для текущего поведения QSaveFile**, что подтверждено экспериментом.
6. **Точное состояние UI.** В `view.cpp` не говорить «archive was not replaced» при post-commit-validation failure; показывать committed-but-unverified и recovery options. Сохранить точные companion checkpoints и сообщение о частичном успехе78; не скрывать failures/reopen requirement.

Регресс для будущей реализации: active/queued2 hydration и4 thumbnail jobs; cold/warm; close-dialog с unfinished jobs; одновременный index/preview; read+writer drain; настоящий external bytes mismatch; exclusive/byte-range reader; первый/второй batch отказ, rollback first, rollback refusal при чужих bytes и retained journal; retry и new edits после checkpoint; late generation/index invalidation. Всё — на собственных копиях; UI-сценарии только с общей блокировкой. Это предлагаемый объём следующего исправления, не запущенные здесь проверки.

## Ограничения и передача

Аудит завершён в согласованном объёме. Исходная публикация zone18 интерактивно не повторялась; живой RES не заменялся. Реальный provider128 одновременно с publication не запускался; доказаны file semantics и кодовые окна, а не частота отказов/owner UI acceptance. OS trace/handle snapshot старого события отсутствуют, точная историческая причина остаётся открытой. Окна/clipboard/gallery cache не затронуты; программы/зависимости не устанавливались. Строгая проверка данных приватного движка не требовалась и не выполнялась.

**Точный список файлов для коммита этой задачи:**

- `docs/validation/ATAMAN_RES_COMMIT_77_AUDIT_20261010.md` — новый отчёт.

Каталог `build-tests-res77-audit/` — локальные доказательства/синтетика, **не для коммита**. Основные артефакты: `res77_audit.cpp`, `.pro`, `inspect.py`, `capture_evidence.py`, `source-receipt.json`, три JSON результатов и run logs, build logs, снимки77/127/128 и excerpt. Инвентаризация всех локальных файлов — `artifact-manifest.json`. Git add/commit не выполнялись; версия128 и постоянная установка сохранены.

### Текст для ROADMAP (вносит сопровождающий)

```markdown
## EI-PUBLICATION-RES-COMMIT-77-AUDIT — аудит отказа публикации RES
- Поручение 2026-10-10: только аудит, база main 33d5c29f6cbca25483dcc589a0efa43ad3db42a7, VERSION128; код/установку/данные не менять. Результат: путь публикации и readers исследованы; QFile reader другого потока синтетически блокирует QSaveFile, close+retry проходит. Главная историческая гипотеза — deferred hydration2, держащая RES при decode/hash; конкретный виновник77 не доказан. Provider128 добавляет до4 concurrent reads на cold cache, но не мог вызвать старое событие77.
- Проверки:21 утверждение,20 PASS/1 FAIL/0 SKIP; FAIL опроверг предположение share-delete => QSaveFile success, исследован отдельными native API probes;128/128 parallel payload reads. Аудит завершён, родительский отказ77 и ручная приёмка78 остаются открыты. Evidence: docs/validation/ATAMAN_RES_COMMIT_77_AUDIT_20261010.md. Следующий шаг: согласовать сокращение handle scope, общий per-archive reader/writer drain, точную I/O diagnostics и регресс collision/rollback/retry; здесь исправление не применялось.
```

### Текст для CHANGELOG (вносит сопровождающий, без повышения VERSION)

```markdown
- EI-PUBLICATION-RES-COMMIT-77-AUDIT (2026-10-10): исследован путь RES publication и readers. На синтетике подтверждён отказ QSaveFile при QFile reader другого потока и успешный повтор после close; выявлено удержание RES при atlas decode/hash. Причина конкретного zone18 отказа77 остаётся гипотезой. Оценён дополнительный concurrency risk provider128. Проверки20 PASS/1 FAIL/0 SKIP (FAIL — отвергнутое предположение о share-delete/native replacement API),128/128 exact parallel reads. Только отчёт; код, VERSION128 и установка не изменены. Evidence: docs/validation/ATAMAN_RES_COMMIT_77_AUDIT_20261010.md.
```
