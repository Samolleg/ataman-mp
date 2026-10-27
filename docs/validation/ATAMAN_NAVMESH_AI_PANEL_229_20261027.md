# EI-NAVMESH-AI-PANEL-229 — 2026-10-27

## Установка принятой229 — 2026-10-27

**УСТАНОВЛЕНА229. Разрешение владельца «ставь» снимает прежний STOP.** Принятый main `e432f0e49387acfdca0a91c19977868f092cd718`; Release `086a99a2ccebf1f9840cea73873bd1e552df49f5`. Без пересборки: канонический `packaging/install_program.ps1`, staging `build-ai-panel-229/build-staging-229-ai-panel`. Его копии tools/install_program.ps1, program_files.ps1, rollback.ps1 совпадают с каноническими после нормализации CRLF/LF.

- Постоянная `artifacts/EI-Mapper-Ataman-2-first-test`: `EI Mapper Endurance Edition 229`, exit0. Все176 управляемых SHA-256 и SHA256SUMS.txt совпали со staging:8 замен,0 добавлений/удалений.
- Откат `artifacts/rollback228-to229-ai-panel-20261027`: проверен созданным установщиком `rollback.ps1 -VerifyOnly`, `ROLLBACK_VERIFIED`; восстановление не запускалось. Чужие/прежние откаты не удалялись.
- Все18919 пользовательских файлов сохранили набор путей, SHA256, размер и mtime. Служебная .ataman-retired.json учитывается отдельно от пользовательских данных.
- До замены отсутствовали любые EIMapperEndurance (включая фон/runtime host) и игра. Ни один процесс не завершался. Исходные11 EIMapperMcp живы с теми же PID/creation time:12676,28628,32416,1364,15800,5076,18608,20272,12096,11204,22200.
- Занят и отодвинут только `EIMapperMcp.exe` → `EIMapperMcp.exe.old-ataman-9a9c5e7c7e8244ff91d63169042eae5c`, старый SHA256 `ECBA463FA755F200F2BD567C14C7C8AF41819B267091CC0BB40FF056F6913662`. Канонический новый EIMapperMcp.exe: `D2F007284ACED2C1F3E823DC9C94F4D229779E3A4B63C58420468E8B8FC4CFE4`.
- Прежний занятый `EIMapperMcp.exe.old-ataman-5f75a1e653c24df0a8d686beb087a980` сохранён (SHA256 `61ED431F7613FF994B536EE668F9880843B9FFCCE182FD0A32F0F9DB5E703D7F`); очистка установщика:removed0/retained1. Это полный перечень .old-* из служебного реестра после установки.
- Регистрации ~/.codex/config.toml и .mcp.json не изменялись. Финализатор на установке, computer-use, рабочий стол, видимые окна, интерактивный маппер, игра и push не запускались. Дополнительные функциональные проверки не выполнялись по поручению владельца.
- Квитанции: `build-ai-panel-229/install-229-receipt.json`, `install-229.log`, `install-229-rollback.log`, `install-229-bridges-before.json`, `install-229-users-before.json`, `installed-229-version.txt`. Установка завершена; текущие мосты продолжают использовать старый mapped image до штатного reconnect владельца.

Ниже сохранена история подготовки до разрешения установки.


## Результат и идентичность

**VERIFIED / PREPARED229. СТОП ПЕРЕД УСТАНОВКОЙ. Push не выполнялся.**

- База: main `86b950a86a9ca7e32a55fe5928f5f073095df2d2`, VERSION228. Номер229 сохранён для этого же исправления. База131, старый AI-граф MOB/.grp и vendor19/lock сохранены.
- Источник Release: `086a99a2ccebf1f9840cea73873bd1e552df49f5`, main. Последующие коммиты меняют только harness/отчёт/карту, не production-код и не передаваемые бинарные файлы.
- Автор/коммитер: Codex <codex@localhost>; трейлер `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Постоянная установка остаётся228. Свежий staging: `build-ai-panel-229/build-staging-229-ai-panel` относительно корня проекта.

Причина невидимой кнопки228: MainWindow вызывал `createNavmeshDock()` до `CView::attach()`, а `initAiGraphPreviewWidget()` вызывался только внутри attach. Условие `if(m_aiGraphPreviewWidget)` было ложным: кнопка вообще не создавалась. Старые тесты отделяли CView от MainWindow и не проверяли видимость кнопки.

Теперь Navmesh создаётся в самой панели «Навигация ИИ». Сверху выбор «Старый AI-граф» / «Navmesh», общий показ по H и сворачивание. Старый режим сохраняет слои0–7, источник, пересчёт, сравнение и сохранение/экспорт. Navmesh: восемь профилей с русскими подписями, имя/алгоритм/дата сетки, приблизительная актуальность, заливка/контуры, узкие места/острова, запекание карты/мода, прогресс, отмена и сворачиваемый журнал. Запекатель по умолчанию ищется в Engine64; другой выбранный файл запоминается. Отдельный док, пустое поле пути и неактивная .navsrc убраны; меню дублирует вход в ту же панель. Физическая H работает и при русской раскладке.

## Проверка и снимки вне экрана

Последнее поручение запрещает computer-use, управление рабочим столом, захват его экрана и видимые окна. Окончательный прогон соблюдает это: production MainWindow/CView остаются в штатной иерархии внутри отдельного тестового процесса. AppData/LocalAppData/User/TEMP/INI/автоматизация/бэкапы и игровой профиль изолированы. Все Qt top-level, включая диалоги, получают **WA_DontShowOnScreen + WA_ShowWithoutActivating до показа**; вызовов move/raise/activateWindow нет. H подаётся только QTest внутри процесса. В каждой точке снимка проверено, что MainWindow остаётся вне экрана и неактивен.

Использован Qt Windows с настоящим OpenGL-контекстом скрытого QGLWidget. Панель и production MainWindow сняты через `QWidget::grab`, сцена — через **собственный `QGLWidget::grabFrameBuffer` этого процесса**. На Windows этот native GL-child отделён от QWidget grab: итоговый кадр совмещает эти реальные пиксели в фактической геометрии MainWindow, сохраняя grab видимых дочерних панелей. Это явно составной внеэкранный кадр, не захват рабочего стола. Для каждой точки сохранены отдельно `*-panel.png`, `*-scene.png`, `*-mainwindow-raw.png`, `*-mainwindow.png`, JSON геометрии и вырезка сцены с панелью. Диагностика Qt о swapBuffers у non-exposed окна ожидаема для этого режима; framebuffer получен, заливка/контуры меняют реальные пиксели.

Пути относительно корня проекта:

| Сценарий | Снимок сцены и панели production MainWindow |
| --- | --- |
| Старый встроенный граф zone1, слой3, 88×72 | [01-old-ai.png](../../build-ai-panel-229/evidence-offscreen/01-old-ai.png) |
| Navmesh: сетка не запечена, запекатель не найден | [02-navmesh-missing.png](../../build-ai-panel-229/evidence-offscreen/02-navmesh-missing.png) |
| r13 zone1, Человек (human), заливка у ворот Посёлка | [03-navmesh-human.png](../../build-ai-panel-229/evidence-offscreen/03-navmesh-human.png) |
| Настоящее запекание r13: прогресс и Отмена | [04-baking.png](../../build-ai-panel-229/evidence-offscreen/04-baking.png) |
| Дополнительно: слой обновлён после bake | [05-baked-updated.png](../../build-ai-panel-229/evidence-offscreen/05-baked-updated.png) |

`tests/nav64_ai_panel229_check.cpp`, связанные production objects архивного Release: **98 PASS /0 FAIL**, exit0, **44,353с**. [stdout](../../build-ai-panel-229/offscreen-stdout.txt), [процесс/хеш harness](../../build-ai-panel-229/offscreen-process.json), [сохранность источников](../../build-ai-panel-229/offscreen-preservation.json).

- Принадлежность navmesh-кнопок панели «Навигация ИИ», isVisible и геометрия всех основных элементов внутри панели; панель внутри сцены, включая меньший MainWindow, прогресс, журнал и сворачивание/возврат. CView не отделялся от MainWindow.
- H дважды переключает старый граф и Navmesh; события физической H с кириллическим символом также переключают слой. Заливка/контуры дают разные framebuffer; отметки узких мест/островов переключаются.
- Обе кнопки запекания без EXE дают понятную ошибку без файлов вывода. Несохранённые входы отказывают с видимой причиной. Пустого поля пути и неактивной .navsrc нет.
- Настоящий r13 SHA256 `85df0ef34432bfc3164cb83067aa9afd1153fdf96ae39e44b61b636ad6dc5ad7` запущен production-функцией с `--output-root <private-run>/bake-output`. **Exit0, BAKE_DONE maps=1 completed=1 missing=0 errors=0**, восемь nav64, восемь PROFILE height_outliers=0; слой автоматически перечитан из нового output-root. [Журнал](../../build-ai-panel-229/evidence-offscreen/real-baker.log). MAP_DONE:7575мс, BAKE_DONE:7594мс.
- Регресс: реальные «Обновить граф» и «Сравнить графы» на zone1 с открытием отчёта;227 zone7 + Autozone7letter → zone6 → zone7 без лишнего MOB. Исходные MPR/MOB и встроенный граф zone1 побайтно сохранены.

Навыки private-engine/modding отсутствуют; приватный код движка не исследовался. Использованы обычный код маппера, разрешённый неизменный vendor и прямо разрешённые игровые копии/r13. Игровая проходимость и точный fingerprint сюда не входят; актуальность по датам приблизительна.

## Release и свежий staging

- Release из `git archive 086a99a2ccebf1f9840cea73873bd1e552df49f5`, Qt5.12.12/MinGW7.3 x64; оба runtime собраны штатным wrapper. `build-ai-panel-229/source-final/build-release/release`. [Build exit0](../../build-ai-panel-229/build-final-process.json).
- Сверены **2183 записи архива**:2182 совпали побайтно с входами сборки, отличается только генерируемый lrelease `translations/ei_mapper_ru.qm`. Потерь/непредусмотренных изменений нет. ZIP исходников SHA256 `E20E7BE3D1AA0AC30B40097C868E94B40E792287ADC50C33E06C0AED404CAC91`. [Receipt](../../build-ai-panel-229/archive-input-receipt.json). Первая версия вспомогательной сверки сравнила Git LF с преобразованными окончаниями строк архива; исправлена на точное сравнение ZIP entries, без изменения программы.
- Свежий staging создан `stage_release.ps1`. Raw/staged `--version`: **229**, четыре SHA EXE/MCP/PLG/RuntimeHost совпали. EXE SHA256 `AAE77A785B53702F2C4A1665ABA99C306E3449AD6EAE5E7D19161138CDEFAD83`. [Идентичность](../../build-ai-panel-229/staging-identity.json).
- Финализатор и внутренний validator **PASS** против228: **176 →176 managed,0 потерь/0 добавлений**. Manifest SHA256 `E9D38BD91D636AD88612431FDB7C8E9D7C8A61E3C16DB442B20B17AED373CBEA`. [Finalizer](../../build-ai-panel-229/finalize.log), [состав](../../build-ai-panel-229/composition.json). Пакет после финализации не запускался. Запекатель/игровые данные в него не включены.
- Игровые/тестовые копии удалены после PASS; квитанция `build-ai-panel-229/cleanup-receipt.json`. Исходные игры, мод и r13 handoff сохранены.

## Инцидент предыдущего запуска

Предыдущий способ проверки был ошибочным: агент запустил видимый тестовый MainWindow на рабочем столе владельца. Harness вызывал showNormal/move/raise/activateWindow; computer-use перечислял окна, выбирал тестовый маппер, активировал его и запрашивал снимки. **Один запрос снимка неактивного маппера вернул часть рабочего стола с посторонними окнами**. Этот кадр не сохранялся как файл приёмки и не включён в репозиторий/пакет, но присутствовал в ответе инструмента. После активации маппера сохранён лишь частичный предыдущий `evidence-attempt1/01-old-ai.png`. Последующий запуск владелец остановил физической Esc. Свой тестовый процесс агент завершил; после Esc в том ходе новых действий computer-use не было.

Вызовов computer-use click/press_key/type_text не было. H в harness подавалась QTest только своему процессу. Тем не менее появление/перемещение/фокусировка видимого окна и сам захват рабочего стола нарушили требуемую границу; старый снимок не принят как доказательство завершения. В текущем продолжении computer-use не вызывался, экран не захватывался, видимых окон не запускалось и фокус владельца не менялся.

Проверка только чтением файлов и метаданных окон/процессов:

- **Все19098 файлов постоянной установки** совпали с `installation-before.json`, снятым до ошибочных видимых запусков, по SHA256/размеру/mtime; набор файлов неизменен. VERSION228; **176/176 managed SHA совпали с её manifest**, включая EXE/runtime. Это также проверяет установленные пользовательские карты, файлы и endurance-options.json. `.mcp.json` совпал с исходным бэкапом. [Incident receipt](../../build-ai-panel-229/incident-preservation.json).
- **10/10 ранее учтённых HWND владельца** по-прежнему присутствуют и видимы. Использованы только EnumWindows/IsWindowVisible/GetWindowText/GetWindowThreadProcessId, без управления или получения пикселей. [Сравнение](../../build-ai-panel-229/incident-window-comparison.json). Перед продолжением тестовых/установленных процессов маппера не осталось; чужие процессы не завершались.
- Изначальные тесты уже использовали частные AppData/INI/профиль; runHeadlessCommand перенаправляет QSettings в свой временный каталог до MainWindow. Настройки ОС/реестр через UI не менялись. **До инцидента не было снимка произвольных документов и настроек остальных приложений**, поэтому их полную побайтную неизменность доказать нельзя. Наличие окон этого не доказывает. Посторонним окнам не отправлялись ввод/сохранение/закрытие, откат их состояния не выполнялся.
- Исходные правки ROADMAP владельца сохранены; в scoped-коммит входит только пункт229. Незнакомые/untracked файлы не затрагивались. Финальные проверки — `final-preservation.json` и `cleanup-receipt.json` в build-ai-panel-229.

Следующий этап — рассмотрение снимков владельцем. **Установка не выполнялась; автоматической установки после проверки нет.**
