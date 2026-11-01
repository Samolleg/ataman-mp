# EI-LOCKS-PROGRAMDATA-271 — 2026-11-01

База main `25427d62f34720074c22d4ad1687792d2b64bc7d`, установленная270.
Версия271 свободна по VERSION/CHANGELOG. STOP BEFORE INSTALLATION; без push.

Хранилище перенесено из данных в `ProgramData/EI Mapper Ataman/locks`.
Корень определяется `SHGetKnownFolderPath(FOLDERID_ProgramData)`. Физический
канонический SHA-256 (включая junction), map/resource-суффиксы, MPR/MOB-пара,
общие leases, QLockFile dead-PID и отсутствие срока жизни живых leases сохранены.
Users (SID S-1-5-32-545) получает наследуемые Modify и DeleteChild при создании
папки. Существующий ACL не исправляется; проверяются права самой папки и
наследуемые права lock-файлов, при отказе русская ошибка с путём новой папки.
WRITE_DAC/WRITE_OWNER/FullControl группе не выдаются.

Старые `.mapper-service/locks/<hash>` и sidecar-имена проверяются без tryLock,
создания, изменения или удаления файлов. Живой PID блокирует; мёртвый
игнорируется. Недоступный PID/нечитаемый старый замок не считается мёртвым.
Для одновременной работы все копии нужно обновить до271: старые версии не
знают о ProgramData и могут начать работу уже после проверки старых мест.
Старую `.mapper-service` автоматически не удаляем. В AGENTS.md место этих
замков не указано; файл не менялся. Описание225 дополнено текущим местом,
исторические результаты не переписаны.

Unit: 15 PASS/0 FAIL/0 SKIP. Проверены межпроцессный отказ на той же карте,
разные карты/RES, другое приложение/установка, MPR/MOB и общий lease,
аварийный PID, живой lock с датой десятилетней давности, старые места обоих
типов и неизменность мёртвых файлов, junction/ещё не созданная цель,
недоступный store и отказ существующего ACL без его изменения. PID старого
замка с другой машины или вне диапазона DWORD не считается локальным мёртвым
владельцем; оба случая отдельно проверены.
Первый тест старения попытался открыть занятый QLockFile на запись данных;
исправлена только обвязка на FILE_WRITE_ATTRIBUTES, финальный тест PASS.

Тестовый корень доступен только при EI_MAPPER_LOCK_STORE_TESTING в отдельном
process_edit_lock_store.cpp. Production Release этой точки не содержит.
Тестовый маппер линкует те же production-объекты, заменяя только объект store;
регресс225 также использует тестовый объект. Установленный EXE не запускается.
Production-объект store отдельно проверен на чтение: путь
`C:\ProgramData\EI Mapper Ataman\locks`, даже при посторонних значениях
переменных ProgramData и EI_MAPPER_TEST_LOCK_ROOT; файлы не создавались.
В поставочном EXE строки тестовой переменной нет.

ACL: native prepareDirectory на новой временной подпапке настоящего ProgramData
успешен. Проверены SID Users, Modify папки и наследуемый Modify созданного файла
(0x1301ff, без WRITE_DAC/WRITE_OWNER). Временные файл и папки удалены,
основная ProgramData/EI Mapper Ataman не создавалась и не менялась.
Квитанция: work/locks-programdata-271-20261101/programdata-acl.json.

Короткий production-регресс225: **40 PASS/0 FAIL**, exit0,40.29с на итоговом коде.
Два stdio-моста одновременно запускают один невидимый фон; обычный редактор
моделируется скрытым MainWindow с теми же production-объектами в другом
процессе. A остаётся dirty/побайтно прежней во время редактирования и Save B;
обе стороны отказывают на занятой карте, затем A независимо сохраняется.
Фон закрывается через мост; после open/save/close в данных нет .mapper-service.
Изолированный RES проверяет слияние независимых членов и отказ устаревшей
записи того же члена. Скрытый режим окон сохранён; computer-use не использовался.
Первый прогон38 PASS/1 FAIL сравнивал options до/после штатного Save обычного
редактора; исправлена область проверки только фоновой операции, production
не менялся. Native stdout/receipt и исходный FAIL сохранены в native-r1/native-r2;
окончательный повтор после защиты remote/invalid PID — в native-final.
Смежный `minimap_export_tests`: **21 PASS/0 FAIL/0 SKIP**, включая resource/save
lock, конфликты, атомарный отказ и no-op RES. Автономный тест без переменных
окружения сам создаёт временный корень, не обращаясь к реальному ProgramData.

Release и оба runtime: exit0, свежий shadow-build из git archive tree
`ec42b8155295802cc3c3266c7a1cb04c254effc9`. В сборку не попали незакоммиченные
правки владельца. Для SDK Known Folder API задан Vista+ только в новом .cpp;
исправлены первоначальный compile отказ старого SDK target и двойное включение
нового .cpp (проект уже использует рекурсивный список SOURCES).
Коррекции списка и защиты remote/invalid PID получены отдельными git archive,
затронутые production/test-объекты пересобраны в свежем build271.
Итоговый test tree `ac75af8622213574a5aec68bb99878e2a2b74f2b` отличается от
Release только обвязкой тестов. Все попытки и итоговый успешный wrapper
сохранены в release-build*.log; окончательный — release-build-verified.log.

Raw и staged CLI: **271**, exit0. SHA четырёх исполняемых компонентов совпали.
Свежий staging: `build-staging-271-locks-programdata-20261101`.
Финализатор и встроенный validate_distribution PASS: **188 прежних путей,
189 текущих,0 потерь**, добавлен только этот отчёт. Для итоговых metadata
после коммита финализатор повторяется: собственная staging ещё не передана,
не запускалась после финализации и не содержит пользовательских данных.

Тестовые копии карт/RES, test app/options/settings/AppData/backups обоих native
прогонов (включая окончательный повтор) удалены после финального PASS. SHA/размер/mtime **198** защищённых
файлов (включая входные данные и управляемые файлы установленной270) сохранены;
7 посторонних файлов checkout, включая AGENTS и QM, сохранили SHA.
Квитанции cleanup-result.json и cleanup-result-final.json; приватные пути/receipts остаются только в work/.
Другой Codex завершил свой scoped-коммит4d3aca863f720db0f41ee8f1254befe736b80895;
его файлы не изменялись и в коммит271 не входят. Его изменения не затрагивают
production source/ei_maper.pro/VERSION/tests; программа собрана от прежней270.
ROADMAP переносится только собственным пунктом поверх актуального HEAD.

Установка270, данные C:\GOG Games\PZ, буфер, приоритет и процессы владельца
не изменялись. Интерактивный/установленный маппер и игра не запускались.
STOP BEFORE INSTALLATION; без push. Evidence: work/locks-programdata-271-20261101/.

Основания API: [Windows file ACL](https://learn.microsoft.com/en-us/windows/win32/fileio/file-security-and-access-rights),
[Qt5.12.12 QLockFile Windows](https://github.com/qt/qtbase/blob/v5.12.12/src/corelib/io/qlockfile_win.cpp).
