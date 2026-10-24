# EI-MCP-READ-218 — фундамент MCP, только чтение

Владелец2026-10-24; main, база задачи
`7b6c153baa672e8bac0794e87a7002d0600d4a65` /217, результат218.
База131 и штатные конвейеры209–216 сохранены; RecipeBaker/MixedEdge не возвращены.
Подготовленная218 принята владельцем и установлена2026-10-24 по отдельному «ставь»; запись установки ниже. Push отсутствует.

## Архитектура и границы

Qt/C++ без нового production-тулчейна. MainWindow владеет CAiAgentServer и доком
«ИИ-агент». Человек выбирает Выкл/Только чтение; Правка недоступна, режим при
следующем запуске сбрасывается. QLocalServer/UserAccessOption, текущий пользователь,
защищённая Windows DACL discovery-каталога, случайные UUID pipe/session secret.
TCP/network listener отсутствует. EIMapperMcp.exe — отдельный stdio JSON-RPC мост;
редактор им не запускается, режим не меняется. Секрет не выдаётся через MCP и не
записывается в конфиги/события. Журнал панели ограничен200 строками.

MCP2026-07-28: server/discover, per-request metadata, resultType, обязательные
cacheScope/ttlMs в cacheable results; совместимость initialize2025-11-25,
2025-06-18,2024-11-05.14 tools,2 resources. ID внутреннего канала независимы
от MCP ID; несоответствующие ответы отбрасываются, таймаут2с уничтожает канал.
Следующий запрос требует reconnect, поэтому поздний ответ не рассинхронизирует мост.

Read adapter не экспортирует изменяющие методы CView. Снимок высот и поиск БД
работают с копией/immutable snapshot вне GUI. Каталог/разбор MPR/MOB — отдельный
read-worker без QApplication, settings, recovery и OpenGL. Он ограничен512 МиБ
Windows Job,64 МиБ на файл,4096 секторами,10000 файлами каталога и сроком30с.
Максимум2 параллельных чтения/8 операций, результаты живут60с; отмена и Off
освобождают результаты и убивают только собственные worker-процессы.
Предел страницы200, фрагмента4096, ответа512 КиБ, кадра1 МиБ.

В normal closeEvent211 stop вызывается ДО prepareForApplicationShutdown/delete
CView. Контроллер использует QPointer; отменяемые фоновые lambda не содержат
GUI-указателей. Fallback destructor/destroyed также выключают сервер.
Дисковые карты никогда не открываются в редакторе и не сохраняются.

Руководство, подключение обоих клиентов, перечень инструментов/параметров,
координаты, ограничения и план применения команд через preview/одну Undo/Save209–216:
[AI_AGENT_MCP.md](../AI_AGENT_MCP.md).

## Сквозная функциональная проверка

[tests/ai_agent_check.cpp](../../tests/ai_agent_check.cpp) связывается с настоящими
production Release-объектами218; заменён только runAssemblyCatalogCheck. Настоящий
MainWindow/QGLWidget скрыт через WA_DontShowOnScreen. Используется настоящий
EIMapperMcp.exe, stdio, QLocalSocket/QLocalServer и настоящие методы загрузки карт.

Финальный [run7](../../build-mcp-read-218/run7/process.json): **540 PASS,0 FAIL,
exit0,12.625с**. Число учитывает повторяемые RPC envelope/id assertions и опросы;
это не540 разных пользовательских сценариев. [Полный stdout](../../build-mcp-read-218/run7/stdout.txt).

- Default Off, отсутствующее обнаружение, недоступный Edit, никакого автозапуска;
  включение только через ComboBox панели, отсутствие секрета в перечислении/журнале.
- Частичный/склеенный кадр, неверный тип JSON, размер, ограничения аргументов,
  отсутствие save/mutation tool, сохранение строкового MCP ID; modern discovery,
  legacy initialize, ошибка неизвестной версии.
- Реальная копия zone1 MPR/MOB, соседние карты и private world-map-layouts.
  Контекст deja-vu/218, активные документы, размеры/dirty/соседи; пути соседей
  подтверждены внутри private fixture.
- Высоты всей карты (min/max/mean) и ограниченная выборка: значения совпадают с
  live CTerrainEditModel; packed paint совпадает с его текущими тайлами;
  реальная палитра, объектные фильтры/страницы/чтение существующего выделения,
  поиск реальных monsters/items/prototypes БД.
- Каталог profile maps и фоновое чтение zone2 MPR/MOB с высотами/покраской/объектами
  без смены активной zone1; отказ `../`, отмена, предел8 результатов и удаление
  устаревших операций. Dirty/Undo index сохраняются всеми чтениями.
- Настоящий PNG: ограничение размеров/байт, отдельный framebuffer QGLWidget,
  просмотрен [снимок окна](../../build-mcp-read-218/run7/window.png) и
  [исходный framebuffer](../../build-mcp-read-218/run7/framebuffer.png).
- Реальный UI настройки Claude: существующий изолированный .mcp.json, полный
  confirm, сохранение чужого сервера/ключа, точная резервная копия. Cancel не
  меняет файл и не создаёт Codex config. CLI/UI не касались конфигов владельца.
- Подставной локальный peer отвечает чужим ID, затем правильным; следующий ответ
  намеренно опаздывает за2с. Мост отклоняет чужой ID, закрывает просроченный канал,
  не принимает поздний ответ и успешно переподключается.
- Off разрывает подключение, отменяет worker, удаляет descriptor; re-enable
  меняет секрет. Close с подключённым агентом и worker удаляет вид до выхода,
  убирает сервер/discovery, запись211 `Shutdown complete status:clean`, exit0.

Все исходные MPR/MOB и скопированные private входы после финального прогона
побайтно сохранены: receipt process.json. Отдельная private portable-структура
содержит только fixture marker вместо game.exe; игра и старые мапперы не запускались.
Разрешённые resource roots/эталон читаются из игры/установки; записи только в
private fixture/settings/backups/temp и конфиги проверки. Приоритет не менялся,
буфер обмена не использовался.

Предварительные прогоны сохранены и не объявляются PASS: run1 — неполный fixture
без RES базового профиля; run2 — ресурсы профиля не подключены к renderer после
автоконфигурации; run3 — в private mod отсутствовала основная database.res;
run4 — драйвер остановился на втором confirm после overwrite-dialog, собственный
тестовый процесс остановлен. Run5/6 прошли ядро; исследование чёрного native PNG
выявило необходимость рисовать framebuffer после QWidget::grab и читать без alpha.
Исправление включено в финальный run7, снимок с настоящей сценой просмотрен.

## Независимые стандартные клиенты

Официальный Python MCP SDK **2.3.0**, установлен только в
`build-mcp-read-218/sdk`, не нужен production-комплекту.
[tests/ai_agent_sdk_check.py](../../tests/ai_agent_sdk_check.py): modern discover и
legacy initialize,14 tools, JSON Schema2020-12,2 resources/read, ping, отказ
неподключённого чтения — PASS. [Результат](../../build-mcp-read-218/sdk-run3/result.json).
Первый SDK-прогон выявил недостающие ttlMs/cacheScope modern tools/list; исправлено.
Второй обнаружил ошибочное ожидание15 вместо14 tools в тесте; production не менялся.

Настоящий **Codex CLI0.164.0** загружен из официального OpenAI release в private
каталог. Изолированный CODEX_HOME: mcp add/get/list PASS. Через его app-server
вызван сгенерированный этим же CLI метод mcpServerStatus/list: он выполнил MCP
handshake с мостом и получил serverInfo218,14 tools и2 resources.
[Ответ Codex](../../build-mcp-read-218/clients-run/codex-mcp-status.json).

Настоящий **Claude Code2.1.310** из официального npm-пакета, отдельный
CLAUDE_CONFIG_DIR: project mcp add/list PASS. Проектная запись требует подтверждения
доверия (ожидаемый Pending approval); отдельная тестовая user-запись того же моста
прошла MCP health-check **Connected**. [Лог обоих клиентов](../../build-mcp-read-218/client-handshake.log).
Модельные запросы/генерация не выполнялись, аккаунты/токены/глобальные настройки
не менялись. Секрет маппера не передавался в конфиги.

## Сборка, сохранность и передача

Production Release218 собран штатным scripts/build_release.cmd (Qt5.12.12,
MinGW7.3 x64; штатные x86 runtime-компоненты пересобраны) из **git archive tree
`cb6f77e9da9be8bf4f7bb02ed7ae412c828f8208`**. Каталог исходников/выходов:
`build-mcp-read-218/source/build-release-218`.
Последующие изменения инструкции/тестов/отчёта и удаление пустой строки build-script
не меняют production C++/ресурсы. Их совпадение фиксируется source-identity receipt.

Передача: свежий `build-mcp-read-218/build-staging-218-mcp-read`, bridge рядом с
редактором, QtNetwork/compiler/runtime DLL, VERSION218, один changelist,
инструкция и этот отчёт. Raw/staged EXE SHA совпадают; --version обоих218;
finalizer и manifest coverage/hash validation — в
[stage-receipt.json](../../build-mcp-read-218/stage-receipt.json).
После финализации комплект не запускался.

Сохранность132 managed-файлов установленной217 подтверждается SHA до/после;
чужое тело ROADMAP побайтно сохранено и исключено из scoped-коммита.
Commit identity: Codex <codex@localhost>, co-author по поручению владельца.
Итоговый полный SHA находится в
[commit-receipt.json](../../build-mcp-read-218/commit-receipt.json) и передаче владельцу.
На этапе подготовки не было ни установки, ни push, ни удаления чужих откатов/артефактов.

Ограничения: только чтение; изменение выделения/камеры, рельеф, покраска, создание
и сохранение через MCP не предоставлены. Прямой model-driven диалог владельца с
живым маппером и его ручная приёмка остаются отдельно; проверены реальные клиенты
на уровне MCP и полный data-path настоящего редактора через мост на копиях.
Большие/повреждённые карты могут получить явный отказ по лимитам. Новое ядро
переходов и этап219+ не реализованы в218.


## Установка218 — 2026-10-24

После принятия218 владелец явно поручил «ставь». Без пересборки установлен
проверенный staging `build-mcp-read-218/build-staging-218-mcp-read`, source
`042ae86f8c9a55e57121f153d279b1b91ca121c4`, в постоянную папку
`artifacts/EI-Mapper-Ataman-2-first-test` по docs/TEST_BUILD_HANDOFF.md.

До замены подготовлен `artifacts/rollback217-to218-mcp-read-20261024`: все132
программных файла217, прежний SHA256SUMS.txt, rollback-state.json и rollback.ps1.
Хэши резервных файлов/manifest, синтаксис скрипта и его VerifyOnly прошли;
`ROLLBACK_VERIFIED_217`. Откат не запускался на запись, предыдущие откаты сохранены.

Перед резервированием и непосредственно перед заменой проверены процессы:
EIMapperEndurance и компоненты постоянной установки не работали; процессы не
останавливались. По manifest заменены7 существующих файлов, добавлены3
(EIMapperMcp.exe, инструкция MCP, отчёт218), удалений0. Использована проверенная
временная копия и атомарная замена каждого файла; новый manifest установлен последним.

Только порученные проверки установки:

- Установленный EXE: `EI Mapper Endurance Edition 218`, exit0. Запрос --version
  выполнялся с отдельными cwd/settings/AppData/temp/backups в install-identity-218.
- Все135 управляемых файлов установки совпали по SHA-256 с manifest staging.
- Все18639 пользовательских файлов вне manifest сохранили набор путей и SHA-256:
  настройки, карты, журналы, сохранения, кэши и другие данные не заменялись.

Receipt: [install-receipt.json](../../build-mcp-read-218/install-receipt.json),
[проверка отката](../../build-mcp-read-218/rollback-verified.json),
[установленная версия](../../build-mcp-read-218/installed-version.txt).
Финализатор на установке не запускался. Функциональные тесты/пересборка не
повторялись; интерактивный маппер, игра и мост не запускались. Реальные конфиги
Codex/Claude не менялись, регистрация MCP не выполнялась. Прежние/чужие откаты
и артефакты сохранены, push отсутствует. Чужая правка ROADMAP сохранена отдельно
от scoped-коммита записи установки.
