# Ataman280 — EI-SAVE-INTEGRITY-280 (2026-11-05)

Текущий результат R3: исправлены A04/A05 с сохранением раздельных наборов команд человека279; полный семейный пакет выбирает только MCP. Подготовлен свежий Release280/R3. Ранее переданный R2 директором НЕ ПРИНЯТ: он ошибочно расширял команды человека. STOP BEFORE INSTALLATION; push не выполнялся. Ни установка279, ни игровые карты/ресурсы не заменялись. Есть нарушения изоляции первых тестов, перечисленные отдельно ниже; не считать ограничения тестового запуска полностью соблюдёнными.

## База и область

- Ветка main; обязательный HEAD `401bd0b8d5f66e3bcd1f3a8252f12e8a1972e994` совпал. VERSION279, номер278 отменён;280 свободен.
- AGENTS.md прочитан и повторно сверен: SHA256 `2a2f58ef34f63d6c5d67d165d0b809b046d0a519181119d22ea8fdd8781fe3b7`. Прочитаны A04/A05 ATAMAN_MCP_AUDIT_20261104.md, действующие договоры и перечисленные владельцем участки сохранения/retention.
- Расширены существующие `commitMapSaveBatch`, cohort/companions CView и `map-save-recovery`. Второго механизма сохранения/восстановления нет. A01–A03 и другие находки не исправлялись; MCP catalog/protocol не менялись.
- Игровые fixtures: только копии DejaVu zone1.mpr/zone1.mob и HDPack_Terrain.res в `work/save-integrity-280-20261105`. Base-карты, приватный код движка, subst/junction, computer-use и мосты владельца не использовались. Навыки evil-islands-modding/private-engine-reference отсутствуют; исследовался обычный код проекта.

## 1. A04: inventory перед удалением namespace

`terrainAtlasMapDirectories` получает также реальные настроенные mapRoots из CSettings. Сохранён прежний охват папок профиля/ресурсных источников, добавлены все настроенные roots. Inventory проходит все вложенные каталоги, включая скрытые файлы и MPR с регистром .MPR, без лимита каталога MCP10000. На Windows ошибки FindFirstFile/FindNextFile отличают недоступный каталог от пустого. Ссылки/нечитаемые каталоги и MPR, ошибка парсера или незавершённый inventory оставляют старый namespace.

Namespaces берутся из одного проверенного чтения каждой MPR, без прежнего непроверенного второго чтения. Перед публикацией очистки повторно проверяются inventory и текущая MPR. Причина отказа возвращается и пишется существующим post-save логом. Ошибка необязательной очистки не отменяет уже успешное сохранение.

## 2. A05: единое семейство при обрабатываемом отказе

Выбор файлов принадлежит команде, не общему механизму:

| Команда | Набор279, сохранённый R3 |
|---|---|
| Save при активном MOB | Только активный MOB и его группировка, если она изменилась |
| Save без активного MOB | Делегирует Save MPR, как279 |
| Save all MOB | Только dirty MOB и изменения их группировок |
| Save MPR | Только MPR/RES/ландшафтные companions и dirty world/neighbour MPR, без MOB |
| Save при dirty travel/CAM через saveMapWithTravel | Прежние travel/CAM companions, dirty landscape/world, активный dirty MOB и его изменённая группировка; не все MOB |
| Save MOB As / JSON export | Прежний выбранный MOB destination / JSON export; чужие MOB и MPR не добавляются |
| Save MPR As | Прежний MPR/атласный Save As; MOB не добавляются |
| MCP Save | Полный MPR/RES/world/все dirty MOB/model-catalog пакет через saveMapFamily, включая travel/CAM при наличии |

`saveMapCompanions` больше не сканирует m_aMob. Человек Save MPR оборачивает только ландшафтный набор в существующий cohort. Save all MOB готовит выбранные dirty MOB и общий model-catalog.json одним пакетом commitMapSaveBatch; Save активного MOB и MOB Save As пользуются тем же подготовителем с одним явно переданным MOB. Для группировок не создаётся/не переформатируется посторонний неизменённый каталог.

Старая проверка MOB выделена в CMob::prepareSaveBytes без изменения текста диагностики/отчёта. Однофайловая команда сохраняет прежний QProgressDialog («Saving <mob>», диапазон0..5), Save all — прежний «Saving all MOBs». Travel сохраняет прежнее «MOB не сохранён» с report.toText/ошибкой staged validation. Baseline/clean/provenance принимаются только после успеха всего выбранного набора. При отказе подготовленные MOB остаются dirty; уже опубликованная часть откатывается. Byte-identical файлы batch не заменяет; clean Save MPR с dirty MOB не добавляет записей.

Закрытие и autosave не перенаправлены в семейный Save: каждый close-ответ делегирует прежнему MOB/MPR writer; recovery autosave по-прежнему создаёт поколения отдельно от исходников и не очищает dirty. World/neighbour save pipeline отличается от279 только поставщиком полного A04 inventory.

## 3. A05: восстановление после смерти процесса

Новый журнал schema2 содержит прежний files/beforeSha256/afterSha256/backup плюс PID, EXE и версию. Жизнь batch защищена QLockFile lease; старые process-slot journals дополнительно защищены существующим process-N.lock. Время не является основанием отнять живой lease; PID не используется как единственное доказательство смерти.

На старте обычного редактора до загрузки окна обработчик проверяет известные journal-каталоги root и processes/N, не сканируя весь backup corpus. Hash/backup работа выполняется в worker. Для проверяемого неживого журнала:

- Все файлы before либо все after: файлы не меняются, журнал закрывается.
- Смесь: весь набор обязан совпасть с before/after; проверяются SHA всех before-backups. Владелец получает вопрос со списком файлов, описанием возврата и резервирования текущего состояния; по умолчанию «Нет».
- Отказ оставляет журнал и файлы; следующий оконный запуск спрашивает снова.
- Неизвестный SHA/нечитаемый файл оставляет **всё** семейство без изменений, журнал остаётся. До отката удерживаются межпроцессные MPR/MOB/RES и save-locks; после согласия повторно проверяются все SHA, создаются проверенные копии текущего состояния и current.json. Перед каждой заменой снова проверяется семейство.
- Headless не берёт recovery-locks, не заменяет файлы и не закрывает журналы; pending journal остаётся, предупреждение идёт в workflow.log и `get_context.saveRecoveryWarnings`. Оно сообщает о pending journal, без фонового ремонта.
- Старый корневой журнал без schema2 и без доказуемого process-slot владельца сохраняется для ручного разбора: назначить ему право заменять файлы безопасно нельзя. Повреждённые/неизвестные журналы также не запускают ремонт. Существующий retention продолжает закреплять backups из журналов.

### Старые журналы владельца

В этапе R2 read-only проверены `%LOCALAPPDATA%/EI Mapper Endurance/backups/map-save-recovery` и вложенные processes/*/map-save-recovery: **0 JSON-журналов**, повторная проверка перед отчётом также0. Эти каталоги не изменялись инструментами. В текущем состоянии при первом запуске280 отката по старым журналам не будет.

Если до запуска появится старый process-slot журнал, живой slot остаётся нетронутым; неживой проходит SHA-классификацию и вопрос. Файл, изменённый после старого сохранения, включая вручную восстановленный HDPack_Terrain.res, при неизвестном SHA блокирует откат всего семейства. Корневой legacy-журнал без owner lease остаётся для ручного разбора, без неожиданной замены.

### Nav64 и AI

Nav64 остаётся отдельным производным этапом: успешный mapSaved может поставить bake в очередь, MCP saved не ждёт его. При отказе пакета новый success-event не испускается; ранее существовавший .nav64 или уже запущенная отдельная работа не откатываются этим журналом. Встроенные байты AI-графа внутри MOB сохраняются/откатываются вместе с целым MOB; отдельные rebuild/persist графа и .grp в пакет не добавлены. Обычный Save не переписывает .grp. Это сохранение исходных lifecycle по поручению, а не подтверждение готовности Nav64.

## 4. Журнал расследования RES

После каждой охваченной атомарной RES-замены (batch, streamed transaction/member, byte-backed coordinator, legacy CResFile, copy/rollback) workflow.log получает RES write JSON: PID, абсолютный EXE, version, путь RES, before/after SHA256, bytes, mtime и namespacesAdded/Removed. Имена namespaces извлекаются из directory RES без декодирования текстурных payloads. Batch использует уже рассчитанные journal SHA; streamed пути — рассчитанные SHA. Дополнительные большие byte-backed audit hashes выполняются в worker.

Resource Browser observed external archive change дополнен известным before и новым after SHA/bytes/mtime. Начальное hash-наблюдение имеет отдельный worker, не задерживающий завершение browser index/publication. Неизвестный прежний SHA честно обозначается unknown. Определение чужого писателя не реализовано.

## Предыдущие проверки R2 и пределы доказательства

Все проверки выполнялись собственными процессами; итоговые проверки используют test-only замену writable QStandardPaths в памяти своего процесса и предусмотренный EI_MAPPER_LOCK_STORE_TESTING. Эти подстановки отсутствуют в staging. Файлы игры только читались; SHA живых zone1.mpr/zone1.mob/HDPack совпали с первоначальными копиями (evidence/live-source-readback.json).

| Сценарий | Результат |
|---|---|
| Production retireTerrainAtlasAfterSave на реальной MPR-копии, вложенная Maps/subdir/deeper/B.MPR с прежним namespace |279 ошибочно удаляет старые members;280 сохраняет. Это адресная проверка post-save production retirement, не полный повтор тяжёлого atlas-render pipeline |
| Отсутствующая папка и повреждённая MPR | Очистка запрещена, причина содержит путь |
| Injected отказ каждого из MPR/RES/MOB1/MOB2, retry |4 отказа: before побайтно;4 повтора: after |
| Реальный Windows handle без FILE_SHARE_DELETE на каждом файле |4 Atomic replacement failed с путём/Qt error; полный rollback, после освобождения handle retry успешен |
| TerminateProcess после каждой из4 замен и новый тестовый процесс recovery |3 смеси: headless/отказ без изменений, согласие before побайтно; после4-й замены all-after закрывает журнал без вопроса |
| All-before; неизвестный внешний SHA; повреждённый backup; ownerless legacy | All-before закрыт без записи файлов; остальные оставлены без замены/вопроса |
| Живой writer с удержанным lease, затем TerminateProcess | Живой пропущен; после смерти восстановлен |
| Копия current-state перед откатом | current.json и каждый backup побайтно сверены с соответствующим смешанным состоянием |
| Настоящий production qMain startup в headless и собственный stdio MCP get_context | Предупреждение получено; MPR/RES/MOB и journal остались прежними |
| R2 Save MPR/reopen (отклонённый scope) | Писал MPR и MOB; именно это расширение команды отменено R3. Текущая матрица ниже |
| Собственный production stdio bridge → revision → Save → poll | saved, MPR/MOB чистые, свежий reopen успешен; чужие endpoints не выбирались |
| Новый model-catalog + ошибка MPR → retry | MOB откатился, новый JSON удалён; повторно группа записана общим пакетом |
| RES audit и внешний watcher | PID/EXE/version/точные SHA/размеры/добавленный и удалённый ns проверены; watcher before/after SHA совпали |
| Существующие draftPublicationKeepsPathsAndRollsBackEveryStage и travelSaveBatchRollback |2 целевых теста; Qt total4 с init/cleanup,0 FAIL/0 SKIP |
| Существующий save_pipeline_check::transaction, только относящееся подмножество |20 PASS: streamed RES, file-backed batch rollback/retry, no-op, transient rollback |

Согласие/отказ в recovery тестируются callback на production recovery engine в новом процессе; текст вопроса проверяемый. Пользовательскую визуальную приёмку оконного QMessageBox не выполнял. Полный rebuild AI/Nav64, A01–A03, посторонние UI/рендер-сюиты и игровой запуск не выполнялись. Синтетический RES в fault-matrix малый; отдельно проверен file-backed RES8МиБ. Обычный Save zone1 использует копию большого HDPack, но height/script-only Save не переписывает весь пак.

### Историческое время R2 семейного Save (не показатель раздельной команды R3)

Одинаковый сценарий: подготовленная zone1 (MPR1,552,871 байт, MOB1,714,199 байт), правка высоты и комментария скрипта, каталоги уже готовы, одно сохранение и strict reopen. Измеряется Save, без загрузки ресурсов:

| Вариант | Save |
|---|---:|
|279 из собственного git archive, полностью изолированный прогон |5,511с|
|280 после переноса прогресса MOB в общий путь |5,506с|
|280 окончательный Release, с preflight каталога и проверкой clean modern-draft route |5,641с|
|280 через MCP, окончательный Release |5,569с|

Последний before/after: +0,130с (+2,4%). Единичные измерения под текущей нагрузкой; они не доказывают статистическое изменение скорости. Повторные после-значения показывают тот же порядок времени. Старые неуспешные замеры до готовности terrain catalog исключены из сравнения.

## Что удалено или изменило поведение — актуально для R3

**Набор команды человека, кнопки, сочетания, настройки и обычные progress/dialog titles возвращены к279.** Расширение человеческих Save/Save all/Save MPR до полного семейства удалено. Save As, close-вопросы, autosave и world/neighbour selection не расширяются.

Остались согласованные изменения A04/A05:

- Внутри выбранного набора публикация атомарна: в частности, Save all MOB и изменения model-catalog — один пакет; отказ следующего файла не оставляет сохранённый предыдущий MOB.
- Только MCP вместо прежних последовательных MPR→MOB сохраняет тот же полный набор одним saveMapFamily; отдельный второй Save all у агента удалён.
- Внешний конфликт/ошибка записи не перезаписываются молча; окно сохраняет текст проверки и получает путь/причину отказа/резервные копии. Валидационные отчёты279 не удалены.
- Очистка оставляет старый atlas при неполном inventory. На старте после прерванной записи есть согласие на откат и read-only headless warning. Обычный Save не получает новый вопрос.
- RES write/external-change diagnostics и служебные journal lease/schema сохранены. Форматы игровых файлов, backup policy/retention, отдельные Nav64/AI lifecycle не меняются.

## Сборка и состав — исторический R2

Release из git archive tree `deab9471e1d34c0ec3ca9f9775fe92425e43c822`, исходная программа без незакоммиченных правок владельца. Использован8.3 путь C:/GOGGAM~1/maped/EI-MAP~2, Qt5.12.12/MinGW7.3, canonical build_release с последующей адресной пересборкой изменённых единиц. Runtime-компоненты и MCP bridge собраны wrapper. subst не использовался, итоговый список пуст.

Выдаваемый staging: `build-staging-280-save-integrity-20261105-r2`. Более ранний staging заменён в выдаче R2 после проверки поведения clean modern draft; он не устанавливался. Raw/staged EXE SHA256 `932e3b152b51ed1b41a7ea98c4ddbb6e5fe86c9a8c5b7c018f8535048ddf485f`; оба --version280/exit0. BUILD_INFO.txt явно Version280 и точный tested tree, без прежнего273. Финализатор/внутренний validator PASS:192→192 managed paths,0 потерь,0 удалённых файлов. Финализатор запускался только на собственных staging, не на установке.

`work/save-integrity-280-20261105/evidence/compiled-sources.json` связывает нормализованные байты изменённого production-кода/тестов с проверенным archive. Отчёт и окончательный статус ROADMAP не меняют программу. Полный SHA scoped-коммита main сообщается отдельно после создания; самоописание SHA внутри этого же коммита не используется.

## Сохранность и нарушения тестовой изоляции

10 исходных protected inputs сохранены: AGENTS, незакоммиченные строки ROADMAP, отчёт277, landscape/trace/neighbor, owner-hunks view.cpp, mob_load_pipeline_check.cpp, QM, .mcp.json. Для view.cpp обратное применение только нашего patch даёт исходные байты; исходный ROADMAP остаётся точным suffix, включая смешанные окончания строк. Чужие файлы не входят в scoped-коммит. Установка279 проверялась только чтением accepted manifest для staging.

Первоначальные тесты **не полностью соблюли запреты изоляции**:

1. Windows QStandardPaths проигнорировал LOCALAPPDATA. Создан/обнаружен один служебный файл тестового приложения: `%LOCALAPPDATA%/Ataman isolated save280/save-integrity-check/script-workspaces/8d59b3dc358bf5ca6436e8898ce4bc2bd50e536484028301bbb6336b19689783/zone1.lua`,58,963 байта, SHA256 `420e6e5b0f9dd36872fde0a77a8b975e964bf8353cec3fa344965a71e356a663`. Его не удалял: запись/очистка AppData запрещена поручением. Затем введён process-only QStandardPaths shim до создания Qt app.
2. Первые прогоны использовали штатный ProgramData lock resolver: переменная EI_MAPPER_LOCKS_DIR им не поддерживается. Затем тесты перелинкованы с предусмотренным EI_MAPPER_LOCK_STORE_TESTING/EI_MAPPER_TEST_LOCK_ROOT внутри work. Read-only инвентарь не нашёл оставшихся lock-файлов с именем тестового приложения; не выполнялась очистка ProgramData.
3. Первый standalone watcher test использовал произвольное имя RES и мог показать штатный модальный вопрос без общего hidden filter. Он остановлен по точному executable path и аргументу observed (собственный PID13708); визуальное состояние рабочего стола не снималось. Повторный тест поставил общий hidden filter и headless property, прошёл без вопроса.

Чужие процессы не завершались. Эти инциденты не замаскированы как соблюдение запретов. Итоговые проверки выполнялись после исправления соответствующей изоляции. Ничего не записывалось в C:/GOG Games/PZ; совпадение живых исходных SHA с первоначальными fixture подтверждено отдельно.

## Очистка и следующий шаг

Адресные логи/receipts сохранены в `work/save-integrity-280-20261105/evidence`. Автоматическая проверка отклонила scoped Remove-Item тестовых каталогов с причиной `blocked by policy`; команда не запускалась, альтернативного удаления не было. Остались7,338 файлов /2,617,995,777 байт, включая собственный старый staging и private fixtures. `work/save-integrity-280-20261105/cleanup-plan.json` содержит точные roots/files/SHA, защищает итоговый R2 и evidence и отмечает deletionExecuted=false. Это предусмотренный поручением fallback, а не PASS очистки. Единственный файл в AppData исключён из автоматической очистки запретом владельца.

Следующий этап — решение владельца об установке R2. В этой задаче STOP BEFORE INSTALLATION и запрет push сохраняются.

## Исторический STOP R2 по HEAD (снят новым поручением для R3)

Перед scoped-коммитом обнаружен новый main HEAD `761f52765835aaf1922d8a9ce24e7f1270655dc3` вместо заданного `401bd0b8d5f66e3bcd1f3a8252f12e8a1972e994`. Новый коммит добавляет только docs/validation/ATAMAN_REBUILD64_RENDERER_STUDY_20261105.md (221 строка); это постороннее параллельное изменение. В соответствии с точным условием владельца «иначе стоп» интеграция не выполнялась. Проверка сработала до изменения основного Git index; staged files0, собственного коммита нет. Программа280, тесты и finalized staging R2 готовы, но задача не закрыта: остаётся разрешённый scoped-коммит после решения владельца о новом HEAD. Установка/push не выполнялись. Чужой документ сохранён. На момент передачи собственных процессов внутри task-root0, subst пуст.

## R3 — исправление по приёмке директора (2026-11-05)

### Что было неверно и что возвращено

R2 ошибочно отправлял saveActiveMob/saveAllMob/saveLand в saveMapFamily и автоматически подбирал dirty MOB внутри saveMapCompanions. Это записывало пробный рельеф по Save MOB и MOB по Save MPR. Результат директора «НЕ ПРИНЯТО» принят; технические PASS R2 не означали сохранение пользовательского сценария.

R3 вернул явные наборы таблицы выше. Старые bodies MainWindow Save/Save all/Save MPR handlers, MOB/MPR Save As, onExit/unload, performAutosave/prepareLandscapeRecoveryTask не расширены. Проверены git show761f5276 и git diff761f5276 по source/app и view*.cpp. В scope-audit.json15 неизменённых функций/реализаций, отдельно2 файла world/atlas, где отличие279 — только A04 provider. Owner trace/neighbor hunks в обзоре отделены, не коммитируются. map_save_batch.cpp/.h и main.cpp startup recovery побайтно равны R2 с нормализацией окончаний строк; прочие A05-части не переделывались.

### Новые относящиеся проверки

На копии zone1 одновременно изменены MPR и два реальных MOB. Отдельные группировки обоих MOB используют один общий тестовый каталог.

| Проверка R3 | Результат |
|---|---|
| Save | Только активный MOB/group record; MPR/RES/второй MOB прежние, MPR и второй MOB dirty |
| Save all MOB | Оба dirty MOB и их group records; пробный MPR/RES прежние, MPR dirty |
| Save MPR | MPR сохранён; оба MOB прежние и dirty; clean повтор не меняет MPR mtime/каталог |
| Реальный собственный stdio MCP Save с revision/poll | MPR и оба MOB сохранены и clean |
| Сбой commit второго MOB в Save all | Оба MOB и каталог before, MPR не записан; dirty сохранены; ошибка содержит second.mob; retry успешно сохраняет только MOB-набор |
| Save с dirty travel | Прежний travel/MPR/активный MOB; второй MOB прежний и dirty |
| Явный agent travel branch | Travel/MPR/все dirty MOB, весь пакет clean |
| Ошибка MOB validation | Человеку доступны прежний объясняющий текст и ERROR finding parser report |
| Recovery autosave | Поколения созданы отдельно; исходники и dirty не изменились |
| Close MOB → Save | Только закрываемый MOB, другой dirty MOB и пробный рельеф не сохранены |
| MOB Save As | Только выбранный MOB destination/group record; исходный MOB и пробный MPR прежние |
| TerminateProcess после первой MOB-замены в Save all | Новый процесс recovery: MOB-набор before побайтно; MPR, не входящая в журнал, неизменна; журнал закрыт |
| Повторное чтение после recovery | CMprDocument и два CMobDocument load/serialize byte-identical,0 parser errors |

Native baseline279:23 PASS; R3 command matrix60 PASS; короткий recovery strict reopen4 PASS с guard. Это счётчики assertions, не60 независимых сценариев. Baseline executable — собственная git-archive сборка279; код source/ в761f5276 равен279. Остальные старые успешные A04/A05 проверки сохранены по неизменности соответствующего кода, не выдаются за повторный запуск.

Время **одной команды Save активного MOB**, с dirty MPR и вторым MOB, после подготовки каталогов:279 **6478мс**, R3 **6374мс**. Разница −104мс (−1,6%); в этом парном замере замедления нет. Загрузка ресурсов в интервал не входит. Это единичные измерения; прежние R2 замеры полного семейства выше не сравниваются с этой раздельной командой.

### Изоляция R3

Hidden filter установлен глобально до первого QWidget, Qt native dialogs отключены, Windows crash dialogs отключены только в собственном процессе. QStandardPaths writableLocation подменён в памяти test process до QApplication; test-only lock store направлен в work через EI_MAPPER_LOCK_STORE_TESTING. Все три resource roots и options/settings/backup/discovery/temp/known-folders каталоги — private work. Использованы копии RES, без подстановки live map roots. В AppData/ProgramData R3 ничего не записывал; к оставленному R2 Lua-файлу не обращался и не удалял его. Подстановки не входят в production/staging. Computer-use, screenshots, буфер, установка и мосты владельца не использовались.

### Выдача R3 и Git

Проверенный program archive tree `65c16a9ab464a26c2d88aded9d1330cb7eeaa0df`; база main `761f52765835aaf1922d8a9ce24e7f1270655dc3`. Новое разрешение владельца заменяет прежний STOP по exact HEAD: последующие docs-only commits допустимы, source changes вызывают STOP. Перед коммитом проверяются новый HEAD, index и index.lock; lock не удаляется.

Fresh Release R3 из git archive в собственном новом build root, Qt5.12.12/MinGW7.3,8.3 путь без subst/junction. Canonical wrapper собрал editor/MCP/runtime. Выдаваемый staging **build-staging-280-save-integrity-20261105-r3**. Raw/staged EXE SHA256 `c2576c5d49b754a37803de71c60d7f808c57f71f43efd2e0a31c6fd7ae13d72b`, оба --version280/exit0. BUILD_INFO: Version280/RevisionR3/точный archive tree. Финализатор PASS:192→192 managed paths,0 потерь/0 удалений. Ранние R2 staging исторические, не выдаются и не устанавливались.

Доказательства: work/save-integrity-280-20261105-r3/{scope-audit.json,preservation.json,package-identity.json,finalize.log,cases/*/scope*.log}.10 protected inputs сохранены, owner view-hunks и исходный ROADMAP suffix побайтно проверены. Scoped-коммит включает реализацию280/R3, тесты и отчёт, без owner files/hunks; его полный SHA сообщается после создания. STOP BEFORE INSTALLATION, push не разрешён и не выполняется.

Для private test copies подготовлен отдельный R3 cleanup-plan.json; ранее отклонённое политикой рекурсивное удаление не повторяется обходным способом. Это не PASS очистки. R2 cleanup-plan и старый AppData файл остаются под прежними ограничениями.
