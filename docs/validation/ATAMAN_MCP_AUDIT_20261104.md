# Аудит MCP Ataman 277

## Основание и состояние

EI-MCP-AUDIT-20261104, 2026-11-04. Поручение владельца «Проведи аудит MCP маппера». Аудит завершён в согласованном объёме чтения кода, истории, документации и изолированных неинтерактивных проверок. Исправления предлагаются в конце отчёта.

Исходный корень `C:/GOG Games/maped/ei-mapper-ataman`, ветка `main`, HEAD `dcbb9c174e5f621ed054e076607398c5015e19d4`, VERSION 277. Установленная VERSION тоже 277. SHA256 установленных EIMapperEndurance.exe и EIMapperMcp.exe совпали с manifest и записью установки 277 в отчёте `ATAMAN_START_CAMERA_277_20261103.md`.

| Артефакт | SHA256 |
|---|---|
| Установленный редактор | `6df13e967cc86451e65311b1645b9ec636b29a8d358645a0bb0703dbca9b7a3f` |
| Установленный мост | `985c17f3c8604336467cc7e8fbe2d32d7da4704f0bf8341a293213e2852be14c` |
| Действующий AGENTS.md | `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7` |

AGENTS.md проверен на входе и перед записью отчёта, прочитаны CONTRIBUTING.md, PROJECT_CONTRACT.md и относящиеся пункты ROADMAP. Последнее поручение разрешает единственный отчёт и его docs-коммит. ROADMAP владельца сохраняется, учёт этой задачи ведётся в данном отчёте и разговоре.

Номера строк исходников ниже относятся к указанному HEAD. В рабочем `source/render/view.cpp` имеются посторонние вставки трассировки, поэтому его текущие номера местами смещены. Проверенные пути сохранения сверены с `git show HEAD`. Ресурсный конвейер в `new_terrain_canvas.cpp`, `res_save_coordinator.cpp`, `res_archive_index.cpp`, `terrain_atlas_compact.cpp`, `view_world_terrain.cpp` между принятой 276 и этим HEAD не изменился.

## Метод и достаточность

Изучены мост, схемы, сервер, read/edit/lifecycle/object adapters, headless startup, интеграция MainWindow, физические locks, MPR/MOB/RES save, retirement namespaces, Undo, экспорт, сохранение навигации и относящиеся тесты. Проверен весь каталог из 41 инструмента. История 218–225, 239, 258, 271–273 использована для проверки происхождения контрактов и границ evidence.

Собраны только собственный мост из текущих исходников и существующие небольшие тестовые программы в `work/mcp-audit-20261104/`. Подключения выполнялись к собственному синтетическому named-pipe peer. Использовались отдельные `EI_MAPPER_AUTOMATION_DIR`, `EI_MAPPER_SETTINGS_DIR`, TEMP/TMP и тестовый lock-root. У собственного моста рядом отсутствует редактор, поэтому стенд также исключает случайный запуск установленного EXE. Настройки владельца, его MCP endpoints и процессы для тестов не использовались.

Функциональный MainWindow, headless-редактор и настоящие карты в этом аудите не запускались. Поведение редактора установлено чтением его реализаций и существующих тестов, с отдельной маркировкой исторических запусков. Приватный код движка не исследовался. Skills evil-islands-modding и evil-islands-private-engine-reference в сессии отсутствовали, работа ограничена обычным кодом Ataman. Игровая и визуальная приёмка сюда не входят.

Классификация описывает последствия достижимого сценария. Чтение кода подтверждает соответствующую ветвь реализации, но не означает, что этот сценарий происходил у владельца.

## Сводка находок

15 находок. Критично — 0, серьёзно — 5, умеренно — 8, мелко — 2.

| ID | Уровень | Находка | Подтверждение |
|---|---|---|---|
| A01 | серьёзно | Потеря явного выбора экземпляра после разрыва | Воспроизведено собственным мостом |
| A02 | серьёзно | Background выбирается без привязки к установке и профилю | Код |
| A03 | серьёзно | Fallback map roots, включая Base, доступны для записи | Код, граница продуктовых полномочий |
| A04 | серьёзно | Retirement namespaces не учитывает вложенные MPR | Код |
| A05 | серьёзно | Save не образует единую аварийно-атомарную транзакцию MPR/MOB/RES/производных | Код |
| A06 | умеренно | Ошибка операции маркируется MCP `isError=false` | Воспроизведена упаковка ответа, серверная форма подтверждена кодом |
| A07 | умеренно | Dirty headless после Off удерживает процесс и locks без доступного MCP восстановления | Код |
| A08 | умеренно | Часть read/export jobs не привязана к соединению | Код |
| A09 | умеренно | Схема wrapper preview исключает edit_units | Каталог собственного моста и код валидатора |
| A10 | умеренно | Обычные окна не перечитывают общий отзыв mode/Save | Код |
| A11 | умеренно | Undo может сообщить успех после неудачного восстановления | Код |
| A12 | умеренно | Мост забывает длинный edit ID раньше окончания разрешённой подготовки | Код |
| A13 | умеренно | Часть старых проверок несовместима с каталогом и версией 277 | Код тестов |
| A14 | мелко | Неполная валидация initialize и ответ на неизвестное notification | Воспроизведено |
| A15 | мелко | Руководство смешивает актуальные правила и устаревшие инструкции | Документация и код |

## Находки с условиями отказа

### A01. Явный экземпляр теряется при переподключении

Серьёзно. `tools/mcp_bridge/main.cpp:212` сохраняет только `targetPreference`, по умолчанию `auto`. Переданный `instance` используется для текущего подключения, но не сохраняется как ограничение последующего выбора. `detach()` на строке 109 сбрасывает selected, `ensureSelected()` на строках 295–301 повторяет connect только с targetPreference.

Сценарий. Агент явно выбирает A по UUID. A закрывается или пересоздаёт endpoint при смене режима. Доступным остаётся B. Следующий get_context/get_revision автоматически относится к B. После получения новой ревизии правка B будет разрешена его собственным доком, хотя агент продолжает задание для A. Старый revision обычно защищает от немедленной записи, но штатное перечитывание revision уже переводит задачу на другой документ.

Воспроизведение на свежем мосте 277 и двух собственных синтетических endpoints дало последовательность `explicit_A.marker=A`, удаление A, `get_context.marker=B` без нового connect_instance. Исполняемые редакторы не использовались. Документированное удержание выбранного сеанса в `docs/AI_AGENT_MCP.md:260` не покрывает этот переход.

### A02. Background не изолирован по установке или профилю

Серьёзно. `tools/mcp_bridge/main.cpp:68–72` ищет любой background descriptor в общем discovery. В выборе на строках 210–225 каждый следующий background заменяет предыдущего кандидата. installation, версия, профиль и источник сохранённых полномочий не сравниваются. Множественные background endpoints не дают ошибку неоднозначности, в отличие от обычных окон.

Сценарий. Другая копия маппера использует тот же discovery и уже держит background с иными endurance-options/resourceRoots. Мост из текущей установки при target=background получает тот процесс. Если descriptors несколько, выбор зависит от порядка UUID. Ревизия и file locks защищают документы внутри выбранного процесса, но не доказывают правильность выбранного мода. Даже один background разделяется агентами, что также не создаёт отдельное рабочее место для каждого задания.

Подтверждено чтением кода. Saved-Off проверяется перед startBackground, а существующий background может быть выбран до этой проверки. Сам headless-сервер перечитывает своё исходное хранилище полномочий. При разных settings/discovery-контекстах это может быть другое разрешение владельца. Вывод не означает обход Off непосредственно в выбранном редакторе.

### A03. Читаемые fallback roots являются целями записи

Серьёзно как риск области записи. `source/app/main_window.cpp:5921` возвращает весь mapRoots без разграничения read/write. Встроенный portable JMV-профиль включает `{modRoot}/Maps` и `{gameRoot}/Base/Maps` в `source/resources/resource_profile.cpp:381`. `captureLifecycle()` в `source/app/ai_agent_lifecycle.cpp:64` использует весь список. create_map выбирает любой root по индексу на строках 71–75. resolve/authorized на строках 18–38 разрешают открытие существующих карт из всех этих roots.

Сценарий. В DejaVu агент вызывает create_map с root=1 либо открывает Base-карту по ключу `1/...`, правит её и вызывает save с разрешённым Save. Общего запрета записи Base или отдельного согласия на запись fallback root нет. Аналогично редактируется уже открытая человеком MPR вне ожидаемого mod root, поскольку sculpt/edit_objects/save проверяют полномочия документа, а не принадлежность writable root.

Это подтверждённая возможность текущей политики. Base как самостоятельно выбранный профиль также допускает создание карт, что исторически использовалось тестом 221. Здесь не утверждается, что любая запись Base ошибочна. Существенная граница отсутствует именно между источниками чтения профиля и разрешёнными целями агентской записи. Произвольный путь через name/root строкой передать нельзя, root является индексом настроенного списка.

### A04. Удаление старого namespace пропускает вложенные карты

Серьёзно. `source/render/terrain_atlas_compact.cpp:28` строит inventory через `entryInfoList` только одного уровня. `terrainAtlasMapDirectories()` на строках 378–385 собирает несколько каталогов эвристикой пути, но не весь рекурсивный каталог карт. `terrainCompactRetiredMembers()` на строках 273–284 считает namespace свободным при отсутствии ссылки в этом inventory. `retireTerrainAtlasAfterSave()` на строках 442–451 выполняет удаление с повторной проверкой того же неполного inventory.

Сценарий. В `Maps/subdir/B.mpr` сохранена копия A с прежним hash-namespace. Агент открывает `Maps/A.mpr`, меняет её атлас и сохраняет с новым namespace. Когда dirs состоят из Maps и Base/Maps, вложенная B не попадает в inventory. Старые страницы и embedded metadata удаляются из общего RES, после чего B перестаёт разрешать свой атлас. Сам MCP-каталог обнаруживает вложенные карты через `QDirIterator::Subdirectories` в `source/app/ai_agent_read.cpp:114`, поэтому такое размещение реально поддержано чтением.

Подтверждено кодом, разрушительный сценарий не воспроизводился. Резерв архива сохраняется. Это отдельный механизм удаления используемого namespace, а не доказательство причины потери namespaces zone18/zone19, лежащих в обычном верхнем Maps.

### A05. Save допускает частично опубликованное семейство файлов

Серьёзно. `source/app/ai_agent_edit.cpp:624–640` сначала вызывает штатный MPR Save, затем saveAllMob. `source/render/view.cpp:17076–17108` сохраняет dirty MOB последовательно, останавливаясь при первой ошибке. Уже сохранённые MPR/RES и предыдущие MOB обратно не откатываются.

Сценарий с обрабатываемой ошибкой. Операция sculpt меняет высоты MPR и связанные высоты объектов MOB, либо накоплены изменения нескольких MOB. MPR/RES успешно записаны, следующий MOB нельзя заменить. MCP вернёт ошибку, но диск уже содержит часть нового состояния. Dirty и резервные копии остаются, однако повторная загрузка до исправления отказа получает рассогласованную пару.

Отдельный аварийный сценарий. `source/documents/map_save_batch.cpp:105` записывает recovery journal, затем на строках 112–126 по одному commit каждого QSaveFile. В world-save MPR добавляются в files до общего RES — `source/render/view_world_terrain.cpp:608–619`. Аварийное завершение между этими заменами оставляет новую MPR со старым RES. Rollback на строках 133–153 выполняется только живым процессом. Поиск использования `map-save-recovery` во всём source нашёл создание журнала и закрепление backup при retention в `source/documents/backup_manager.cpp:704–716`, автоматического доведения/отката журнала на старте не обнаружено.

Nav64 имеет отдельный lifecycle. `source/render/view_navmesh.cpp:45–54` направляет успешное сохранение геометрии в requestBakeOnSave, `source/ui/navmesh_panel.cpp:195–210` откладывает запекание. MCP `status=saved` не ждёт результата этого дочернего этапа. Сохранённый legacy AI-граф лежит в MOB, `.grp` инструмент rebuild_ai_graph непосредственно не меняет. Ни legacy graph, ни производные Nav64 не добавляются единой общей транзакцией к MPR/RES/MOB.

Все эти выводы подтверждены кодом. Принудительный сбой полного редактора в аудите не выполнялся. Существующие tests с injected commit failure проверяют возврат из ошибки, что не эквивалентно остановке процесса между rename.

### A06. Ошибка внутри operation_status выглядит успешной для MCP-клиента

Умеренно. `source/app/ai_agent_server.cpp:415–416` возвращает обычному завершённому job `status=complete` независимо от `job.result.error`. `tools/mcp_bridge/main.cpp:349` проверяет только верхний error и верхние failed/cancelled. Вложенная ошибка не влияет на isError.

Сценарий. Подготовка или commit отказали по revision conflict, запрету Save либо ошибке файла. Клиент получает успешный CallToolResult и complete, если не разбирает JSON во вложенном текстовом content. По документации клиент должен проверять result.error, но стандартный механизм сообщения об ошибке инструмента остаётся неверным.

На собственном peer мосту возвращена серверная форма `{"status":"complete","result":{"error":"Document revision conflict; test fixture"}}`. Реальный stdio-ответ моста имел `isError=false`. Форма реального редактора подтверждена указанным dispatch. Исправный клиент может вручную распознать этот JSON. Контракт MCP описывает tool execution errors через isError=true. [MCP Tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)

### A07. Dirty headless после Off нельзя завершить через MCP

Умеренно. `source/app/ai_agent_server.cpp:230` применяет Off через setEnabled/stop, убирая pipe и descriptor. Затем `shutdownHeadless()` на строках 240–247 отказывает при dirty. Простой, отключение всех мостов и повторные вызовы prune не отменяют этот отказ.

Сценарий. Агент применил правку без Save, владелец выключил доступ. Процесс удерживает несохранённый документ и lifetime leases, но pipe для save/undo уже закрыт. Указание ошибки «сохранить или отменить» через MCP невыполнимо до повторного включения доступа. Поскольку процесс скрыт, окно для ручного разрешения также отсутствует. Изолированные discovery/settings деревья могут оставить несколько таких процессов.

Подтверждено кодом. Сохранение dirty в памяти намеренно защищает данные, что прямо описано в отчёте 225. Найденный пробел относится к доступному способу разрешения этого состояния и ограничению срока удержания ресурсов.

### A08. Владение read/export jobs неполное

Умеренно. В `source/app/ai_agent_server.cpp:260–344` owner задаётся только для list_units/get_unit. startTopdownExport на строках 469–471 также не задаёт owner. Отключение клиента на строке 144 отменяет только jobs с совпавшим owner. status/cancel на строке 396 проверяет владельца исключительно для `job.edit`.

Сценарий. Агент запускает export_map_topdown и отключает мост. Экспорт продолжает вычисления и может опубликовать файлы после исчезновения инициатора. Другой агент, которому известен operation ID, может прочитать/снять результат или отменить этот экспорт. ID чужого scene job может быть возвращён через BUSY у window_snapshot на строках 425–426. Аналогичный пробел отмены по disconnect есть у обычных background reads.

Подтверждено кодом. Прямой Off отменяет такие jobs, а подготовка изменяющих jobs и sourceOperation units привязаны к владельцу правильно. Незнание UUID затрудняет чужое обращение, но не заменяет проверку ownership.

### A09. Схема preview не описывает поддержанный edit_units

Умеренно. `source/app/ai_agent_protocol.cpp:197–198` не включает edit_units ни в enum tool, ни в anyOf arguments. Однако специальный валидатор на строках 254–258 и startEdit его принимают.

Сценарий. Клиент, проверяющий inputSchema, отклоняет `preview(tool=edit_units, arguments=...)` до отправки, хотя редактор поддерживает этот маршрут. Поле preview=true непосредственного edit_units работает и является доступным обходом. Все схемы синтаксически построены как JSON Schema, но эта семантическая неполнота препятствует единому wrapper API.

Подтверждено каталогом собственного моста и чтением server validation. Полный список tool содержит 41 запись. Отсутствие инструмента в enum приведено в воспроизводимом probe ниже.

### A10. Общий отзыв полномочий не обновляет другие обычные окна

Умеренно. `source/app/ai_agent_server.cpp:37–42` сохраняет режим/Save в общие QSettings и меняет только свой сервер. headlessAuthority на строках 13–16 и периодический sync на строках 226–231 работают только для headless. Обычное второе окно держит собственные m_mode/m_allowSave без повторного чтения общего файла.

Сценарий. Открыты обычные A и B с Edit+Save в одном settings-профиле. В A владелец выбирает Off либо снимает Save. B продолжает принимать изменения/Save под своим старым состоянием, хотя сохранённые предпочтения уже отозваны, а новый headless их соблюдает.

Подтверждено кодом. Это не обход локального Off в B — в его доке остался прежний режим. Если полномочия задуманы независимыми для каждого обычного окна, требуется явное описание этого отличия от общего сохранённого разрешения и headless. Сейчас руководство создаёт более широкое ожидание действия отключения.

### A11. Undo может вернуть undone после отказа восстановления

Умеренно. `CAiAgentCommand::undo()` в `source/app/ai_agent_edit.cpp:412` игнорирует результат switchMap, а replay failure сообщает только через updateMsg. `CAiEditAdapter::undo()` на строках 618–622 проверяет сдвиг индекса и лишь отдельный aiLifecycleHistoryError, который CAiAgentCommand для этих отказов не устанавливает.

Сценарий. Создать B из чистой A, затем внешний процесс удаляет или повреждает A. Агент получает свежую ревизию B и делает undo_last_agent_operation. switchMap сначала выгружает B, затем не может загрузить A. QUndoStack уже сдвинулся, и MCP отвечает status=undone. Прежнее рабочее место не восстановлено. Аналогично ошибка replay может оставить сообщение только в UI при успешной оболочке MCP.

Подтверждено кодом, реальный документ не удалялся. Одна запись QUndoCommand на обычную правку действительно создаётся, проблема относится к обработке отказа её исполнения.

### A12. Edit poll allowance истекает раньше разрешённой подготовки

Умеренно. `source/app/ai_agent_server.cpp:565` разрешает auto_paint 120 секунд подготовки. Мост в `tools/mcp_bridge/main.cpp:270–289` хранит editJobs только 60 секунд от исходного запроса. После expiry operation_status/cancel идут с обычным двухсекундным local timeout вместо 30 секунд. TTL результата редактора при этом правильно считается от завершения.

Сценарий. auto_paint готовится 85 секунд, затем входит в длительную GUI публикацию кандидата. Poll на 90-й секунде уже не имеет длинного allowance, может потерять pipe через 2 секунды. Начавшийся commit сохраняется редактором, а его job привязан к старому socket. Новый клиент не может получить его результат как другой writer. Переисполнение новой мутации требует проверки фактического результата.

Подтверждено кодом. Длительный auto_paint/GUI timeout не запускался, поэтому частота такого отказа не измерялась. Отдельно мост при любых IPC timeouts правильно уничтожает поток и не переигрывает мутацию автоматически.

### A13. Старые проверки нельзя считать актуальным сплошным регрессом 277

Умеренно. `tests/ai_agent_check.cpp:111` и `tests/ai_agent_edit_check.cpp:81` требуют контекст версии 219. `tests/ai_agent_sdk_check.py:34` и строка 41 требуют ровно 22 tools, тогда как текущий каталог содержит 41. Проход с текущими бинарными файлами штатно остановится на этих assertions независимо от исправности рассматриваемого поведения.

Сценарий. Сопровождающий запускает исходный regression entry point против 277 и получает ложный FAIL либо сохраняет старую проверенную сборку для получения PASS. Это мешает подтвердить весь поддерживаемый интерфейс на одном текущем состоянии.

Подтверждено чтением тестов и текущим tools/list. Файлы не исправлялись. Более новые tests/mcp_objects_check.cpp и mcp_units_check.cpp содержат актуальные специализированные сценарии, но не заменяют автоматически все ранние проверки. Счётчик PASS в native harness включает повторные envelope/poll assertions и не равен числу различных пользовательских сценариев.

### A14. Неполная обработка служебных JSON-RPC сообщений

Мелко. В `tools/mcp_bridge/main.cpp:314–319` initialize без protocolVersion/capabilities/clientInfo принимается и переводит legacyInitialized в true. На строках 422–427 специальные notifications/* игнорируются корректно, но валидное неизвестное notification с иным method получает error с id=null.

Воспроизведено на собственном мосте. Запрос initialize с пустыми params вернул protocolVersion 2025-11-25 и capabilities. Сообщение `{"jsonrpc":"2.0","method":"nonexistent"}` вернуло `{"id":null,"error":{"code":-32601,"message":"Method not found"}}`. Ответ на notification не соответствует JSON-RPC. Требования handshake требуют полей версии, capabilities и clientInfo. [MCP Lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle), [JSON-RPC 2.0](https://www.jsonrpc.org/specification#notification)

Сценарий отказа связан с клиентом, проверяющим строгую корреляцию ответов, либо с ошибкой конфигурации клиента, скрываемой успешным initialize. Стандартные исправные initialize поддерживаемых редакций проходят.

### A15. Документация содержит устаревшие операционные указания

Мелко. `docs/AI_AGENT_MCP.md:271–273` говорит о будущей установке 225, строка 345 — о staging 225 и установленной 223. Строки 241–242 без уточнения объявляют отмену операций при Off, хотя уже committing job удерживается `source/app/ai_agent_server.cpp:84,104–105`. Строка 113 каталога в ai_agent_protocol.cpp называет cancel_operation отменой только read, хотя работает и для edit preview. Строка 197 обещает PNG wrapper preview для всех перечисленных инструментов, тогда как object/graph/unit summaries имеют иной состав. В установленном BUILD_INFO.txt осталась запись 273/STOP при VERSION 277 и подтверждённых SHA установки 277.

Сценарий. Агент/владелец ожидает прерывания уже начавшегося commit, ищет ненужный этап установки, либо ждёт одинакового PNG от объектного preview. Отдельные разделы руководства правильно описывают часть ограничений, поэтому проблему создаёт сосуществование противоречащих указаний.

Все 41 имени инструмента присутствуют в AI_AGENT_MCP.md. Инструментов полностью без упоминания и отсутствующих в фактическом каталоге основных описанных команд не обнаружено. Имеются несоответствия параметров/свойств и поведения A01, A09, A10, A15. Исторические отчёты 218–225 не должны переписываться как описание текущей версии.

## Гипотеза об откате HDPack_Terrain.res

### Проверенный путь записи

Для обычного старого in-memory RES index/namespace set гипотеза отклонена применительно к рассмотренным путям 277. Найденные изменения кода этого ресурсного конвейера после 276 отсутствуют. Проверены следующие маршруты.

1. create_map вызывает `commitNewTerrainCanvas()` через `source/app/ai_agent_edit.cpp:568`. В `source/render/new_terrain_canvas.cpp:138–140` удерживаются RES/map leases, на строках 161–167 создаётся свежий CResArchiveIndex и считается текущий SHA. Transaction добавляет новые members, отклоняет коллизии и передаёт beforeSha256 в общий batch на строках 188–195.
2. Рабочий atlas Save открывает свежий `hdPackArchive` в `source/render/view.cpp:10975–10976`. Финальная публикация отдельно открывает `sourceArchive` на строке 15927 и считает SHA на строке 15931. Это не индекс Resource Browser, загруженный при открытии редактора.
3. World terrain Save читает текущие байты RES через QFile в `source/render/view_world_terrain.cpp:510–512`, строит обновлённый архив с остальными членами и передаёт исходные байты на compare в batch на строках 611–619.
4. `commitStreamedResMembers()` в `source/documents/res_save_coordinator.cpp:654–658` получает resource lock и проверяет sourceIsCurrent. На строках 672–688 проверяются SHA исходника, backup и race перед commit. `source/resources/res_archive_index.cpp:367–491` переносит все неизменяемые члены, сравнивает их payload/timestamps, заново строит и проверяет hash-цепи, перед commit повторяет guards.
5. `commitMapSaveBatch()` проверяет before до staging, перед backup и непосредственно перед каждой заменой, сохраняет проверенные backups и выполняет compare-and-restore при обнаруженной ошибке. Два актуальных экземпляра с разными MPR и общим RES поэтому либо сливают по очереди свежие состояния, либо один получает conflict/busy.

sourceIsCurrent сам по себе проверяет размер, mtime и fingerprint начала файла, а не весь архив — `source/resources/res_archive_index.cpp:160–176`. Внешняя запись, сохраняющая эти признаки намеренно, находится за пределами данного вывода. Это не криптографическая защита от другого процесса с полномочиями того же Windows-пользователя. Обычные source/backup hash guards и перечитывание перед публикацией проверены отдельно.

### Исторические свидетельства

Отчёт `ATAMAN_ZONE18_19_ATLAS_RECOVERY_20261103.md` подтверждает отсутствие обоих новых namespaces при оставшихся новых MPR, затем record-level восстановление 30 members с сохранением 516 прежних. Его выводы об установленном результате не подменяются повторной записью живых ресурсов.

В `artifacts/EI-Mapper-Ataman-2-first-test/logs/workflow-20261103-144227.log` найдены следующие записи.

| Строка | Наблюдение |
|---|---|
| 2 | Runtime version 276, запуск 03.11 в 11:57:09 |
| 91 | HDPack_Terrain.res индексируется с 516 entries уже в 11:57:11 |
| 3078 | External archive change в 12:02:53 |
| 8542, 8544 | External archive change в 12:34:01 |
| 8517, 8520 | Перед вторым событием удалены старые namespaces zone9 и zonemainmenunew, по 33 и 10 members |

Сообщение `Resource Browser observed external archive change` исходит из `source/ui/resource_browser.cpp:803–816`. Оно означает, что локальный watcher получил изменение вне своего текущего internal-publication маркера. Оно не содержит PID писателя, старого/нового SHA или состава удалённых members. Последующая асинхронная перестройка индекса сама RES не пишет. Само количество 516 не доказывает идентичность архива по байтам, но показывает, что два поздних watcher timestamps нельзя принять за доказанный момент исходного отката.

В логах 02.11 найдены публикации/сохранения zone18/zone19 и резервирование производных Nav64. Наличие резервов и уведомлений не устанавливает виновный процесс. Чужие фоновые деревья из recovery-отчёта в этом аудите не запускались и не опрашивались.

### Вывод об инциденте

Штатная запись целого старого RES из сохранённого Resource Browser index текущим MCP-кодом не подтверждается и противоречит свежему чтению/merge/CAS в проверенных маршрутах. Причина конкретного исторического отката остаётся неустановленной. Полномочия и бинарная идентичность исторических чужих headless-копий по этим логам неизвестны.

Два других механизма требуют исправления независимо от расследования — неполный inventory удаления namespaces A04 и авария между commit MPR и RES A05. Второй способен дать именно новую MPR со старым RES. Ни один из них не доказан как причина данного инцидента. Для причинной атрибуции нужны журнал писателя с PID/EXE SHA и before/after SHA архива, состав publication/retirement и recovery journal конкретной транзакции. Содержимое живой игры для этого аудита не изменялось.

## Проверенное поведение без обнаруженного дефекта

Здесь «код» означает проверку реализации, «свежий тест» — выполненный в этом аудите запуск. Широкая игровая исправность из этих результатов не выводится.

- Локальный Off закрывает listener, отзывает secret/UUID, убирает descriptor и отменяет подготовку. Read-only блокирует все методы из mutation allowlist, включая preview/apply/undo/open/close. Создание MPR/MOB и размещение в мире, Save и записывающая layout Undo требуют Save. FinishCommit повторно сверяет headless authority и Save. Обход этих проверок через вложенный preview не найден. Уже начавшийся commit намеренно завершается. Код `ai_agent_server.cpp:99–129,346–386,504–526`.
- Headless startup при отсутствующем или недопустимом сохранённом mode завершает процесс до MainWindow. Полномочия копируются во временные QSettings, источник сохраняется для повторного чтения. Отсутствующее allowSave равно false. Startup/metadata/list_instances не запускают headless. Код `main.cpp:4254–4278`, `tools/mcp_bridge/main.cpp:69–105`.
- Discovery защищается DACL текущего пользователя, named pipe использует UserAccessOption, secret проверяется до dispatch. Секрет удаляется из list_instances. TCP listener и tool для включения режима отсутствуют. Код `ai_agent_protocol.cpp:10–39`, `ai_agent_server.cpp:115–124,153–184`.
- Name create_map ограничен ASCII-идентификатором, запрещает Windows device names, существующие MPR/MOB не перезаписываются созданием. Lifecycle resolver отклоняет абсолютные пути, `..`, двоеточия и выход за canonical roots. Template выбирается внутри тех же roots. Проверен код `ai_agent_lifecycle.cpp:18–45,67–75,97–143` и `ai_agent_edit.cpp:554–568`. Риск ширины самого списка roots выделен A03.
- export_map_topdown допускает запись только в новую абсолютную папку workspace с существующим родителем. Проверяет game/map/resource overlaps, UNC/ADS, reparse ancestors и удерживает directory handles против подмены маршрута. read_export_image принимает зарегистрированный exportId/layer, сверяет manifest и PNG SHA/размеры, не принимает произвольный путь. Код `topdown_export_files.cpp:81–179`.
- window_snapshot захватывает заново отрисованный framebuffer собственного GL viewport через captureTopdownPass/FBO, а не экран Windows, соседние окна или clipboard. Есть контекст карты/профиля, capturedRevision, проверка изменения документа и bounded PNG encoding. Код `ai_agent_server.cpp:422–462`, `ai_agent_read.cpp:250–256`, `render/view_topdown.cpp`. Снимки владельца в аудите не получались.
- Ревизия включает активный terrain/state/materials, Undo index/count, сериализацию MOB, рабочие соседние документы, профиль, resource epochs и layout bytes. Объектные планы дополнительно проверяют SHA дисковых MOB, family/map.txt зависимостей до применения. Preview хранит detached план, принадлежит writer, apply проверяет captured/current revision. Код `ai_agent_edit.cpp:421–451,547–550`, `ai_agent_objects.cpp:249–288,353,627–628`.
- Физические MPR/одноимённый MOB используют общий ключ, дополнительные MOB и affected maps удерживают leases. RES имеет отдельный resource key. Store определяется Known Folder ProgramData, production игнорирует тестовую переменную. Старые locations проверяются без записи, живой lease не стареет. Свежий тест подтвердил один/разные документы, другое имя EXE, release последней ссылки, новые пути, crash-PID, migration, удалённые/недопустимые PID и отказы store/ACL. Junction специально не запускался.
- Сохраняемые файлы проходят backup, candidate/readback и compare исходника. Свежие 7 RES/export сценариев подтвердили collision/no-overwrite, сохранение существующего архива при ошибке validation, resource/save lock, внешнюю запись/появившийся destination и atomic failure/no-op. Это unit-level проверка используемого координатора, а не полный MPR/MOB/RES round trip.
- Подготовка edit выполняется на detached values, workers освобождают свои snapshots. Сохранение и изменения вызывают штатные конвейеры, отдельного MCP serializer нет. Sculpt/paint/natural operations используют одну команду общей истории с affected maps. Objects/units/graph используют одну native Undo command с validation и rollback кандидата. Отмена подготовки не публикует живую правку. Lifecycle create Undo намеренно сохраняет начальные файлы на диске. Код и исторические end-to-end тесты.
- Ограничены кадр 1 МиБ, локальный результат 512 КиБ, входная очередь stdio 16, connections 16 authenticated и 16 pending, jobs 8, обычные reads 2, страницы 200, samples 4096, файлы 64 МиБ. PNG encoder уменьшает до 340 КиБ перед base64, размеры snapshot не более 1024×768. Переполнение возвращает ошибку. Код `ai_agent_protocol.h:6–10`, `tools/mcp_bridge/main.cpp:388–419`, `ai_agent_server.cpp:135–147,185–195`, `topdown_export_files.cpp:23–35`.
- Timeout закрывает локальный поток, поздний чужой ID не принимается. Мутации не переигрываются транспортом автоматически. Результаты server jobs живут 60 секунд после completion, истёкший ID даёт Unknown or expired operation. Export/snapshot terminal result потребляется при чтении, обычный read/edit result остаётся до TTL/cancel. Это различие важно для повторных запросов.
- Свежий catalog-only тест подтвердил инструменты/ресурсы/templates/instructions от peer, list_changed в простое без запроса, новый инструмент через старый fallback, удаление прежнего инструмента, fallback 218–221, явный отказ несовместимого major и восстановление, EOF exit0. Взаимодействие с живыми клиентами владельца отсутствовало.

## Матрица всего каталога

| Группа и все входящие tools | Фактический маршрут и результат аудита |
|---|---|
| list_instances, connect_instance, disconnect_instance | Мост, ограниченный discovery и секретный pipe. Выбор/переподключение имеют A01/A02. Явный disconnect подавляет metadata auto-connect до нового connect |
| get_context, get_revision | Синхронные read adapters. Документ/режим/dirty/revision доступны, гарантия верного workspace зависит от выбора экземпляра |
| read_terrain, read_paint, material_palette, list_objects | Snapshot/ограниченные страницы. Обычное чтение не правит выделение, Undo или файлы карт. read_terrain асинхронный, остальные здесь синхронные |
| search_database, list_profile_maps, read_profile_map | Immutable DB или отдельный read-worker с Windows memory limit 512 МиБ и deadline. Каталог рекурсивный, profileRead требует относительный catalog key, соседний MOB ограничен canonical directory |
| list_units, get_unit | Detached CUnit logic/raw snapshot с connection-owned sourceOperation и TTL. SS_TEXT используется для статического анализа ссылок, не исполняется |
| edit_objects | Static-only операции, ID allocation по семейству и ссылкам, предел 500, preview/apply/native Undo, Save отдельно. Схема размещения/модель/текстуры проверяются |
| edit_units | Transform/copy/delete/points, пять logic slots, remap внутренних ссылок/ID, cross-map sourceOperation, существующий targetMob, passability diagnostics и лимиты. Создания из базы нет. Wrapper schema имеет A09 |
| query_passability, validate_map | Read-only detached диагностика. Восемь слоёв, 200 точек/32 м, валидация family и ресурсов, Info отдельно. Ошибки могут иметь A06 |
| rebuild_ai_graph | Штатный detached builder старого графа, применение в primary MOB, одна Undo. Сохранение отдельно, прямой записи .grp/Nav64 нет |
| sculpt, paint | Рельеф/покрытие через общий terrain pipeline, captured revision, соседние карты с leases, одна Undo. По умолчанию headless includeNeighbors=false, обычный редактор использует включённых соседей |
| generate_terrain, erode, carve_river, auto_paint | Общий natural/terrain pipeline, seeds/области/iterations/пределы. Detached preparation, одна Undo. auto_paint имеет deadline 120 с и A12 |
| preview, apply | Вложенная валидация выбранного инструмента, owner и revision. A09. Apply удаляет использованный preview, новую мутацию после неизвестного результата нельзя считать автоматически безопасным повтором |
| create_map, create_mob | Profile root/template и no-overwrite, Save обязателен, disk creation до загрузки с rollback отказа загрузки/мира. Начальный файл после успешного create и Undo сохраняется. A03 |
| open_map, close_map | Только Edit, асинхронная смена чистого workspace. Dirty terrain/MOB/script/travel/database отклоняет навигацию, перед заменой проверяются target leases |
| place_map_in_world, set_world_position | Проверка размеров/пересечений и layout transaction, Save обязателен. set_world_position — тот же маршрут. Layout имеет lock, merge независимых записей, compare/backup/atomic write |
| operation_status, cancel_operation | TTL, bounded results и отказ отмены committing job. A06/A08/A12. Cancel завершённого edit удаляет result, но не отменяет уже применённое действие, для этого нужна Undo |
| export_map_topdown, read_export_image | Собственный renderer, aligned PNG/manifest, новая workspace-папка, immutable PNG writer и guards, затем registry read. A08. Отмена writing может оставить явно incomplete папку, что предусмотрено реализацией |
| window_snapshot | Только viewport GL/FBO, фазовый job 30 с, bridge ждёт image, BUSY/timeout/cancel. Работа с экраном/буфером не обнаружена |
| shutdown_headless | Чистый background закрывается, обычное окно защищено. Dirty/preview/busy удерживают фон, A07 |
| undo_last_agent_operation, save | Верхняя agent command и current revision, штатные save/backup pipelines. Save очищает содержащую agent commands историю. A05/A11, это не независимая распределённая транзакция |

## Протокол, ресурсы и клиенты

stdio использует по одному UTF-8 JSON object на строку. Частичные/склеенные frames разбираются, batch/non-object и недопустимый UTF-8 отклоняются. stdout содержит только JSON-RPC, diagnostic уходит отдельно. Ресурс ataman://tools строится той же ai::mcpCatalog, что tools/list fallback и editor mcp_catalog, содержит protocolMajor=1, catalogVersion=1, version, 41 tools, instructions, две resources и пустой resourceTemplates. Живой редактор вправе прислать новый совместимый каталог, мост пересылает его без старой локальной tool allowlist.

Legacy initialize для 2024-11-05, 2025-06-18, 2025-11-25 имеет согласование версии. Дополнительный маршрут с per-request metadata/2026-07-28 изучен по коду. Ссылка на спецификацию 2026-07-28 из руководства оказалась недоступна веб-инструменту. Этот аудит сверял требования standard initialize/tools/stdio по доступной официальной спецификации 2025-11-25. Полная нормативная сертификация расширенного маршрута и запуск клиентских SDK 2026 не выполнялись. [MCP Lifecycle](https://modelcontextprotocol.io/specification/2025-11-25/basic/lifecycle), [MCP Transports](https://modelcontextprotocol.io/specification/2025-11-25/basic/transports)

JSON Schema имеет object/properties/required/additionalProperties=false, типы, диапазоны, enum и bounds массивов. Условные зависимости area/action дополнительно проверяет runtime. Обнаружена A09. Unknown tools сейчас превращаются в текстовый tool error после selection/forwarding, тогда как стандарт относит неизвестное имя к protocol errors. Resources/read неизвестного URI через remote catalog даёт общий -32000 вместо более конкретной классификации. Эти различия входят в границы строгой protocol compatibility, самостоятельный сценарий потери данных для них не найден. [MCP Tools](https://modelcontextprotocol.io/specification/2025-11-25/server/tools)

Ошибка приложения и ошибка transport различаются, но A06 ломает автоматическое определение части application failures. StructuredContent/outputSchema не объявлены, данные инструментов возвращаются JSON в TextContent. Это допустимый формат, клиенту требуется разбирать соответствующий text. Вложенные preview/snapshot дополнительно дублируются стандартными image blocks, общий выход ограничен 1 МиБ. Повторный operation_status для consume-on-read export/snapshot может уже вернуть unknown ID.

Исторический отчёт 222 корректно отделяет Claude Code 2.1.308, где наблюдался повторный tools/list по list_changed в том же соединении, от Codex 0.164.0-alpha.11.1, где подтверждены устойчивость, прямые calls и новый connect, но обновление каталога модели в постоянной Desktop-связи не доказано. В этом аудите эти версии клиентов не запускались. Свежий synthetic hot-swap подтверждает серверный механизм уведомлений, а не поведение сегодняшнего UI Codex/Claude.

## Количество процессов и завершение

Один EIMapperMcp обслуживает одну stdio-сессию. Родительский MCP-клиент создаёт эти процессы, в мосте нет общего singleton или менеджера, который закрывает другие мосты. Каждый живой stdin может удерживать собственный процесс сколько угодно. EOF ведёт к app.quit. Шесть–одиннадцать мостов сами по себе не означают шесть–одиннадцать редакторов.

Старт headless сериализуется headless-start.lock. Lifetime защищён mutex с hash discoveryDirectory и headless.lock — `source/app/main.cpp:4262–4267`. При одном discovery/session это ограничивает headless одним процессом, независимо от каталога EXE. При разных discovery roots либо Windows sessions возможны отдельные экземпляры. Общего машинного лимита по всем таким roots нет.

Чистый фон выходит после 10 минут без рабочих запросов или примерно 1,5 с без клиентов после первого агентского контакта, с повторными guards. Metadata activity не продлевает рабочий idle. При незавершённом worker/commit отсчёт откладывается. Dirty фон может удерживаться бессрочно, A07. На readiness timeout 45 с bridge сообщает PID/log, но не имеет управляемого child lifecycle после startDetached. Успешно стартовавший чистый процесс обычно гасит собственный idle-контроллер. Зависание до появления server/expiry не покрыто этим контроллером.

Обычный close проходит dirty preflight и штатный onExit, ожидает активные записи/работников, automation stop вызывается до удаления CView. Агентский unsaved документ не должен теряться от простого disconnect. Начавшийся commit защищён aiCommitBusy, отмена отказывается его прервать. Окна владельца через shutdown_headless завершить нельзя. Код `main_window.cpp:8063–8082,9862–9891,10009–10016`, `ai_agent_server.cpp:240–257,397–401`. Живые процессы владельца в аудите не завершались и не инвентаризировались.

## Что действительно покрывают тесты и отчёты

### Свежие запуски этого аудита

| Проверка | Выполненное подмножество | Результат |
|---|---|---|
| mcp_hotswap_check --catalog-only | Собственный мост 277, собственный QLocalServer, будущие tools/resources/templates, list_changed, fallback/major/recovery/EOF | 89 PASS, 0 FAIL, exit 0 |
| process_edit_lock_tests | 12 выбранных сценариев без junction, отдельный machine-locks | 14 PASS с init/cleanup, 0 FAIL, 0 SKIP, exit 0 |
| minimap_export_tests | 7 сценариев publication/collision/validation/race/lock/no-op на синтетических архивах | 9 PASS с init/cleanup, 0 FAIL, 0 SKIP, exit 0 |
| Собственный stdio probe | Два синтетических endpoints, потеря explicit A, вложенный error, empty initialize, notification, чтение preview schema | A01/A06/A09/A14 подтверждены, owned bridge exit 0 |

Qt lock cases запускались по именам usesServiceDirectoryWithoutChangingDocuments, sameMapMobAndAliasRefuseOtherProcess, differentInstallationsAndApplicationNamesShareLock, independentMapsAndResourcesCanBeEdited, lastSharedReferenceReleasesLock, newDestinationKeepsIdentityAfterCreation, crashedOwnerIsRecovered, liveOldOwnersAreRespectedAndDeadOwnersAreUntouched, remoteOrInvalidLegacyPidIsNotAssumedDead, unavailableServiceStoreRefusesWithoutSidecarFallback, existingRestrictedAclIsRejectedWithoutModification, liveOwnerNeverExpires. Тест junctionUsesPhysicalDocumentStore и ProgramData ACL mode не запускались.

RES cases запускались по именам createsAddsReplacesAndRefusesCollision, appendValidationMatchesByNameAfterHashTableReorder, validationFailureDoesNotPublish, validationFailurePreservesExistingArchive, resourceAndSaveLocksRefuseExport, externalChangesAndAppearingDestinationArePreserved, atomicFailureAndNoOpPreserveArchive. Три QWARN указали на неудачную автоматическую очистку QTemporaryDir с readonly backup files. Все assertions прошли. Оставшиеся собственные каталоги включены в финальную очистку аудита.

Собственный мост собран напрямую qmake/mingw32-make из tools/mcp_bridge/mcp_bridge.pro в новом build-root. Qt 5.12.12/mingw73_64 и MinGW730 прочитаны из настроенного retained kit, toolchain не изменялся, subst/junction не создавались. SHA этого собственного EXE отличается от установленного вследствие отдельной сборки, его version — 277.

| Проверенный временный артефакт | SHA256 |
|---|---|
| bridge/release/EIMapperMcp.exe | `844cc938db76e283bb2ca09c0d8c2a35f3c4cdb1bf7f19b6cc3b2dfdf14cc729` |
| hotswap-result.txt | `85e618ec868e49acb3513cc5ff9a532ed5977ab166600c32758fbddfc9f62d75` |
| locks-verified.txt | `cadffe82bf3015b3c6857f637c21fc66577f13f1db0259d24dbcafbd887211b8` |
| res-verified.txt | `cfd55877c5d8ee0a7717e0d68eb51bdcc144151346bb016e563ea554a445ecda` |
| protocol-probe.json | `d1247773dcc9b7b6d4e86194a2f1eb9359e848bc7d78e3b3c9005d35e2b850dd` |

Транскрипт существенных результатов stdio probe сохранён непосредственно здесь, чтобы вывод был проверяем после удаления временного дерева.

```json
{
  "explicit_A": {"marker": "A", "dirty": false},
  "after_explicit_A_disappears": {"marker": "B", "dirty": false},
  "nested_error": {
    "payload": {"status": "complete", "result": {"error": "Document revision conflict; test fixture"}},
    "isError": false
  },
  "initialize_empty": {"protocolVersion": "2025-11-25", "accepted": true},
  "unknown_notification_response": {"id": null, "error": {"code": -32601, "message": "Method not found"}},
  "tool_count": 41,
  "preview_schema_tools": ["sculpt", "paint", "generate_terrain", "erode", "carve_river", "auto_paint", "edit_objects", "rebuild_ai_graph"],
  "owned_bridge_exit": 0
}
```

### Исторические запуски и текущие тестовые сценарии

| Версия/источник | Что заявлено и чем подкреплено | Граница доказательства |
|---|---|---|
| 218, ATAMAN_MCP_READ_218_20261024.md | Native 540 assertions, настоящий скрытый MainWindow/stdio, reads, secret/Off, framing/late ID, workers/limits, snapshot и clean close, отдельный SDK | Историческая версия, часть текущих ранних assertions привязана к 219. Современный каталог 41 этим SDK test не покрыт |
| 219, ATAMAN_MCP_EDIT_219_20261024.md | Native 165 + fresh reopen 17, create/sculpt/paint через соседей, preview/revision conflict, cancel/disconnect, одна Undo, Save/no-Save и limits | Подготовка и обнаруживаемые ошибки проверены, crash всего файлового cohort между commit не заявлен |
| 220, ATAMAN_NATURAL_TERRAIN_220_20261024.md | Есть unit/natural/native проверки, R2 214 assertions, дальнейшие R3 и сравнения terrain/materials | В отчёте сохранены ограничения художественного сходства. Метрики/картинки не доказывают MCP authority и multi-process save |
| 221, ATAMAN_MCP_AUTONOMY_221_20261024.md | Native 111 + reopen 3, полный create/place/generate/paint/create_mob/save/open, path/dirty/permission, rollback MOB/world и conflict Undo | Это успешная техническая цепочка на fixture, отдельная авария процесса между файлами не проверена |
| 222, ATAMAN_MCP_BRIDGE_HOTSWAP_222_20261024.md | Native installation/rollback 185, production read/edit 118, synthetic client notification probes | Rename live EXE/DLL и protocol hot catalog проверены исторически. Современное обновление UI каталога Codex явно не объявлено подтверждённым |
| 223, ATAMAN_MCP_BRIDGE_CHURN_223_20261024.md | Native 193, адресный исходный starvation 3+1 мостов, раздельные connection budgets, backoff, mode reconnect, reads/edits/rollback | Прямой бесконечный churn исправлен в описанном сценарии. Это не глобальный сборщик всех stdio процессов |
| 224, ATAMAN_MCP_PERSISTENT_MODE_224_20261024.md | Native 188 + catalog 89, отдельные процессы с одним INI, mode/Save restore, restart, 3 bridges, ambiguity 2 windows | Второе окно проверено для выбора, синхронный отзыв Save/Off во всех уже открытых обычных окнах не покрыт |
| 225, ATAMAN_MCP_HEADLESS_225_20261025.md | Native 95, собственные bridge/headless и модель окна владельца, две карты, shared RES/layout, idle, Save revoke, dirty retention, crash/restart | Два экземпляра и обычные конфликты проверены. Explicit instance после исчезновения на другой sole instance, nested namespace и crash cohort не покрыты |
| 239, tests/topdown_export_check.cpp | В исходнике есть export path/race/cancel по фазам, изменение scene/environment, ресурсы, layers/read image, render failure | Disconnect ownership и публикация export после исчезновения инициатора не проверены найденными assertions |
| 258, tests/mcp_job_lifetime_check.cpp | Позднее завершение/TTL от finish, отмена timeout подготовки, неистечение committing job, stop во время commit | Bridge editJobs allowance 60/120 с этим server-only test не проверяется |
| 271, tests/process_edit_lock_tests.cpp | Физические ключи, разные установки, MPR/MOB, RES, crash/PID, старые locks/ACL, junction и resolver | В этом аудите заново выполнены только перечисленные 12 сценариев, production ProgramData/junction не использовались |
| 272, ATAMAN_MCP_OBJECTS_272_20261102.md | Native 90, второй профиль 90, guards 26, regression225 40, static IDs/references/graph/passability/validation/Undo/save | Семь исходных Warning и сравнение с ранним R4 первоначально открыты, затем владелец принял native graph criterion и завершил приёмку |
| 273, ATAMAN_MCP_UNITS_273_20261102.md | Native 138, routes 65, regression225 40, NPC/animal logic, cross-copy, raw Undo, save/reload and validation | Исходные 20/39 Warning описаны, 0 новых ошибок. Это не тест смерти процесса между записью MPR и MOB и не проверка wrapper schema edit_units |

Проверены также tests/ai_agent_revision_check.cpp и natural_terrain_tests.cpp. Первый проверяет стабильность ревизии, ручное изменение, соседний документ и отказ stale apply. Natural tests проверяют детерминизм/геометрию генераторов, их прохождение не устанавливает надёжность transport. Полный набор MainWindow tests на 277 в этом аудите не прогонялся. Для A02–A05, A07–A08, A10–A12 остаются конкретные непрогнанные end-to-end сценарии из описаний находок.

## Сохранность и завершение аудита

Единственный выдаваемый и коммитируемый файл — этот отчёт. Версия остаётся 277. Исходники, translations, ROADMAP, .mcp.json, отчёт 277, установка и конфигурации владельца исключены из коммита. Все записи тестов выполнялись внутри собственного `work/mcp-audit-20261104/`, копии настоящих игровых данных не создавались.

Проверены 10 исходных protected files по SHA256, размеру и mtime, различий нет. В набор входят AGENTS, ROADMAP, отчёт 277, landscape/view/neighbor/trace, mob_load_pipeline_check.cpp, QM и .mcp.json. Проверка процессов с executable path только внутри собственного audit-root нашла 0 оставшихся процессов.

Автоматическая проверка разрешений отклонила `Remove-Item -LiteralPath <audit-root> -Recurse -Force` с причиной `blocked by policy`. Команда удаления не исполнялась, другой способ удаления не применялся. По предусмотренному владельцем варианту оставлен `work/mcp-audit-20261104/cleanup-plan.json`, содержащий точный root, перечень 66 файлов с SHA256/размерами и инструкции повторной проверки перед очисткой. SHA256 плана `f12ae442dd068fbb52a0d2d8d618678c5a4f47cb7c77719fbf1644d860c403cf`. Рабочее дерево теста сохраняется только из-за этого ограничения, игровых копий в нём нет. План не входит в docs-коммит.

Аудит завершён с указанными границами воспроизведения и планом очистки. Перед docs-коммитом проверяется staged diff только этого отчёта, ветка main и исходный HEAD. Исправления программы и повторная установка в объём результата не входят.

## Предложенный порядок исправлений

1. A04 и A05. Сначала закрыть потерю данных — полный inventory всех поддерживаемых map roots/вложенных MPR перед retirement, единая стратегия MPR/RES/MOB публикации с аварийным восстановлением. Добавить сценарии nested shared namespace, остановки процесса между commit каждого файла и отказа второго MOB. Учитывать отдельную готовность Nav64.
2. A01–A03. Закрепить identity выбранного workspace через reconnect, сделать background выбор однозначным с проверяемыми installation/settings/profile, определить writable roots отдельно от fallback чтения и политику Base. Проверить A→B при исчезновении, разные portable copies и root=Base.
3. A06, A08, A12. Единообразно передавать terminal failure в MCP, назначить владельца каждому job, согласовать deadlines server/bridge и дать проверяемое получение результата длительного commit после transport failure. Проверить disconnect в каждой фазе export и два клиента с известным operation ID.
4. A07, A10, A11. Определить доступное владельцу разрешение dirty headless после Off, согласовать per-window/общий отзыв полномочий, передавать ошибки Undo как ошибки операции с сохранённым состоянием истории. Проверить недоступную предыдущую MPR и отказ replay.
5. A09, A13–A15. Синхронизировать schema/runtime/catalog/docs, сделать старые tests пригодными для текущего интерфейса, закрыть строгие JSON-RPC/initialize случаи. Затем повторить совместимость с конкретными актуальными версиями Claude Code/Codex в изолированных конфигурациях, отдельно от synthetic protocol test.

Этот порядок является предложением по результатам аудита. Реализация исправлений требует отдельного задания владельца.
