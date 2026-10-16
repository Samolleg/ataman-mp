# Ataman197 — задержки редактора и MPR recovery

EI-EDITOR-STALLS-197, 16–17.10.2026. База196/main b1e132f6538733a539c728f2f9a77eea4d599e6b. Поручение: исправить повторные задержки выделения, удаления, перемещения и покраски, убрать лишнюю валидацию и потерю SD/HD-кэша, сохранить ID, переходы, тени, миникарту и Undo/Redo; исправить автосохранение MPR, проверить и установить. Исходная диагностика — ATAMAN_EDITOR_STALL_AUDIT_196_20261016.md. Новое ядро переходов132–176 не возвращалось. Работа лично, без делегирования. Игровые оригиналы не изменялись.

## Изменения

- Полная проверка БД больше не запускается из общего indexChanged истории. Настоящие команды БД запрашивают проверку явно при push/Undo/Redo, в том числе при заполненном лимите истории и многошаговом setIndex. Панель использует прежние findings только для того же DatabaseDocument и его stateRevision; импорт, Save, ID и валидация промежуточных кандидатов не кэшируются и не отключаются.
- Исправлен безусловный сброс normalized display cache после удачного mixed SD/HD update. Сохраняются native и normalized snapshots, при смене uniform/mixed оба согласованы; изменённые слоты идут в существующий glTexSubImage2D с полным glGenerateMipmap. Reset/ошибка не используются как свидетельство успешной публикации.
- Инспектор тайлов создаёт делегаты один раз; при прежнем размере удерживает незатронутые значки, ячейки и выделение. Новые размеры перестраивают таблицу, изменённые изображения инвалидируют свои значки. Все изображения остаются в исходном разрешении.
- MPR recovery сохраняет рабочий RES, точный профиль по закодированным пикселям и shared-corner semantic field. На GUI создаётся неизменяемый снимок; кодирование, профиль и запись пары выполняются QtConcurrent без QObject/renderer/settings в захвате. Один MPR job одновременно; изменение исходного MPR во время подготовки отклоняет устаревшую публикацию.
- RES и MPR пишутся до JSON commit record. Дедупликация учитывает оба хэша и проверяет файлы; повреждённая/неполная пара не рекламируется и не открывается. Скопированная тройка MPR/-atlas.res/.json открывается по соседним файлам, без записи в игровые ресурсы. Хранятся20 полных поколений; живые карты не заменяются. При открытии verified autosave его собственный атлас имеет приоритет.

## Подтверждение

Приватные outputs: build-editor-stalls-197-20261016, вне Git/пакета. Release197 и оба runtime-компонента собраны в свежем build-release-197-editor-stalls; version identity и упаковка фиксируются отдельными квитанциями.

1. Native object/MainWindow: 100 PASS, exit0. Реальная БД DejaVu (29 таблиц/5629 строк), флаг workspaceLoadSmoke снят до проверяемых команд, нормальный scheduler включён. На196 тот же регресс воспроизвёл11 FAIL: повторная DB validate, выбор и mixed-cache/инспектор. Три обновления панели196 —1294ms;197 —0ms после первого расчёта334ms. Отчёты не скрываются. Проверены настоящее изменение БД, push/Undo/Redo и предел истории.
2. Синтетический MOB2000/1,5MB,24 объекта: Delete59ms, Undo65/Redo58ms; copy121ms, paste preview324ms, commit31ms, paste Undo83/Redo414ms; move33ms, Undo34/Redo33ms. Сохранены скриптовое подтверждение, владение MOB при смене активного, логические дочерние узлы и точные bytes/order/IDs. Save/reopen; удалённый ID остаётся занят в Undo даже после сохранения. Эти значения не обещают мгновенную вставку любого объёма.
3. Mixed SD/HD128 слотов: три публикации196 —48ms;197 —4ms. Change/Undo/Redo/no-op, рост/сжатие, mixed→uniform→mixed и invalid candidate. GPU pixels совпали с fresh full atlas на всех mip; исходные размеры/данные, текущий slot/иконки сохранены, число делегатов не растёт.
4. Миникарта: настоящая GL-сцена,1024, alpha-cutout, полные lit/unlit pixels, grading/MMP; Delete/Undo/Redo, асинхронный worker, generation/stale/locked/workspace guards. Качество не снижено.
5. Native HD/Recovery: 363 PASS, exit0. На частной копии zone19 настоящие кнопки/Cancel,38014 замен,288 вариантов, точные внешние кромки196, переходы/вода/анимации/типы, GPU. Два Save/reopen, оригинальные MMP побайтно сохранены. Recovery reopen совпал с обычным Save по всем native pixels и клеткам, shared-corner семантика восстановлена. Снимок на GUI188ms, worker1649ms,102 heartbeat/maxGap30ms. Автосохранение не изменило текущую модель, атлас и историю.
6. BackupManager QtTest:11 PASS/0 FAIL/0 SKIP. MPR+RES+JSON, повтор, atlas-only change, copied trio, обрыв до JSON, повреждённые/отсутствующие MPR/RES, отказ по source hash и retention23→20 целых поколений; прежние MOB backups и session notifications.
7. Общая кисть через zone13/bz10k: PASS,16 граничных клеток, shared Undo/Redo, сравнение с полным renderer/atlas/mip и тенями, переоткрытие соседа. Изменения текстур дали0 shadow invalidations. Повторные border stamps17–25ms, внутренние14–40ms.

## Границы

В cross-map synthetic cold-start первые два resolve заняли3638/2207ms (resolve3275/2185ms); это остающийся первый расчёт/гидратация нового перехода. Этот результат не выдается за исправление всех возможных холодных задержек. Повторные паузы из журнала413–637ms DB UI и542–750ms каждого mixed stamp адресованы отдельно и воспроизведены тестами. Первое открытие/проверка БД и подготовка GUI-снимка autosave имеют конечную цену, указанную выше. Автосохранение здесь проверено для активной MPR; мировой Save cohort не изменялся.

Native hidden windows/GL и файловые readback не заменяют визуальную приёмку владельцем на рабочей карте или игровой прогон. Live desktop editor не запускался. Общий clipboard UI smoke не запускался: адресные тесты не меняли системный буфер. Source originals MPR/RES в live-inputs.json перепроверены; тесты писали только частные копии.

## Идентичность сборки

Source main `024c5d28587ecaf4b8ef5fc0b2686427103ab007`; 274 production objects и 459 source input hashes записаны в private receipts. make -q release подтвердил актуальность. EXE `0cbbf20455aa408bf67997ae975b9393e03f91d3fddb45a27eccf1aae68f26a1`. Runtime-компоненты собраны wrapper этой версии.

## Установка17.10.2026

- Постоянная artifacts/EI-Mapper-Ataman-2-first-test обновлена до197 после проверки отсутствия процессов из неё. Fresh staging/raw version197, одинаковые EXE/runtime, штатный finalizer PASS; после finalizer пакет не запускался.
- Все130 managed совпали с manifest `B4D2F2D4228D32DD4550C65BF46D859198758FD6C97C99E61BFA68AFA5492654`. EXE `0CBBF20455AA408BF67997AE975B9393E03F91D3FDDB45A27ECCF1AAE68F26A1`; source main `024c5d28587ecaf4b8ef5fc0b2686427103ab007`.
- Все16915 owner-файлов вне manifest побайтно сохранены, digest `16915:1B5BC80CCCE8E6A2DD12B567E02292931B569B98BD8CDD328090CFFFD62D7057`. Настройки, карты, сохранения, кэш и HD-банк установки не очищались.
- Резерв196: artifacts/rollback196-to197-editor-stalls-20261016. Его реконструкция по старому manifest проверена перед удалением только прежнего проверенного rollback195-to196-base-hd-20261016. Другие архивы/установки не менялись.
- Квитанции: build-receipt.json, staging-receipt.json, final-probe-receipt.json, install-receipt.json, rollback-retirement.json в private task folder. Реализация и установка адресных исправлений завершены; ограничения холодного resolve и приёмки владельцем указаны выше. GitHub этим поручением не обновлялся.
