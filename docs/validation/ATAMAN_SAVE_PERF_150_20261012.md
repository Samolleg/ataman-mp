# EI-SAVE-PERF-150 — сохранение и recovery autosave

Дата: 2026-10-12
База: `main` VERSION 149, `b36fe4a30002e07e53b0a703021fa2f7bfe0d512`
Измерения RES: приватные копии `%TEMP%\EI-SAVE-PERF-150`; исходная установка и данные игры не менялись.

## Причины

- В журнале 149 `workflow-20261011-234905.log` переход `Save MPR route` → `Modern terrain publication authorized` занимает около 9 секунд; во время интервала зарегистрирован FPS 1,8. Код публикации строил и заново валидировал весь HDPack синхронно, включая чтение, сборку и контроль неизменённых членов.
- Отказ recovery MPR с `mpr.test.atlas.destination` подтверждён кодом: проверка рабочей карты требовала выходной `terrainAtlasResource`, но autosave вызывал builder без этого адреса. Защита корректно отказывала, чтобы не потерять unsaved working atlas. Recovery теперь сохраняет MPR и зависимый `terrain.res` одной проверенной генерацией.
- Журнал 149 фиксирует около 2 секунд автосохранения MOB и 15-секундный countdown. Countdown не давал владельцу полезного выбора и откладывал снимок. Сериализация MOB и запись поколений перенесены в worker; MPR builder/валидация и paired atlas также выполняются там после снятия значения снимка в GUI.
- Длинные всплески 05:21:50–05:22:37 и 05:23:20 не совпадают по времени с autosave: события сохранения начинаются в 05:22:55. В renderer telemetry очередь `collect/draw` достигает секунд; это корреляция с параллельной сборкой Codex, а не доказательство самостоятельной причины Ataman. Источники журналов не позволяют доказать причинность сборки.

## Решение по RES и целостности

Частичная запись/append не используется. Читатель RES ищет члены через таблицу и hash-chain; существующий layout может содержать старые данные и таблицу в другом месте, поэтому добавление новой таблицы в конец не гарантирует совместимость со старым игровым читателем. Сборщик публикует полный кандидат через `QSaveFile` с отключённым direct-write fallback, предварительной резервной копией, source/destination race checks и повторным открытием после commit. Это сохраняет прежнюю схему и не ослабляет защиту.

Тест публикации zone1 и zone9 добавлял один тестовый MMP в копию HDPack. Для каждого исходного члена после сохранения повторно открыт архив и payload каждого из 1226 исходных членов побайтно сравнён с исходным; новый MMP также сравнен побайтно. Время процесса/публикации не блокирует UI в штатном маршруте: сборка, резервирование, запись, replace и post-commit проверка выполняются в worker с фазовым прогрессом. Тест прерывания завершал дочерний publisher после частичной записи во временный sidecar; исходный именованный RES оставался побайтно неизменным.

## Измерения RES на копиях

Размер HDPack: 1 312 276 405 байт; 1226 исходных членов. Значения — `elapsedMs` из worker журнала; фазы одной публикации последовательны.

| Фаза | zone1 | zone9 |
|---|---:|---:|
| Чтение source | 290 | 206 |
| Чтение destination | 617 | 396 |
| Сборка кандидата и проверка членов | 4489 | 4479 |
| Запись/flush stage | 470 | 458 |
| Переоткрытие и проверка stage | 4047 | 3881 |
| Ожидание читателей | 0 | 0 |
| Backup + SHA-256 | 8037 | 7981 |
| Запись/flush кандидата | 681 | 348 |
| Atomic replace/durable commit | 1106 | 1114 |
| Post-commit reopen/hash | 4344 | 4250 |
| Сумма фаз | **24 081** | **23 113** |

Оба backup-файла имеют SHA-256 исходного HDPack `79ae99a3bd47cde0b36e73a774b20e2cfbabcdd9491aad49dc1e1cf8d3376909`. Это изолированный тест RES coordinator с тестовым MMP, не полное интерактивное сохранение реального MPR. В ходе данного измерения пик working set теста кратковременно достигал примерно 12,9 ГБ; RES путь всё ещё держит несколько полных представлений большого архива. Работа вынесена с GUI-потока, но расход RAM остаётся существенным ограничением для машин с низкой памятью.

### Autosave и GUI

149: autosave MOB около 2 с на 3 правках; отсчёт перед сохранением — 15 с. 150: timer сразу начинает capture; MOB snapshot и MPR snapshot замеряются отдельными GUI-фазами, сериализация/валидация и запись поколений логируются worker-фазами. Отсчёт удалён. Для MPR recovery снимок включает копии MPR/edit model/materials/active atlas и authored pages; validation и пакетная сборка идут в worker. GUI snapshot duration залогируется при первом autosave с реальной картой.

`--terrain-height-res-save-check` измерил GUI snapshot на синтетической карте: 0 мс при MPR 13 787 байт. Worker builder и его MPR/paired RES candidate проверены этим же сценарием. Интерактивный редактор по заданию не запускался, поэтому на реальных zone1/zone9 MPR не получен runtime замер GUI capture и не подтверждено числом ограничение ~100 мс для больших карт. Save/backup tests покрывают контрактные MPR/MOB пути на тестовых копиях; они не заменяют этот runtime замер.

15-секундный countdown убран: recovery snapshot не требует подтверждения и больше не должен задерживать сохранение. Пока фоновая генерация идёт, отображается ненавязчивое состояние; пользователь может продолжать редактирование, а новый autosave не начинает вторую конкурирующую запись.

## Кодировка

`QTextStream` workflow logger принудительно использует UTF-8. Русский текст validation findings в новых логах записывается с корректной кодировкой.

## Проверки

- Release build VERSION 150: `mingw32-make -j4 release` — PASS; существующие предупреждения компилятора остаются.
- RES archive tests: 29 passed / 0 failed / 0 skipped; backup manager tests: 13 / 0 / 0.
- Kill/interruption test: именованный исходный архив побайтно неизменен после убийства publisher на частичной записи временного файла.
- `--ui-widget-check`: exit 0, `nativeAttempts=0`, sequence/formats/clipboard unchanged; source Release под `build-locks/ui-tests.lock`. После сборки package exact EXE (SHA-256 ниже) повторно прошёл тот же check под lock на изолированной копии package runtime.
- `--scenario-layer-check`: 49 passed / 0 failed; `--dark-theme-check`: 0 failures.
- `--terrain-height-res-save-check`: PASS для смешанной покраски+высоты, повторного открытия и отсутствия повторной публикации при height-only save. Точный package EXE повторно прошёл этот check: GUI snapshot 0 мс, MPR 13 099 байт; worker MPR и paired RES candidate PASS.
- `--terrain-hd-republish-check`: PASS; результаты на копиях zone19 и zone6, соответственно 47 и 602 уже имеющихся страниц применены без потери соседних членов.
- `--terrain-hd-variants-check`: PASS на 79-map fixture; 6 итоговых страниц, проверка записей только в `build-tests-150-save-perf`.
- `--terrain-mixed-edge-check`: 67 passed / 0 failed / 0 skipped; `--terrain-random-paint-check`: 82 / 0 / 0, включая exact undo/redo bytes.
- Миниатюры: 149 cold check — 0/5 PASS, видимый queue 1–2/5, median first sample 194 мс и elapsed 299 мс; 150 — 5/5 PASS, median first sample 34 мс, elapsed 44 мс, GUI gap 6–8 мс. Это диагностический benchmark; 150 добавляет только его timing output.
- Backup MPR paired atlas проверен тестами manager: generation/manifest/sha и связанный `terrain.res`; путь назначения передаётся в autosave builder, отказ `mpr.test.atlas.destination` не маскируется ослаблением валидации.

## Release и установка

- Реализационный commit: `7427aa67e45b4659620bdbaa10ebb73318c8f415`; чистый `git archive` SHA-256 `D15656B79C6DD04F51757DA05E5F3E649373906815DD6CA0E4124C29581C331F`. Archive Release VERSION150, `--version` PASS. Точный Release EXE, staging и установленный EXE побайтно совпадают, SHA-256 `A32F9C3558F0446E7D051677AE2A719BCD9C35A8F048315A72B437E2327F85B4`.
- Staging manifest SHA-256 `2633F6A1FD2A464049AC9A4491900536DAD02D39833A348C8B27342F46856513`. Finalizer на свежей staging-папке: 129 прежних / 130 текущих / 0 удалённых / 0 необъяснённых удалений. Передача в `artifacts/EI-Mapper-Ataman-2-first-test`: 6 заменяемых файлов, 1 новый отчёт, 0 удалений; `rollback149-to150-EI-SAVE-PERF-150-20261012` содержит исходные программные файлы и SHA-256. После установки все 130 managed paths совпали с manifest; все 22 941 unmanaged-файла (4 516 692 037 байт) сохранились; inventory SHA-256 до/после `649545EF0904CF53CF1C23CE6DB4300578A1BB88A6D2E6CC4C4901634BD0E1A3`.
- Ограничения: интерактивный редактор/игра и runtime autosave реальных zone1/zone9 MPR/MOB не запускались. Capture 0 мс — синтетическая карта ~13 KB, поэтому latency больших реальных документов и реальное время фаз autosave MPR/MOB на целевой установке не измерены. RES coordinator на HDPack-копии 1,31 GB исполнялся в worker; его peak working set около 12,9 GB может ограничивать системы с низкой RAM. Game compatibility проверена парсером игрового формата и побайтной проверкой payloads 1226 членов, но игровой запуск/ручная приёмка владельца не выполнялись.
