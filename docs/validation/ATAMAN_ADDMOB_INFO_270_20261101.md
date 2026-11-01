# EI-ADDMOB-INFO-270 — 2026-11-01

База: main `77c9099f5fb21239916d4af8204c6572b375365d`, установленная269.
Версия270. STOP BEFORE INSTALLATION; push не выполняется.

Причина: `prepareBackgroundMob` переносил все `CValidationReport::findings()`
в `warnings`, а `MainWindow::pumpBackgroundMobs` выводит этот список через
`eLogWarning` и уведомление AddMob. Теперь Info исключаются; Warning, Critical
и Error сохраняются. Critical не скрываются как потенциально опасные находки.
Парсер, байты MOB и последовательность загрузки не изменены.

Проверены соседние пути: `neighbor_scene_data.cpp` добавляет диагностику отчёта
при отказе загрузки, а не при успешном разборе; `mob_family_validation.cpp`
формирует предметные предупреждения AddMob; синхронный `CMob::readMob`
фильтрует severity. Другого безусловного сбора успешных Info в warnings
в этих путях нет. Посторонние операции рельефа не изменялись.

Проверки — отдельные процессы, native Qt/OpenGL с WA_DontShowOnScreen;
собственные AppData, settings, automation, backup, TEMP и копии карт.
Установленный маппер, видимые окна, computer-use, буфер и игра не запускались.
Новый `tests/addmob_info_check.cpp`: **16 PASS**, exit0. Проверяет реальные
Info парсера, чистый AddMob без уведомления, duplicate_id с исходной строкой
в уведомлении, отсутствие обоих Info в нём и побайтные no-op кандидаты.
Журнал: чистый loaded:1/warnings:0; duplicate loaded:1/warnings:1 и
`{WARNING} ID 42 occurs 2 times; it was preserved`. Info разбора/сериализации
в Warning отсутствуют. Evidence: `work/addmob-info-270-20261101/`.

`mob_load_pipeline_check` из принятого HEAD, без чужих незакоммиченных изменений:
**74 PASS / 0 FAIL**, exit0; включает первичную карту до AddMob, heartbeat,
0 промежуточных кадров при загрузке, полное семейство, raw no-op, ошибку загрузки,
standalone, циклы/общие ссылки/повреждённый и отсутствующий MOB, коллекцию,
отмену при переключении и изменении скрипта, холодный/тёплый порядок соседей
и выключение соседей. Квитанции процессов — отдельные result.json.

Release и оба runtime собраны exit0 из git archive в свежем shadow-build.
Production tree не включает незакоммиченные изменения владельца. Архивный
тест270 после исправления private-доступа пересоздан отдельно; production-код
не менялся после сборки. Исходный и staged CLI сообщают270, SHA четырёх
исполняемых компонентов совпали. Staging:
`build-staging-270-addmob-info-20261101`. Финализатор PASS, состав против269:
187 прежних путей,188 текущих,0 удалённых; добавлен этот отчёт.
Квитанция — `package270.json`, журнал — `finalization270*.log`.

После exit0 проверены SHA/размер/mtime106 исходных файлов. Собственные копии
карт/RES, тестовые app/settings/AppData/backups и options удалены; сохранены
текстовые журналы и сводные квитанции. Незакоммиченные файлы владельца совпали
с исходными SHA; ROADMAP коммитится только с новым собственным пунктом поверх
HEAD. Evidence: `cleanup-result.json`, `owner-before.json` и staged diff.
Повторная финализация после внесения итогового отчёта и SHA коммита нужна
только потому, что эти файлы пакета изменились; программу после финализации
не запускаем. Ручная приёмка владельцем и установка в этот запрос не входят.
Постоянная установка269 не изменялась; push не выполнялся.

## Установка270 — решение владельца 2026-11-01

Владелец принял main `1277fb1d8d043416acb4e02708a17e9c83801b14` и сказал
«ставь»: прежний STOP BEFORE INSTALLATION снят этим новым поручением.
Установлен готовый Release из проверенного staging
`build-staging-270-addmob-info-20261101`, без пересборки, через канонический
`packaging/install_program.ps1`. BUILD_INFO: Release tree
`50f5ab3cb55f7326fb6e0f599b18d1aa88be2c90`.

Постоянный target: `artifacts/EI-Mapper-Ataman-2-first-test`.
Откат: `artifacts/rollback269-to270-addmob-info-20261101`;
его собственный `rollback.ps1 -VerifyOnly` вернул `ROLLBACK_VERIFIED`.
Установщик:188 проверенных управляемых файлов,9 замен,1 добавление,0 удалений.
Все188 SHA совпали со staging; установленный SHA256SUMS.txt:
`2DBFBDBA3CD0CE446367691FF2BD5594C02D161B35D03662BED6D03ECAB7FF85`.
Установленный `--version`: `EI Mapper Endurance Edition 270`, exit0,
процесс без видимого окна.13415 пользовательских файлов сохранили
SHA256, размер и LastWriteTimeUtc. Финализатор на установке не запускался.

Перед установкой все EIMapperEndurance (включая headless), runtime host и игра
закрыты. EIMapperMcp PID до/после:
22272,20980,19576,11456,25200,22564,27876,10880,9564.
Все9 живы с прежним временем создания. Никакие процессы не завершались.
Занятый файл `EIMapperMcp.exe` отодвинут в
`EIMapperMcp.exe.old-ataman-88c5cfe5bc034357ba66bd42daa067c7`;
его старый SHA256 `792CCE25DA24241A74368DD70552D1B2F6291FBDA5F625F9DDEA4437280DCE00`.
Канонический EIMapperMcp.exe установлен из270, SHA256
`D05C626EA8FE8C1C5C3478F20CBAEE90FAED600405C9775B74075E2F8CA5FFED`.
Прежний `EIMapperMcp.exe.old-ataman-25d26775d0d44bcbaa2866a7a1ebb57c`
сохранён; очистка retired:0 удалено,1 сохранён до новой замены.
Прежние откаты не удалялись.

Регистрация ~/.codex/config.toml и .mcp.json, исходники/QM владельца
сохранили SHA. ROADMAP получил только запись этой установки и завершение
её следующего шага; чужие строки сохранены и исключены из staged diff.
Без интерактивного запуска маппера/игры, computer-use, управления столом,
видимых окон, дополнительных функциональных проверок и push.
Приёмка270 владельцем получена; этот этап подтверждает установку.
Evidence: `work/addmob-info-270-20261101/install270/receipt.json`,
`installer.log`, `rollback-verify.log`, `bridges-before.json`,
`bridges-after.json`, `user-before.json`, `user-after.json`.
